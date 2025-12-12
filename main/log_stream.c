#include "log_stream.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <stdarg.h>

static const char *TAG = "LOG_STREAM";

#define MAX_SSE_CLIENTS 4
#define LOG_BUFFER_SIZE 512
#define KEEPALIVE_INTERVAL_SEC 30
#define CHECK_INTERVAL_MS 1000

// SSE client list
static int sse_clients[MAX_SSE_CLIENTS];
static SemaphoreHandle_t clients_mutex = NULL;

// Original log handler (pour chaînage)
static vprintf_like_t original_log_handler = NULL;

// Watchdog thread handle
static TaskHandle_t watchdog_task_handle = NULL;

// Custom log handler qui intercepte tous les logs ESP-IDF
static int custom_log_handler(const char *fmt, va_list args)
{
    // Appeler le handler original pour garder les logs USB/UART
    int ret = 0;
    if (original_log_handler) {
        va_list args_copy;
        va_copy(args_copy, args);
        ret = original_log_handler(fmt, args_copy);
        va_end(args_copy);
    }

    // Si on a des clients SSE connectés, envoyer le log
    int client_count = log_stream_get_client_count();
    if (client_count > 0) {
        // Parser le format ESP-IDF: "X (12345) TAG: message"
        // où X = E/W/I/D/V pour Error/Warning/Info/Debug/Verbose
        char buffer[LOG_BUFFER_SIZE];
        vsnprintf(buffer, sizeof(buffer), fmt, args);

        // Extraire niveau, tag et message du format ESP-IDF
        char level_char = 'I';  // Par défaut INFO
        char tag[32] = "APP";
        char *message = buffer;

        // Format typique: "I (12345) TAG: message"
        if (buffer[0] && buffer[1] == ' ' && buffer[2] == '(') {
            level_char = buffer[0];

            // Trouver le tag (entre ") " et ": ")
            char *tag_start = strstr(buffer, ") ");
            if (tag_start) {
                tag_start += 2;
                char *tag_end = strstr(tag_start, ": ");
                if (tag_end) {
                    size_t tag_len = tag_end - tag_start;
                    if (tag_len < sizeof(tag)) {
                        memcpy(tag, tag_start, tag_len);
                        tag[tag_len] = '\0';
                        message = tag_end + 2;  // Skip ": "
                    }
                }
            }
        }

        // Convertir le niveau en string
        const char *level_str = "info";
        switch (level_char) {
            case 'E': level_str = "E"; break;
            case 'W': level_str = "W"; break;
            case 'I': level_str = "I"; break;
            case 'D': level_str = "D"; break;
            case 'V': level_str = "V"; break;
        }

        // Enlever le '\n' final si présent
        size_t msg_len = strlen(message);
        if (msg_len > 0 && message[msg_len - 1] == '\n') {
            message[msg_len - 1] = '\0';
        }

        // Envoyer aux clients SSE
        log_stream_send(message, level_str, tag);
    }

    return ret;
}

// Watchdog task: monitors all SSE clients for disconnection and sends keepalives
static void sse_watchdog_task(void *pvParameters)
{
    (void)pvParameters;
    int keepalive_counter = 0;
    char dummy_buf[1];

    ESP_LOGI(TAG, "SSE watchdog task started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CHECK_INTERVAL_MS));
        keepalive_counter++;

        xSemaphoreTake(clients_mutex, portMAX_DELAY);

        for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
            int fd = sse_clients[i];
            if (fd < 0) continue;

            // Check if client is still connected (non-blocking peek)
            int peek_result = recv(fd, dummy_buf, sizeof(dummy_buf), MSG_PEEK | MSG_DONTWAIT);

            if (peek_result == 0) {
                // FIN received, client disconnected
                ESP_LOGI(TAG, "SSE client fd=%d disconnected gracefully, closing socket", fd);
                close(fd);
                sse_clients[i] = -1;
                continue;
            } else if (peek_result < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
                // Connection error
                // EBADF (errno=9) est normal si le socket a été fermé côté httpd
                if (errno == EBADF) {
                    ESP_LOGD(TAG, "SSE client fd=%d socket already closed", fd);
                } else {
                    ESP_LOGW(TAG, "SSE client fd=%d connection error (errno=%d), closing socket", fd, errno);
                    close(fd);
                }
                sse_clients[i] = -1;
                continue;
            }

            // Send keepalive every KEEPALIVE_INTERVAL_SEC seconds
            if (keepalive_counter >= KEEPALIVE_INTERVAL_SEC) {
                const char *keepalive = ": keepalive\n\n";
                int sent = send(fd, keepalive, strlen(keepalive), MSG_DONTWAIT);
                if (sent <= 0) {
                    ESP_LOGW(TAG, "Failed to send keepalive to fd=%d, closing socket", fd);
                    close(fd);
                    sse_clients[i] = -1;
                }
            }
        }

        if (keepalive_counter >= KEEPALIVE_INTERVAL_SEC) {
            keepalive_counter = 0;
        }

        xSemaphoreGive(clients_mutex);
    }
}

