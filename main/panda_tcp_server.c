#include "panda_tcp_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <sys/param.h>
#include <fcntl.h>
#include <errno.h>

static const char *TAG = "PANDA_TCP";
#define NVS_NAMESPACE "can_servers"
#define NVS_KEY_PANDA_AUTOSTART "panda_auto"

// Configuration
#define PANDA_TCP_PORT 1338  // Port standard pour Panda/cabana
#define MAX_PANDA_CLIENTS 4
#define PANDA_RX_BUFFER_SIZE 128
#define PANDA_TX_BUFFER_SIZE 2048

// Panda Protocol Format
// Le protocole Panda utilise un format binaire simple :
// - Chaque message CAN est encodé sur 16 bytes (format CanData)
// - Structure : address(4) + busTime(2) + data(8) + src(1) + flags(1)
//
// Format détaillé (16 bytes total) :
// [0-3]   : address (uint32_t, little-endian) - CAN ID + flags
// [4-5]   : busTime (uint16_t, little-endian) - timestamp
// [6-13]  : data[8] (uint8_t[8]) - payload
// [14]    : src (uint8_t) - bus source (0-3)
// [15]    : flags (uint8_t) - reserved/unused
//
// Address field encoding (32 bits):
// - Bit 31: Reserved
// - Bit 30: Extended ID flag (1 = extended 29-bit, 0 = standard 11-bit)
// - Bits 29-0: CAN ID value

#define PANDA_MSG_SIZE 16

// Client structure
typedef struct {
    int socket;
    TaskHandle_t task_handle;
    bool active;
    uint32_t frames_sent;
} panda_client_t;

// Server state
static bool server_initialized = false;
static bool server_running = false;
static int listen_socket = -1;
static TaskHandle_t accept_task_handle = NULL;
static panda_client_t clients[MAX_PANDA_CLIENTS];
static SemaphoreHandle_t clients_mutex = NULL;

// ============================================================================
// Panda Protocol Frame Encoding
// ============================================================================

/**
 * @brief Encode CAN frame in Panda binary format (16 bytes)
 *
 * Panda frame format (16 bytes):
 * [0-3]   : address (uint32_t LE) - CAN ID + extended flag
 * [4-5]   : busTime (uint16_t LE) - timestamp
 * [6-13]  : data[8] (uint8_t[8]) - payload (zero-padded if DLC < 8)
 * [14]    : src (uint8_t) - bus number
 * [15]    : flags (uint8_t) - reserved (0)
 *
 * @param bus Bus number (0 or 1)
 * @param msg TWAI message
 * @param out_frame Output buffer (must be at least 16 bytes)
 */
static void encode_panda_frame(int bus, const twai_message_t *msg, uint8_t *out_frame)
{
    // Clear output buffer
    memset(out_frame, 0, PANDA_MSG_SIZE);

    // Address field (bytes 0-3): CAN ID with extended flag
    uint32_t address = msg->identifier;
    if (msg->extd) {
        address |= (1 << 30);  // Bit 30 = extended ID flag
    }
    out_frame[0] = (address >> 0) & 0xFF;
    out_frame[1] = (address >> 8) & 0xFF;
    out_frame[2] = (address >> 16) & 0xFF;
    out_frame[3] = (address >> 24) & 0xFF;

    // BusTime field (bytes 4-5): 16-bit timestamp (milliseconds, wraps)
    // Note: Panda uses a 16-bit counter, we'll use the lower 16 bits of esp_timer
    uint32_t timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint16_t busTime = (uint16_t)(timestamp_ms & 0xFFFF);
    out_frame[4] = (busTime >> 0) & 0xFF;
    out_frame[5] = (busTime >> 8) & 0xFF;

    // Data field (bytes 6-13): CAN payload (8 bytes, zero-padded)
    for (int i = 0; i < 8; i++) {
        if (i < msg->data_length_code) {
            out_frame[6 + i] = msg->data[i];
        } else {
            out_frame[6 + i] = 0;  // Zero padding
        }
    }

    // Source field (byte 14): Bus number
    out_frame[14] = (uint8_t)bus;

    // Flags field (byte 15): Reserved (unused)
    out_frame[15] = 0;
}

// ============================================================================
// Client Management
// ============================================================================

