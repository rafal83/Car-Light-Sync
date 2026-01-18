#include "gvret_tcp_server.h"

#include "can_servers_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/param.h>

static const char *TAG = "GVRET_TCP";

// Configuration
#define GVRET_TCP_PORT 23 // Standard GVRET network port (hardcoded in SavvyCAN)
#define MAX_GVRET_CLIENTS 4
#define GVRET_RX_BUFFER_SIZE 128
#define GVRET_TX_BUFFER_SIZE 2048

// GVRET Protocol Commands
#define GVRET_CMD_BUILD_CAN_FRAME 0x00
#define GVRET_CMD_TIME_SYNC 0x01
#define GVRET_CMD_GET_DIG_INPUTS 0x02
#define GVRET_CMD_GET_ANALOG_INPUTS 0x03
#define GVRET_CMD_SET_DIG_OUT 0x04
#define GVRET_CMD_SETUP_CANBUS 0x05
#define GVRET_CMD_GET_CANBUS_PARAMS 0x06
#define GVRET_CMD_GET_DEVICE_INFO 0x07
#define GVRET_CMD_SET_SINGLEWIRE 0x08
#define GVRET_CMD_KEEP_ALIVE 0x09
#define GVRET_CMD_SET_SYSTYPE 0x0A
#define GVRET_CMD_ECHO_CAN_FRAME 0x0B
#define GVRET_CMD_GET_NUM_BUSES 0x0C
#define GVRET_CMD_SETUP_EXT_BUSES 0x0E

// Frame markers
#define GVRET_FRAME_START 0xF1
#define GVRET_BINARY_MODE 0xE7 // Enter binary mode command

// Client structure
typedef struct {
  int socket;
  TaskHandle_t task_handle;
  bool active;
  uint32_t frames_sent;
} gvret_client_t;

// Server state
static bool server_initialized         = false;
static bool server_running             = false;
static int listen_socket               = -1;
static TaskHandle_t accept_task_handle = NULL;
static gvret_client_t clients[MAX_GVRET_CLIENTS];
static SemaphoreHandle_t clients_mutex = NULL;
static bool has_active_clients         = false; // Cached client status for fast check

// ============================================================================
// GVRET Protocol Frame Encoding
// ============================================================================

/**
 * @brief Encode CAN frame in GVRET binary format
 *
 * GVRET frame format (13-21 bytes):
 * [0xF1] [cmd=0x00] [timestamp:4] [id:4] [dlc+bus] [data:0-8]
 *
 * IRAM_ATTR: Called for every CAN frame received (~2000 fps), pure encoding logic
 *
 * @param bus Bus number (0 or 1)
 * @param msg TWAI message
 * @param out_frame Output buffer (must be at least 21 bytes)
 * @return Frame length in bytes
 */
static int IRAM_ATTR encode_gvret_frame(int bus, const twai_message_t *msg, uint8_t *out_frame) {
  int idx            = 0;

  // Frame start marker
  out_frame[idx++]   = GVRET_FRAME_START;

  // Command: BUILD_CAN_FRAME
  out_frame[idx++]   = GVRET_CMD_BUILD_CAN_FRAME;

  // Timestamp (microseconds) - get current time
  uint32_t timestamp = (uint32_t)(esp_timer_get_time() & 0xFFFFFFFF);
  out_frame[idx++]   = (timestamp >> 0) & 0xFF;
  out_frame[idx++]   = (timestamp >> 8) & 0xFF;
  out_frame[idx++]   = (timestamp >> 16) & 0xFF;
  out_frame[idx++]   = (timestamp >> 24) & 0xFF;

  // CAN ID (4 bytes)
  // Bit 31 = extended flag, bits 0-28 = ID
  uint32_t id        = msg->identifier;
  if (msg->extd) {
    id |= (1 << 31); // Set extended flag
  }
  out_frame[idx++] = (id >> 0) & 0xFF;
  out_frame[idx++] = (id >> 8) & 0xFF;
  out_frame[idx++] = (id >> 16) & 0xFF;
  out_frame[idx++] = (id >> 24) & 0xFF;

  // DLC + Bus (1 byte)
  // Low nibble = data length (0-8)
  // High nibble = bus number (0-15)
  uint8_t dlc_bus  = (msg->data_length_code & 0x0F) | ((bus & 0x0F) << 4);
  out_frame[idx++] = dlc_bus;

  // Data bytes (0-8)
  for (int i = 0; i < msg->data_length_code && i < 8; i++) {
    out_frame[idx++] = msg->data[i];
  }

  return idx;
}

