#include "log_stream.h"

#include "esp_log.h"
#include "spiffs_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *TAG = "LOG_STREAM";

#define MAX_SSE_CLIENTS 4
#define LOG_BUFFER_SIZE 512
#define KEEPALIVE_INTERVAL_SEC 30
#define CHECK_INTERVAL_MS 1000
#define LOG_FILE_MAX_SIZE (12 * 1024)
#define LOG_ROTATE_BOOT_MAX 6
#define LOG_FILE_MESSAGE_BUFFER_SIZE (LOG_BUFFER_SIZE * 12)

static const char *LOG_BOOT_COUNT_PATH = "/spiffs/logs/boot_count";
static char current_log_path[64]       = "/spiffs/logs/logs.1.txt";

// SSE client list
static int sse_clients[MAX_SSE_CLIENTS];
static SemaphoreHandle_t clients_mutex     = NULL;
static SemaphoreHandle_t log_file_mutex    = NULL;
static MessageBufferHandle_t log_file_msg_buffer = NULL;

// Original log handler (for chaining)
static vprintf_like_t original_log_handler = NULL;

// Watchdog thread handle
static TaskHandle_t watchdog_task_handle   = NULL;
static bool file_logging_enabled           = false;
static uint32_t current_log_index          = 1;
static TaskHandle_t log_file_task_handle   = NULL;

static char log_line_buffer[LOG_BUFFER_SIZE];

static size_t log_file_get_size_no_log(const char *path) {
  if (!path) {
    return 0;
  }

  struct stat st;
  if (stat(path, &st) != 0) {
    return 0;
  }

  return (size_t)st.st_size;
}

static void log_file_set_current_path(uint32_t index) {
  if (index < 1 || index > LOG_ROTATE_BOOT_MAX) {
    index = 1;
  }
  current_log_index = index;
  snprintf(current_log_path, sizeof(current_log_path),
           "/spiffs/logs/logs.%u.txt", (unsigned)index);
}

static void log_file_truncate_current(void) {
  FILE *f = fopen(current_log_path, "w");
  if (f != NULL) {
    fclose(f);
  }
}

static void log_file_rotate_if_needed(size_t incoming_len) {
  size_t current_size = log_file_get_size_no_log(current_log_path);
  if (current_size + incoming_len <= LOG_FILE_MAX_SIZE) {
    return;
  }

  log_file_truncate_current();
}

static uint32_t log_boot_count_read(void) {
  uint32_t count = 0;
  FILE *f = fopen(LOG_BOOT_COUNT_PATH, "rb");
  if (f == NULL) {
    return 0;
  }

  (void)fread(&count, 1, sizeof(count), f);
  fclose(f);
  return count;
}

static void log_boot_count_write(uint32_t count) {
  FILE *f = fopen(LOG_BOOT_COUNT_PATH, "wb");
  if (f == NULL) {
    return;
  }

  (void)fwrite(&count, 1, sizeof(count), f);
  fclose(f);
}

static void log_file_append_line(const char *line, size_t len) {
  if (!file_logging_enabled || !line || len == 0 || !log_file_msg_buffer) {
    return;
  }

  (void)xMessageBufferSend(log_file_msg_buffer, line, len, 0);
}

static void log_file_writer_task(void *arg) {
  (void)arg;
  char buffer[LOG_BUFFER_SIZE];

  while (1) {
    if (!file_logging_enabled || !log_file_msg_buffer || !log_file_mutex) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    size_t len = xMessageBufferReceive(log_file_msg_buffer, buffer, sizeof(buffer), pdMS_TO_TICKS(500));
    if (len == 0) {
      continue;
    }

    xSemaphoreTake(log_file_mutex, portMAX_DELAY);
    log_file_rotate_if_needed(len);
    FILE *f = fopen(current_log_path, "a");
    if (f != NULL) {
      fwrite(buffer, 1, len, f);
      fclose(f);
    }
    xSemaphoreGive(log_file_mutex);
  }
}