static panda_client_t* find_free_client_slot(void)
{
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_PANDA_CLIENTS; i++) {
        if (!clients[i].active) {
            xSemaphoreGive(clients_mutex);
            return &clients[i];
        }
    }
    xSemaphoreGive(clients_mutex);
    return NULL;
}

static void mark_client_inactive(panda_client_t *client)
{
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    client->active = false;
    client->socket = -1;
    client->task_handle = NULL;
    xSemaphoreGive(clients_mutex);
}

// ============================================================================
// Client Handler Task
// ============================================================================

/**
 * @brief Process received data from Panda client
 *
 * Note: Panda protocol is primarily one-way (ESP32 -> client).
 * Client typically doesn't send commands, but we handle it gracefully.
 * If we receive data, we just log it and discard.
 */
static void process_panda_rx_data(panda_client_t *client, const uint8_t *data, int len)
{
    // Panda protocol doesn't define client->server commands for basic streaming.
    // If we receive data, just log it (could be keepalive or tool-specific commands)
    ESP_LOGD(TAG, "Received %d bytes from client (slot %d) - ignoring",
             len, (int)(client - clients));
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);
}

static void panda_client_task(void *arg)
{
    panda_client_t *client = (panda_client_t *)arg;
    uint8_t rx_buf[PANDA_RX_BUFFER_SIZE];

    ESP_LOGI(TAG, "Client connected (slot %d, socket %d)",
             (int)(client - clients), client->socket);

    // Panda protocol: Server streams CAN frames to client
    // Client typically doesn't send commands (unlike GVRET)

    while (1) {
        // Check for incoming data (non-blocking)
        int len = recv(client->socket, rx_buf, sizeof(rx_buf), 0);

        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                vTaskDelay(pdMS_TO_TICKS(100));  // No data, sleep
                continue;
            }
            ESP_LOGW(TAG, "recv() error: %d (%s)", errno, strerror(errno));
            break;
        } else if (len == 0) {
            ESP_LOGI(TAG, "Client disconnected gracefully");
            break;
        }

        // Process received data (if any)
        process_panda_rx_data(client, rx_buf, len);
    }

    // Cleanup
    ESP_LOGI(TAG, "Client disconnected (slot %d, %u frames sent)",
             (int)(client - clients), client->frames_sent);

    close(client->socket);
    mark_client_inactive(client);
    vTaskDelete(NULL);
}

// ============================================================================
// Accept Task
// ============================================================================

static void panda_accept_task(void *arg)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    ESP_LOGI(TAG, "Accept task started, listening on port %d", PANDA_TCP_PORT);

    while (server_running) {
        int client_socket = accept(listen_socket, (struct sockaddr *)&client_addr, &addr_len);

        if (client_socket < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            ESP_LOGW(TAG, "accept() error: %d (%s)", errno, strerror(errno));
            break;
        }

        // Log client connection
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        ESP_LOGI(TAG, "New connection from %s:%d", client_ip, ntohs(client_addr.sin_port));

        // Find free client slot
        panda_client_t *client = find_free_client_slot();
        if (!client) {
            ESP_LOGW(TAG, "Max clients reached, rejecting connection from %s", client_ip);
            close(client_socket);
            continue;
        }

        // Initialize client
        client->socket = client_socket;
        client->active = true;
        client->frames_sent = 0;

        // Set socket to non-blocking
        int flags = fcntl(client_socket, F_GETFL, 0);
        fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);

        // Create client task
        BaseType_t ret = xTaskCreate(
            panda_client_task,
            "panda_client",
            4096,
            client,
            10,
            &client->task_handle
        );

        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create client task");
            close(client_socket);
            mark_client_inactive(client);
        }
    }

    ESP_LOGI(TAG, "Accept task stopped");
    vTaskDelete(NULL);
}

// ============================================================================
// Public API Implementation
// ============================================================================

