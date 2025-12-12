#include "slcan_tcp_server.h"
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
#include <stdio.h>

static const char *TAG = "SLCAN_TCP";
#define NVS_NAMESPACE "can_servers"
#define NVS_KEY_SLCAN_AUTOSTART "slcan_auto"

// Configuration
#define SLCAN_TCP_PORT 3333  // Port standard pour SLCAN over TCP
#define MAX_SLCAN_CLIENTS 4
#define SLCAN_RX_BUFFER_SIZE 128
#define SLCAN_TX_BUFFER_SIZE 64

// SLCAN Protocol Format (LAWICEL)
// Le protocole SLCAN utilise un format ASCII simple :
//
// Standard CAN (11-bit ID):
// t<ID><DLC><DATA>\r   - Transmission frame
// r<ID><DLC>\r         - RTR frame
//
// Extended CAN (29-bit ID):
// T<ID><DLC><DATA>\r   - Transmission frame
// R<ID><DLC>\r         - RTR frame
//
// Exemples:
// t1230812345678ABCDEF01\r  - Standard ID 0x123, DLC 8, data
// T000001230812345678ABCDEF01\r  - Extended ID 0x00000123, DLC 8, data
//
// Commands (not implemented in broadcast mode):
// S<baudrate>\r  - Set bitrate
// O\r            - Open channel
// C\r            - Close channel
// V\r            - Get version
// N\r            - Get serial number

// Client structure
typedef struct {
    int socket;
    TaskHandle_t task_handle;
    bool active;
    uint32_t frames_sent;
} slcan_client_t;

// Server state
static bool server_initialized = false;
static bool server_running = false;
static int listen_socket = -1;
static TaskHandle_t accept_task_handle = NULL;
static slcan_client_t clients[MAX_SLCAN_CLIENTS];
static SemaphoreHandle_t clients_mutex = NULL;

// ============================================================================
// SLCAN Protocol Frame Encoding
// ============================================================================

/**
 * @brief Encode CAN frame in SLCAN ASCII format
 *
 * SLCAN frame format (ASCII):
 * Standard (11-bit): t<ID:3hex><DLC:1hex><DATA:DLC*2hex>\r
 * Extended (29-bit): T<ID:8hex><DLC:1hex><DATA:DLC*2hex>\r
 *
 * @param bus Bus number (0 or 1) - encoded in extended ID bit 28
 * @param msg TWAI message
 * @param out_buffer Output buffer (must be at least 64 bytes)
 * @return Length of encoded string (including \r)
 */
static int encode_slcan_frame(int bus, const twai_message_t *msg, char *out_buffer)
{
    char *ptr = out_buffer;

    if (msg->extd) {
        // Extended frame: T<ID:8hex><DLC:1hex><DATA>\r
        *ptr++ = msg->rtr ? 'R' : 'T';

        // Extended ID (29-bit) - 8 hex digits
        // Encode bus number in bit 28 (not standard but useful for debugging)
        uint32_t id = msg->identifier;
        if (bus == 1) {
            id |= (1 << 28);  // Set bit 28 for CHASSIS bus
        }
        ptr += sprintf(ptr, "%08X", id);
    } else {
        // Standard frame: t<ID:3hex><DLC:1hex><DATA>\r
        *ptr++ = msg->rtr ? 'r' : 't';

        // Standard ID (11-bit) - 3 hex digits
        ptr += sprintf(ptr, "%03X", msg->identifier);
    }

    // DLC (1 hex digit)
    *ptr++ = "0123456789ABCDEF"[msg->data_length_code & 0x0F];

    // Data bytes (only if not RTR)
    if (!msg->rtr) {
        for (int i = 0; i < msg->data_length_code; i++) {
            ptr += sprintf(ptr, "%02X", msg->data[i]);
        }
    }

    // Carriage return terminator
    *ptr++ = '\r';
    *ptr = '\0';

    return (int)(ptr - out_buffer);
}

// ============================================================================
// Client Management
// ============================================================================

static slcan_client_t* find_free_client_slot(void)
{
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_SLCAN_CLIENTS; i++) {
        if (!clients[i].active) {
            xSemaphoreGive(clients_mutex);
            return &clients[i];
        }
    }
    xSemaphoreGive(clients_mutex);
    return NULL;
}

static void mark_client_inactive(slcan_client_t *client)
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
 * @brief Process received data from SLCAN client
 *
 * Note: SLCAN protocol is primarily one-way (ESP32 -> client) for monitoring.
 * Commands like 'O' (open), 'C' (close), 'S' (set speed) could be implemented
 * but are not needed for basic CAN streaming.
 */