// Custom log handler that intercepts all ESP-IDF logs
static int custom_log_handler(const char *fmt, va_list args) {
  // Call original handler to keep USB/UART logs
  int ret = 0;
  if (original_log_handler) {
    va_list args_copy;
    va_copy(args_copy, args);
    ret = original_log_handler(fmt, args_copy);
    va_end(args_copy);
  }

  const char *task_name = pcTaskGetName(NULL);
  if (task_name && strcmp(task_name, "sys_evt") == 0) {
    return ret;
  }

  bool file_logging = file_logging_enabled;
  int client_count = log_stream_get_client_count();
  if (client_count == 0 && !file_logging) {
    return ret;
  }

  // Format raw log line once, and reuse it
  int raw_len = vsnprintf(log_line_buffer, sizeof(log_line_buffer), fmt, args);
  if (raw_len > 0 && raw_len < (int)sizeof(log_line_buffer)) {
    size_t len = (size_t)raw_len;
    if (log_line_buffer[len - 1] != '\n' && len + 1 < sizeof(log_line_buffer)) {
      log_line_buffer[len] = '\n';
      log_line_buffer[len + 1] = '\0';
      len += 1;
    }
    if (file_logging) {
      log_file_append_line(log_line_buffer, len);
    }
  }

  if (client_count > 0) {
    // Parse ESP-IDF format: "X (12345) TAG: message"
    // where X = E/W/I/D/V for Error/Warning/Info/Debug/Verbose
    char level_char = 'I'; // Default INFO
    char tag[32]    = "APP";
    char *message   = log_line_buffer;

    // Typical format: "I (12345) TAG: message"
    if (log_line_buffer[0] && log_line_buffer[1] == ' ' && log_line_buffer[2] == '(') {
      level_char      = log_line_buffer[0];

      // Find tag (between ") " and ": ")
      char *tag_start = strstr(log_line_buffer, ") ");
      if (tag_start) {
        tag_start += 2;
        char *tag_end = strstr(tag_start, ": ");
        if (tag_end) {
          size_t tag_len = tag_end - tag_start;
          if (tag_len < sizeof(tag)) {
            memcpy(tag, tag_start, tag_len);
            tag[tag_len] = '\0';
            message      = tag_end + 2; // Skip ": "
          }
        }
      }
    }

    const char *level_str = "info";
    switch (level_char) {
    case 'E':
      level_str = "E";
      break;
    case 'W':
      level_str = "W";
      break;
    case 'I':
      level_str = "I";
      break;
    case 'D':
      level_str = "D";
      break;
    case 'V':
      level_str = "V";
      break;
    }

    // Remove the trailing '\n' if present
    size_t msg_len = strlen(message);
    if (msg_len > 0 && message[msg_len - 1] == '\n') {
      message[msg_len - 1] = '\0';
    }

    // Send to SSE clients
    log_stream_send(message, level_str, tag);
  }

  return ret;
}

// Watchdog task: monitors all SSE clients for disconnection and sends keepalives
static void sse_watchdog_task(void *pvParameters) {
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
      if (fd < 0)
        continue;

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
        // EBADF (errno=9) is normal if the socket was closed by httpd
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
        int sent              = send(fd, keepalive, strlen(keepalive), MSG_DONTWAIT);
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

esp_err_t log_stream_init(void) {
  // Create mutex for client list
  clients_mutex = xSemaphoreCreateMutex();
  if (!clients_mutex) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_ERR_NO_MEM;
  }
  log_file_mutex = xSemaphoreCreateMutex();
  if (!log_file_mutex) {
    ESP_LOGE(TAG, "Failed to create log file mutex");
    vSemaphoreDelete(clients_mutex);
    clients_mutex = NULL;
    return ESP_ERR_NO_MEM;
  }

  // Initialize client array
  for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
    sse_clients[i] = -1;
  }
  log_file_set_current_path(1);

  // Create watchdog task for SSE client monitoring
  BaseType_t ret = xTaskCreate(sse_watchdog_task,
                               "sse_watchdog",
                               3072, // Stack size
                               NULL,
                               5, // Priority
                               &watchdog_task_handle);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create SSE watchdog task");
    vSemaphoreDelete(clients_mutex);
    vSemaphoreDelete(log_file_mutex);
    clients_mutex  = NULL;
    log_file_mutex = NULL;
    return ESP_FAIL;
  }

  log_file_msg_buffer = xMessageBufferCreate(LOG_FILE_MESSAGE_BUFFER_SIZE);
  if (!log_file_msg_buffer) {
    ESP_LOGE(TAG, "Failed to create log file message buffer");
    vSemaphoreDelete(clients_mutex);
    vSemaphoreDelete(log_file_mutex);
    clients_mutex  = NULL;
    log_file_mutex = NULL;
    return ESP_ERR_NO_MEM;
  }

  ret = xTaskCreate(log_file_writer_task,
                    "log_file_writer",
                    4096,
                    NULL,
                    4,
                    &log_file_task_handle);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create log file writer task");
    vSemaphoreDelete(clients_mutex);
    vSemaphoreDelete(log_file_mutex);
    vMessageBufferDelete(log_file_msg_buffer);
    clients_mutex       = NULL;
    log_file_mutex      = NULL;
    log_file_msg_buffer = NULL;
    return ESP_FAIL;
  }

  // Install log hook to intercept all ESP-IDF logs
  original_log_handler = esp_log_set_vprintf(custom_log_handler);

  ESP_LOGI(TAG, "Log streaming initialized (max %d clients, watchdog running)", MAX_SSE_CLIENTS);
  return ESP_OK;
}

esp_err_t log_stream_send_headers(int fd) {
  // Send HTTP headers
  const char *headers = "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/event-stream\r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: keep-alive\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "\r\n";

  int sent            = send(fd, headers, strlen(headers), 0);
  if (sent <= 0) {
    ESP_LOGE(TAG, "Failed to send SSE headers to fd=%d", fd);
    return ESP_FAIL;
  }

  // Send initial comment
  const char *init_msg = ": SSE stream connected\n\n";
  send(fd, init_msg, strlen(init_msg), 0);

  // Send initial status event
  const char *status_msg = "data: {\"level\":\"I\",\"tag\":\"LOG_STREAM\",\"message\":\"Streaming started\"}\n\n";
  send(fd, status_msg, strlen(status_msg), 0);

  return ESP_OK;
}