esp_err_t log_stream_init(void)
{
    // Create mutex for client list
    clients_mutex = xSemaphoreCreateMutex();
    if (!clients_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize client array
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        sse_clients[i] = -1;
    }

    // Create watchdog task for SSE client monitoring
    BaseType_t ret = xTaskCreate(
        sse_watchdog_task,
        "sse_watchdog",
        3072,  // Stack size
        NULL,
        5,     // Priority
        &watchdog_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SSE watchdog task");
        vSemaphoreDelete(clients_mutex);
        return ESP_FAIL;
    }

    // Installer le hook de log pour intercepter tous les logs ESP-IDF
    original_log_handler = esp_log_set_vprintf(custom_log_handler);

    ESP_LOGI(TAG, "Log streaming initialized (max %d clients, watchdog running)", MAX_SSE_CLIENTS);
    return ESP_OK;
}

esp_err_t log_stream_send_headers(int fd)
{
    // Send HTTP headers
    const char *headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";

    int sent = send(fd, headers, strlen(headers), 0);
    if (sent <= 0) {
        ESP_LOGE(TAG, "Failed to send SSE headers to fd=%d", fd);
        return ESP_FAIL;
    }

    // Send initial comment
    const char *init_msg = ": SSE stream connected\n\n";
    send(fd, init_msg, strlen(init_msg), 0);

    // Send initial status event
    const char *status_msg = "data: {\"level\":\"I\",\"tag\":\"LOG_STREAM\",\"message\":\"Streaming démarré\"}\n\n";
    send(fd, status_msg, strlen(status_msg), 0);

    return ESP_OK;
}

esp_err_t log_stream_register_client(int fd)
{
    if (fd < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(clients_mutex, portMAX_DELAY);

    // Find empty slot
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (sse_clients[i] == -1) {
            sse_clients[i] = fd;
            xSemaphoreGive(clients_mutex);
            ESP_LOGI(TAG, "SSE client registered (fd=%d, slot=%d)", fd, i);
            return ESP_OK;
        }
    }

    xSemaphoreGive(clients_mutex);
    ESP_LOGW(TAG, "Max SSE clients reached, rejecting fd=%d", fd);
    return ESP_ERR_NO_MEM;
}

void log_stream_unregister_client(int fd)
{
    xSemaphoreTake(clients_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (sse_clients[i] == fd) {
            sse_clients[i] = -1;
            ESP_LOGI(TAG, "SSE client unregistered (fd=%d, slot=%d)", fd, i);
            break;
        }
    }

    xSemaphoreGive(clients_mutex);
}

int log_stream_get_client_count(void)
{
    int count = 0;

    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (sse_clients[i] >= 0) {
            count++;
        }
    }
    xSemaphoreGive(clients_mutex);

    return count;
}

// Escape a string for JSON (RFC 8259 compliant)
static void json_escape(const char *src, char *dst, size_t dst_size)
{
    if (!src || !dst || dst_size < 2) {
        if (dst && dst_size > 0) dst[0] = '\0';
        return;
    }

    size_t j = 0;
    const size_t max_j = dst_size - 7;  // Reserve space for worst case: "\uXXXX" + '\0'

    for (size_t i = 0; src[i] && j < max_j; i++) {
        unsigned char c = (unsigned char)src[i];

        switch (c) {
            case '"':   dst[j++] = '\\'; dst[j++] = '"';  break;
            case '\\':  dst[j++] = '\\'; dst[j++] = '\\'; break;
            case '\b':  dst[j++] = '\\'; dst[j++] = 'b';  break;
            case '\f':  dst[j++] = '\\'; dst[j++] = 'f';  break;
            case '\n':  dst[j++] = '\\'; dst[j++] = 'n';  break;
            case '\r':  dst[j++] = '\\'; dst[j++] = 'r';  break;
            case '\t':  dst[j++] = '\\'; dst[j++] = 't';  break;

            default:
                if (c < 0x20) {
                    // Control characters: use \uXXXX format
                    j += snprintf(&dst[j], dst_size - j, "\\u%04x", c);
                } else {
                    // Normal printable character
                    dst[j++] = c;
                }
                break;
        }
    }
    dst[j] = '\0';
}

void log_stream_send(const char *message, const char *level, const char *tag)
{
    if (!message || !level || !tag) {
        return;
    }

    int client_count = log_stream_get_client_count();
    if (client_count == 0) {
        return;  // No clients connected, skip
    }

    // Escape message for JSON
    char escaped_message[LOG_BUFFER_SIZE / 2];
    json_escape(message, escaped_message, sizeof(escaped_message));

    // Format SSE event
    // Format: data: {"level":"INFO","tag":"Main","message":"System started"}\n\n
    char buffer[LOG_BUFFER_SIZE];
    int len = snprintf(buffer, sizeof(buffer),
                      "data: {\"level\":\"%s\",\"tag\":\"%s\",\"message\":\"%s\"}\n\n",
                      level, tag, escaped_message);

    if (len <= 0 || len >= sizeof(buffer)) {
        return;  // Buffer overflow or error
    }

    // Send to all connected clients
    xSemaphoreTake(clients_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (sse_clients[i] >= 0) {
            int sent = send(sse_clients[i], buffer, len, MSG_DONTWAIT);
            if (sent < 0) {
                // Client disconnected or error, mark for removal
                ESP_LOGD(TAG, "Failed to send to SSE client fd=%d, removing", sse_clients[i]);
                sse_clients[i] = -1;
            }
        }
    }

    xSemaphoreGive(clients_mutex);
}