esp_err_t panda_tcp_server_init(void)
{
    if (server_initialized) {
        return ESP_OK;
    }

    // Create mutex for client array
    clients_mutex = xSemaphoreCreateMutex();
    if (!clients_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize client array
    memset(clients, 0, sizeof(clients));
    for (int i = 0; i < MAX_PANDA_CLIENTS; i++) {
        clients[i].socket = -1;
        clients[i].active = false;
    }

    server_initialized = true;
    ESP_LOGI(TAG, "Panda TCP server initialized");
    return ESP_OK;
}

esp_err_t panda_tcp_server_start(void)
{
    if (!server_initialized) {
        ESP_LOGE(TAG, "Server not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (server_running) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_ERR_INVALID_STATE;
    }

    // Create listening socket
    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    }

    // Set socket options
    int opt = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind to port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PANDA_TCP_PORT);

    int ret = bind(listen_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        ESP_LOGE(TAG, "bind() failed: %d (%s)", errno, strerror(errno));
        close(listen_socket);
        listen_socket = -1;
        return ESP_FAIL;
    }

    // Listen for connections
    ret = listen(listen_socket, MAX_PANDA_CLIENTS);
    if (ret < 0) {
        ESP_LOGE(TAG, "listen() failed: %d (%s)", errno, strerror(errno));
        close(listen_socket);
        listen_socket = -1;
        return ESP_FAIL;
    }

    // Set socket to non-blocking
    int flags = fcntl(listen_socket, F_GETFL, 0);
    fcntl(listen_socket, F_SETFL, flags | O_NONBLOCK);

    // Create accept task
    server_running = true;
    BaseType_t task_ret = xTaskCreate(
        panda_accept_task,
        "panda_accept",
        4096,
        NULL,
        11,
        &accept_task_handle
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create accept task");
        close(listen_socket);
        listen_socket = -1;
        server_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Panda TCP server started on port %d", PANDA_TCP_PORT);
    return ESP_OK;
}

void panda_tcp_server_stop(void)
{
    if (!server_running) {
        return;
    }

    ESP_LOGI(TAG, "Stopping Panda TCP server...");

    // Stop accepting new connections
    server_running = false;

    // Close listening socket
    if (listen_socket >= 0) {
        close(listen_socket);
        listen_socket = -1;
    }

    // Close all client connections
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_PANDA_CLIENTS; i++) {
        if (clients[i].active && clients[i].socket >= 0) {
            close(clients[i].socket);
            clients[i].socket = -1;
            clients[i].active = false;
            // Client tasks will self-terminate
        }
    }
    xSemaphoreGive(clients_mutex);

    // Wait for accept task to finish
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "Panda TCP server stopped");
}

bool panda_tcp_server_is_running(void)
{
    return server_running;
}

int panda_tcp_server_get_client_count(void)
{
    int count = 0;

    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_PANDA_CLIENTS; i++) {
        if (clients[i].active) {
            count++;
        }
    }
    xSemaphoreGive(clients_mutex);

    return count;
}

void panda_tcp_broadcast_can_frame(int bus, const twai_message_t *msg)
{
    if (!server_running) {
        return;
    }

    // Encode frame in Panda format (16 bytes)
    uint8_t frame[PANDA_MSG_SIZE];
    encode_panda_frame(bus, msg, frame);

    // Broadcast to all active clients
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_PANDA_CLIENTS; i++) {
        if (clients[i].active && clients[i].socket >= 0) {
            int sent = send(clients[i].socket, frame, PANDA_MSG_SIZE, 0);
            if (sent == PANDA_MSG_SIZE) {
                clients[i].frames_sent++;
            } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGW(TAG, "send() to client %d failed: %d (%s)", i, errno, strerror(errno));
            }
        }
    }
    xSemaphoreGive(clients_mutex);
}

// ============================================================================
// Autostart Management (NVS)
// ============================================================================

esp_err_t panda_tcp_server_set_autostart(bool autostart)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for autostart: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t value = autostart ? 1 : 0;
    err = nvs_set_u8(nvs_handle, NVS_KEY_PANDA_AUTOSTART, value);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Autostart %s", autostart ? "enabled" : "disabled");
    } else {
        ESP_LOGE(TAG, "Failed to save autostart: %s", esp_err_to_name(err));
    }

    return err;
}

bool panda_tcp_server_get_autostart(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        // Default to false if NVS not accessible
        return false;
    }

    uint8_t value = 0;
    err = nvs_get_u8(nvs_handle, NVS_KEY_PANDA_AUTOSTART, &value);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        return value != 0;
    }

    // Default to false if key not found
    return false;
}