esp_err_t log_stream_register_client(int fd) {
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

void log_stream_unregister_client(int fd) {
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

int log_stream_get_client_count(void) {
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

uint32_t log_stream_get_current_file_index(void) {
  return current_log_index;
}

uint32_t log_stream_get_file_rotation_max(void) {
  return LOG_ROTATE_BOOT_MAX;
}

// Escape a string for JSON (RFC 8259 compliant)
static void json_escape(const char *src, char *dst, size_t dst_size) {
  if (!src || !dst || dst_size < 2) {
    if (dst && dst_size > 0)
      dst[0] = '\0';
    return;
  }

  size_t j           = 0;
  const size_t max_j = dst_size - 7; // Reserve space for worst case: "\uXXXX" + '\0'

  for (size_t i = 0; src[i] && j < max_j; i++) {
    unsigned char c = (unsigned char)src[i];

    switch (c) {
    case '"':
      dst[j++] = '\\';
      dst[j++] = '"';
      break;
    case '\\':
      dst[j++] = '\\';
      dst[j++] = '\\';
      break;
    case '\b':
      dst[j++] = '\\';
      dst[j++] = 'b';
      break;
    case '\f':
      dst[j++] = '\\';
      dst[j++] = 'f';
      break;
    case '\n':
      dst[j++] = '\\';
      dst[j++] = 'n';
      break;
    case '\r':
      dst[j++] = '\\';
      dst[j++] = 'r';
      break;
    case '\t':
      dst[j++] = '\\';
      dst[j++] = 't';
      break;

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

void log_stream_send(const char *message, const char *level, const char *tag) {
  if (!message || !level || !tag) {
    return;
  }

  int client_count = log_stream_get_client_count();
  if (client_count == 0) {
    return; // No clients connected, skip
  }

  // Escape message for JSON
  char escaped_message[LOG_BUFFER_SIZE / 2];
  json_escape(message, escaped_message, sizeof(escaped_message));

  // Format SSE event
  // Format: data: {"level":"INFO","tag":"Main","message":"System started"}\n\n
  char buffer[LOG_BUFFER_SIZE];
  int len = snprintf(buffer, sizeof(buffer), "data: {\"level\":\"%s\",\"tag\":\"%s\",\"message\":\"%s\"}\n\n", level, tag, escaped_message);

  if (len <= 0 || len >= sizeof(buffer)) {
    return; // Buffer overflow or error
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

esp_err_t log_stream_enable_file_logging(bool enable) {
  if (!log_file_mutex) {
    return ESP_ERR_INVALID_STATE;
  }

  if (enable) {
    // Ensure logs directory exists (SPIFFS may be freshly formatted).
    mkdir("/spiffs/logs", 0755);

    xSemaphoreTake(log_file_mutex, portMAX_DELAY);
    uint32_t boot_count = log_boot_count_read();
    boot_count          = (boot_count % LOG_ROTATE_BOOT_MAX) + 1;
    log_boot_count_write(boot_count);
    log_file_set_current_path(boot_count);

    FILE *f = fopen(current_log_path, "w");
    if (f == NULL) {
      int err = errno;
      xSemaphoreGive(log_file_mutex);
      ESP_LOGE(TAG, "Failed to open log file %s (errno=%d)", current_log_path, err);
      return ESP_FAIL;
    }
    fclose(f);
    if (log_file_msg_buffer) {
      xMessageBufferReset(log_file_msg_buffer);
    }
    file_logging_enabled = true;
    xSemaphoreGive(log_file_mutex);
  } else {
    file_logging_enabled = false;
  }

  return ESP_OK;
}

bool log_stream_is_file_logging_enabled(void) {
  return file_logging_enabled;
}

esp_err_t log_stream_get_file_size(size_t *out_size) {
  if (!out_size) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!log_file_mutex) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(log_file_mutex, portMAX_DELAY);
  size_t size = log_file_get_size_no_log(current_log_path);
  bool exists = spiffs_file_exists(current_log_path);
  xSemaphoreGive(log_file_mutex);

  if (size == 0 && !exists) {
    *out_size = 0;
    return ESP_ERR_NOT_FOUND;
  }

  *out_size = size;
  return ESP_OK;
}

esp_err_t log_stream_clear_file_log(void) {
  if (!log_file_mutex) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(log_file_mutex, portMAX_DELAY);
  for (uint32_t i = 1; i <= LOG_ROTATE_BOOT_MAX; i++) {
    char path[64];
    snprintf(path, sizeof(path), "/spiffs/logs/logs.%u.txt", (unsigned)i);
    unlink(path);
  }
  unlink(LOG_BOOT_COUNT_PATH);
  log_file_set_current_path(1);
  if (log_file_msg_buffer) {
    xMessageBufferReset(log_file_msg_buffer);
  }
  xSemaphoreGive(log_file_mutex);

  return ESP_OK;
}