// ============================================================================
// Client Management
// ============================================================================

static gvret_client_t *find_free_client_slot(void) {
  xSemaphoreTake(clients_mutex, portMAX_DELAY);
  for (int i = 0; i < MAX_GVRET_CLIENTS; i++) {
    if (!clients[i].active) {
      xSemaphoreGive(clients_mutex);
      return &clients[i];
    }
  }
  xSemaphoreGive(clients_mutex);
  return NULL;
}

static void mark_client_inactive(gvret_client_t *client) {
  xSemaphoreTake(clients_mutex, portMAX_DELAY);
  client->active      = false;
  client->socket      = -1;
  client->task_handle = NULL;

  // Update cached status
  has_active_clients  = false;
  for (int i = 0; i < MAX_GVRET_CLIENTS; i++) {
    if (clients[i].active) {
      has_active_clients = true;
      break;
    }
  }
  xSemaphoreGive(clients_mutex);
}

// ============================================================================
// Client Handler Task
// ============================================================================

static void send_device_info(int socket) {
  // Response format: [0xF1] [0x07] [build_num:2] [eeprom_ver:1] [file_type:1] [auto_start:1] [single_wire:1] [payload...]
  uint8_t response[20];
  int idx            = 0;

  response[idx++]    = GVRET_FRAME_START;         // 0xF1
  response[idx++]    = GVRET_CMD_GET_DEVICE_INFO; // 0x07

  // Build number (2 bytes, little-endian)
  uint16_t build_num = 618; // Version from original GVRET
  response[idx++]    = (build_num >> 0) & 0xFF;
  response[idx++]    = (build_num >> 8) & 0xFF;

  // EEPROM version (1 byte)
  response[idx++]    = 1;

  // File type (1 = GVRET)
  response[idx++]    = 1;

  // Auto-start logging (0 = no)
  response[idx++]    = 0;

  // Single wire mode (0 = no)
  response[idx++]    = 0;

  send(socket, response, idx, 0);
  ESP_LOGD(TAG, "Sent DEVICE_INFO response");
}

static void send_num_buses(int socket) {
  // Response format: [0xF1] [0x0C] [num_buses:1]
  uint8_t response[3];
  int idx         = 0;

  response[idx++] = GVRET_FRAME_START;       // 0xF1
  response[idx++] = GVRET_CMD_GET_NUM_BUSES; // 0x0C
  response[idx++] = 2;                       // We have 2 CAN buses (BODY + CHASSIS)

  send(socket, response, idx, 0);
  ESP_LOGD(TAG, "Sent NUM_BUSES response (2 buses)");
}

static void send_canbus_params(int socket) {
  // Response format: [0xF1] [0x06] [can0_config:1] [can0_speed:4] [can1_config:1] [can1_speed:4]
  // Based on GVRET protocol: https://github.com/collin80/SavvyCAN/blob/master/connections/gvretserial.cpp
  uint8_t response[12];
  int idx         = 0;

  response[idx++] = GVRET_FRAME_START;           // 0xF1
  response[idx++] = GVRET_CMD_GET_CANBUS_PARAMS; // 0x06

  // CAN0 (BODY) configuration byte
  // Lower 4 bits: enabled (1 = yes), Upper 4 bits: listenOnly (0 = no)
  response[idx++] = 0x01; // Enabled, not listen-only

  // CAN0 Speed (500kbps) - 4 bytes little-endian
  uint32_t speed  = 500000;
  response[idx++] = (speed >> 0) & 0xFF;
  response[idx++] = (speed >> 8) & 0xFF;
  response[idx++] = (speed >> 16) & 0xFF;
  response[idx++] = (speed >> 24) & 0xFF;

  // CAN1 (CHASSIS) configuration byte
  // Lower 4 bits: enabled, Bits 4-5: listenOnly, Bits 6-7: singleWireMode
  response[idx++] = 0x01; // Enabled, not listen-only, no single-wire

  // CAN1 Speed (500kbps) - 4 bytes little-endian
  response[idx++] = (speed >> 0) & 0xFF;
  response[idx++] = (speed >> 8) & 0xFF;
  response[idx++] = (speed >> 16) & 0xFF;
  response[idx++] = (speed >> 24) & 0xFF;

  send(socket, response, idx, 0);
  ESP_LOGD(TAG, "Sent CANBUS_PARAMS response (both buses)");
}