static void process_slcan_rx_data(slcan_client_t *client, const uint8_t *data, int len)
{
    // SLCAN commands (optional, not implemented in monitoring mode):
    // O\r - Open channel (respond with \r or error)
    // C\r - Close channel
    // S0-S8\r - Set bitrate
    // V\r - Get version
    // N\r - Get serial number

    // For now, just log and ignore (pure monitoring mode)
    ESP_LOGD(TAG, "Received %d bytes from client (slot %d) - ignoring",
             len, (int)(client - clients));

    // Optional: Send OK response to 'O' command
    for (int i = 0; i < len; i++) {
        if (data[i] == 'O' && (i + 1 < len) && data[i + 1] == '\r') {
            // Respond with \r (OK)
            send(client->socket, "\r", 1, 0);
            ESP_LOGD(TAG, "Responded to 'O' (Open) command");
        }
    }
}

static void slcan_client_task(void *arg)
{
    slcan_client_t *client = (slcan_client_t *)arg;
    uint8_t rx_buf[SLCAN_RX_BUFFER_SIZE];

    ESP_LOGI(TAG, "Client connected (slot %d, socket %d)",
             (int)(client - clients), client->socket);

    // SLCAN protocol: Server streams CAN frames to client
    // Client may send commands (optional)

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
        process_slcan_rx_data(client, rx_buf, len);
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

static void slcan_accept_task(void *arg)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    ESP_LOGI(TAG, "Accept task started, listening on port %d", SLCAN_TCP_PORT);

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
        slcan_client_t *client = find_free_client_slot();
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
            slcan_client_task,
            "slcan_client",
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

esp_err_t slcan_tcp_server_init(void)
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
    for (int i = 0; i < MAX_SLCAN_CLIENTS; i++) {
        clients[i].socket = -1;
        clients[i].active = false;
    }

    server_initialized = true;
    ESP_LOGI(TAG, "SLCAN TCP server initialized");
    return ESP_OK;
}

esp_err_t slcan_tcp_server_start(void)
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
    server_addr.sin_port = htons(SLCAN_TCP_PORT);

    int ret = bind(listen_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        ESP_LOGE(TAG, "bind() failed: %d (%s)", errno, strerror(errno));
        close(listen_socket);
        listen_socket = -1;
        return ESP_FAIL;
    }

    // Listen for connections
    ret = listen(listen_socket, MAX_SLCAN_CLIENTS);
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
        slcan_accept_task,
        "slcan_accept",
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

    ESP_LOGI(TAG, "SLCAN TCP server started on port %d", SLCAN_TCP_PORT);
    return ESP_OK;
}

void slcan_tcp_server_stop(void)
{
    if (!server_running) {
        return;
    }

    ESP_LOGI(TAG, "Stopping SLCAN TCP server...");

    // Stop accepting new connections
    server_running = false;

    // Close listening socket
    if (listen_socket >= 0) {
        close(listen_socket);
        listen_socket = -1;
    }

    // Close all client connections
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_SLCAN_CLIENTS; i++) {
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

    ESP_LOGI(TAG, "SLCAN TCP server stopped");
}

bool slcan_tcp_server_is_running(void)
{
    return server_running;
}

int slcan_tcp_server_get_client_count(void)
{
    int count = 0;

    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_SLCAN_CLIENTS; i++) {
        if (clients[i].active) {
            count++;
        }
    }
    xSemaphoreGive(clients_mutex);

    return count;
}

void slcan_tcp_broadcast_can_frame(int bus, const twai_message_t *msg)
{
    if (!server_running) {
        return;
    }

    // Encode frame in SLCAN format (ASCII)
    char frame[SLCAN_TX_BUFFER_SIZE];
    int frame_len = encode_slcan_frame(bus, msg, frame);

    // Broadcast to all active clients
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_SLCAN_CLIENTS; i++) {
        if (clients[i].active && clients[i].socket >= 0) {
            int sent = send(clients[i].socket, frame, frame_len, 0);
            if (sent == frame_len) {
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

esp_err_t slcan_tcp_server_set_autostart(bool autostart)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for autostart: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t value = autostart ? 1 : 0;
    err = nvs_set_u8(nvs_handle, NVS_KEY_SLCAN_AUTOSTART, value);
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

bool slcan_tcp_server_get_autostart(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        // Default to false if NVS not accessible
        return false;
    }

    uint8_t value = 0;
    err = nvs_get_u8(nvs_handle, NVS_KEY_SLCAN_AUTOSTART, &value);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        return value != 0;
    }

    // Default to false if key not found
    return false;
}