static void process_gvret_command(gvret_client_t *client, const uint8_t *data, int len) {
  if (len < 1) {
    return; // Too short
  }

  // Handle binary mode activation (0xE7 0xE7)
  if (data[0] == GVRET_BINARY_MODE) {
    ESP_LOGI(TAG, "Client sent BINARY_MODE command (already in binary mode)");
    return;
  }

  // Check for frame start marker
  if (data[0] != GVRET_FRAME_START) {
    ESP_LOGD(TAG, "Invalid frame start: 0x%02X (expected 0xF1)", data[0]);
    return;
  }

  if (len < 2) {
    return; // Need at least start + command
  }

  uint8_t cmd = data[1];

  switch (cmd) {
  case GVRET_CMD_GET_NUM_BUSES:
    ESP_LOGI(TAG, "Client requested NUM_BUSES");
    send_num_buses(client->socket);
    break;

  case GVRET_CMD_GET_DEVICE_INFO:
    ESP_LOGI(TAG, "Client requested DEVICE_INFO");
    send_device_info(client->socket);
    break;

  case GVRET_CMD_GET_CANBUS_PARAMS:
    ESP_LOGI(TAG, "Client requested CANBUS_PARAMS");
    send_canbus_params(client->socket);
    break;

  case GVRET_CMD_SETUP_CANBUS:
    ESP_LOGI(TAG, "Client sent SETUP_CANBUS (ignored - read-only mode)");
    break;

  case GVRET_CMD_TIME_SYNC:
    ESP_LOGD(TAG, "Client sent TIME_SYNC");
    break;

  case GVRET_CMD_KEEP_ALIVE:
    ESP_LOGD(TAG, "Client sent KEEP_ALIVE");
    // Send keep-alive response: [0xF1][0x09][0xDE][0xAD]
    {
      uint8_t response[4] = {GVRET_FRAME_START, GVRET_CMD_KEEP_ALIVE, 0xDE, 0xAD};
      send(client->socket, response, sizeof(response), 0);
      ESP_LOGD(TAG, "Sent KEEP_ALIVE response");
    }
    break;

  default:
    ESP_LOGD(TAG, "Unknown GVRET command: 0x%02X", cmd);
    break;
  }
}

static void gvret_client_task(void *arg) {
  gvret_client_t *client = (gvret_client_t *)arg;
  uint8_t rx_buf[GVRET_RX_BUFFER_SIZE];

  ESP_LOGI(TAG, "Client connected (slot %d, socket %d)", (int)(client - clients), client->socket);

  // Wait for SavvyCAN to send commands (0xE7 0xE7, then 0xF1 0x0C, 0xF1 0x06, etc.)
  // We don't send anything proactively - SavvyCAN initiates the handshake

  while (1) {
    // Receive data from TCP socket
    int len = recv(client->socket, rx_buf, sizeof(rx_buf), 0);

    if (len < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }
      ESP_LOGW(TAG, "recv() error: %d (%s)", errno, strerror(errno));
      break;
    } else if (len == 0) {
      ESP_LOGI(TAG, "Client disconnected gracefully");
      break;
    }

    // Process received GVRET commands from SavvyCAN
    ESP_LOGI(TAG, "Received %d bytes from client", len);

    // Dump received bytes for debugging
    ESP_LOG_BUFFER_HEX(TAG, rx_buf, len);

    // Parse buffer - multiple commands may be concatenated
    int offset = 0;
    while (offset < len) {
      // Handle 0xE7 (single byte command)
      if (rx_buf[offset] == GVRET_BINARY_MODE) {
        ESP_LOGI(TAG, "Client sent BINARY_MODE command");
        offset++;
        continue;
      }

      // Handle 0xF1 commands (at least 2 bytes: start + cmd)
      if (rx_buf[offset] == GVRET_FRAME_START && (offset + 1) < len) {
        // Process this command
        process_gvret_command(client, &rx_buf[offset], len - offset);
        offset += 2; // Minimum: start byte + command byte
        continue;
      }

      // Unknown byte, skip it
      ESP_LOGD(TAG, "Skipping unknown byte: 0x%02X", rx_buf[offset]);
      offset++;
    }
  }

  // Cleanup
  ESP_LOGI(TAG, "Client disconnected (slot %d, %u frames sent)", (int)(client - clients), client->frames_sent);

  close(client->socket);
  mark_client_inactive(client);
  vTaskDelete(NULL);
}

// ============================================================================
// Accept Task
// ============================================================================

static void gvret_accept_task(void *arg) {
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);

  ESP_LOGI(TAG, "Accept task started, listening on port %d", GVRET_TCP_PORT);

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
    gvret_client_t *client = find_free_client_slot();
    if (!client) {
      ESP_LOGW(TAG, "Max clients reached, rejecting connection from %s", client_ip);
      close(client_socket);
      continue;
    }

    // Initialize client
    client->socket      = client_socket;
    client->active      = true;
    client->frames_sent = 0;
    has_active_clients  = true; // Update cached status

    // Set socket to non-blocking
    int flags           = fcntl(client_socket, F_GETFL, 0);
    fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);

    // Create client task
    BaseType_t ret = xTaskCreate(gvret_client_task, "gvret_client", 4096, client, 10, &client->task_handle);

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

esp_err_t gvret_tcp_server_init(void) {
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
  for (int i = 0; i < MAX_GVRET_CLIENTS; i++) {
    clients[i].socket = -1;
    clients[i].active = false;
  }

  server_initialized = true;
  ESP_LOGI(TAG, "GVRET TCP server initialized");
  return ESP_OK;
}

esp_err_t gvret_tcp_server_start(void) {
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
  server_addr.sin_family      = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port        = htons(GVRET_TCP_PORT);

  int ret                     = bind(listen_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (ret < 0) {
    ESP_LOGE(TAG, "bind() failed: %d (%s)", errno, strerror(errno));
    close(listen_socket);
    listen_socket = -1;
    return ESP_FAIL;
  }

  // Listen for connections
  ret = listen(listen_socket, MAX_GVRET_CLIENTS);
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
  server_running      = true;
  BaseType_t task_ret = xTaskCreate(gvret_accept_task, "gvret_accept", 4096, NULL, 11, &accept_task_handle);

  if (task_ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create accept task");
    close(listen_socket);
    listen_socket  = -1;
    server_running = false;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "GVRET TCP server started on port %d", GVRET_TCP_PORT);
  return ESP_OK;
}

void gvret_tcp_server_stop(void) {
  if (!server_running) {
    return;
  }

  ESP_LOGI(TAG, "Stopping GVRET TCP server...");

  // Stop accepting new connections
  server_running = false;

  // Close listening socket
  if (listen_socket >= 0) {
    close(listen_socket);
    listen_socket = -1;
  }

  // Close all client connections
  xSemaphoreTake(clients_mutex, portMAX_DELAY);
  for (int i = 0; i < MAX_GVRET_CLIENTS; i++) {
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

  ESP_LOGI(TAG, "GVRET TCP server stopped");
}

bool gvret_tcp_server_is_running(void) {
  return server_running;
}

int gvret_tcp_server_get_client_count(void) {
  int count = 0;

  xSemaphoreTake(clients_mutex, portMAX_DELAY);
  for (int i = 0; i < MAX_GVRET_CLIENTS; i++) {
    if (clients[i].active) {
      count++;
    }
  }
  xSemaphoreGive(clients_mutex);

  return count;
}

void gvret_tcp_broadcast_can_frame(int bus, const twai_message_t *msg) {
  if (!server_running) {
    return;
  }

  // Fast check: if no clients, don't process frames at all (no mutex needed)
  if (!has_active_clients) {
    return; // No clients connected, skip processing
  }

  // Encode frame in GVRET format
  uint8_t frame[21];
  int frame_len = encode_gvret_frame(bus, msg, frame);

  // Broadcast to all active clients
  xSemaphoreTake(clients_mutex, portMAX_DELAY);
  for (int i = 0; i < MAX_GVRET_CLIENTS; i++) {
    if (clients[i].active && clients[i].socket >= 0) {
      int sent = send(clients[i].socket, frame, frame_len, 0);
      if (sent > 0) {
        clients[i].frames_sent++;
      } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGW(TAG, "send() to client %d failed: %d (%s)", i, errno, strerror(errno));
      }
    }
  }
  xSemaphoreGive(clients_mutex);
}

// ============================================================================
// Autostart Management (delegated to can_servers_config)
// ============================================================================

esp_err_t gvret_tcp_server_set_autostart(bool autostart) {
  return can_servers_config_set_autostart(CAN_SERVER_GVRET, autostart);
}

bool gvret_tcp_server_get_autostart(void) {
  return can_servers_config_get_autostart(CAN_SERVER_GVRET);
}
