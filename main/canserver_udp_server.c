#include "canserver_udp_server.h"

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

static const char *TAG = "CANSERVER_UDP";

// Configuration
#define CANSERVER_UDP_PORT 1338 // Port standard pour CANServer (Panda)
#define MAX_CANSERVER_CLIENTS 4
#define CANSERVER_RX_BUFFER_SIZE 128
#define CANSERVER_KEEPALIVE_TIMEOUT_MS 5000 // 5 seconds timeout
#define MAX_FRAMES_PER_PACKET 120           // Up to ~1900 bytes per UDP packet

// Panda Protocol Format
// Le protocole Panda utilise UDP avec un format binaire simple :
// - Client envoie "ehllo"/"hello" pour démarrer le streaming
// - Serveur envoie des paquets UDP contenant des frames (16 bytes chacune)
// - Client doit renvoyer un ping toutes les 5 secondes pour maintenir la connexion
//
// Format d'une frame (16 bytes total) (CANServer / Panda UDP binaire) :
// [0-3]   : f1 (uint32_t LE) - (CAN ID << 21) | (extended << 31)
// [4-7]   : f2 (uint32_t LE) - (length & 0x0F) | (busId << 4)
// [8-15]  : data[8] (uint8_t[8]) - CAN payload (zero-padded if DLC < 8)

#define PANDA_MSG_SIZE 16

// Client structure
typedef struct {
  struct sockaddr_in addr;
  bool active;
  uint64_t last_ping_time; // Timestamp of last ping (microseconds)
  uint32_t frames_sent;
} canserver_client_t;

// Frame buffer for batching
typedef struct {
  uint8_t data[PANDA_MSG_SIZE * MAX_FRAMES_PER_PACKET];
  int count;
} frame_buffer_t;

// Server state
static bool server_initialized     = false;
static bool server_running         = false;
static int udp_socket              = -1;
static TaskHandle_t rx_task_handle = NULL;
static TaskHandle_t tx_task_handle = NULL;
static canserver_client_t clients[MAX_CANSERVER_CLIENTS];
static SemaphoreHandle_t clients_mutex = NULL;
static frame_buffer_t frame_buffer;
static SemaphoreHandle_t buffer_mutex = NULL;

// ============================================================================
// Panda Protocol Frame Encoding
// ============================================================================

/**
 * @brief Encode CAN frame in Panda binary format (16 bytes)
 *
 * Panda frame format (16 bytes) attendu par Scan My Tesla / CANServer:
 * [0-3]   : f1 (uint32_t LE) - (CAN ID << 21) | extended bit (31)
 * [4-7]   : f2 (uint32_t LE) - (length & 0x0F) | (busId << 4)
 * [8-15]  : data[8] (uint8_t[8]) - CAN payload (zero-padded if DLC < 8)
 *
 * @param bus Bus number (0 or 1)
 * @param msg TWAI message
 * @param out_frame Output buffer (must be at least 16 bytes)
 */
static void encode_panda_frame(int bus, const twai_message_t *msg, uint8_t *out_frame) {
  // Clear output buffer
  memset(out_frame, 0, PANDA_MSG_SIZE);

  // f1 field (bytes 0-3): CAN ID << 21 (+ extended flag on bit 31)
  uint32_t f1 = (msg->identifier & 0x1FFFFFFF) << 21;
  if (msg->extd) {
    f1 |= (1u << 31);
  }
  out_frame[0] = (f1 >> 0) & 0xFF;
  out_frame[1] = (f1 >> 8) & 0xFF;
  out_frame[2] = (f1 >> 16) & 0xFF;
  out_frame[3] = (f1 >> 24) & 0xFF;

  // f2 field (bytes 4-7): DLC + bus
  uint32_t f2  = (msg->data_length_code & 0x0F) | ((uint32_t)bus << 4);
  out_frame[4] = (f2 >> 0) & 0xFF;
  out_frame[5] = (f2 >> 8) & 0xFF;
  out_frame[6] = (f2 >> 16) & 0xFF;
  out_frame[7] = (f2 >> 24) & 0xFF;

  // Data field (bytes 8-15): CAN payload (8 bytes, zero-padded)
  for (int i = 0; i < 8; i++) {
    if (i < msg->data_length_code) {
      out_frame[8 + i] = msg->data[i];
    } else {
      out_frame[8 + i] = 0; // Zero padding
    }
  }
}

// ============================================================================
// Client Management
// ============================================================================

static canserver_client_t *find_client_by_addr(const struct sockaddr_in *addr) {
  for (int i = 0; i < MAX_CANSERVER_CLIENTS; i++) {
    if (clients[i].active && clients[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr && clients[i].addr.sin_port == addr->sin_port) {
      return &clients[i];
    }
  }
  return NULL;
}

static canserver_client_t *find_free_client_slot(void) {
  for (int i = 0; i < MAX_CANSERVER_CLIENTS; i++) {
    if (!clients[i].active) {
      return &clients[i];
    }
  }
  return NULL;
}

static void remove_stale_clients(void) {
  uint64_t now        = esp_timer_get_time();
  uint64_t timeout_us = CANSERVER_KEEPALIVE_TIMEOUT_MS * 1000ULL;

  xSemaphoreTake(clients_mutex, portMAX_DELAY);
  for (int i = 0; i < MAX_CANSERVER_CLIENTS; i++) {
    if (clients[i].active) {
      if ((now - clients[i].last_ping_time) > timeout_us) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clients[i].addr.sin_addr, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "Client %s:%d timed out (no ping for >5s), removing", ip_str, ntohs(clients[i].addr.sin_port));
        clients[i].active = false;
      }
    }
  }
  xSemaphoreGive(clients_mutex);
}

// ============================================================================
// RX Task - Handle incoming UDP packets (pings/hello)
// ============================================================================

static void canserver_rx_task(void *arg) {
  uint8_t rx_buf[CANSERVER_RX_BUFFER_SIZE];
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);

  ESP_LOGI(TAG, "RX task started, listening on UDP port %d", CANSERVER_UDP_PORT);

  while (server_running) {
    // Receive UDP packet
    int len = recvfrom(udp_socket, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&client_addr, &addr_len);

    if (len < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      ESP_LOGW(TAG, "recvfrom() error: %d (%s)", errno, strerror(errno));
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (len == 0) {
      continue;
    }

    // Null-terminate for string comparison
    if (len < CANSERVER_RX_BUFFER_SIZE) {
      rx_buf[len] = '\0';
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

    xSemaphoreTake(clients_mutex, portMAX_DELAY);

    // Find existing client or create new one
    canserver_client_t *client = find_client_by_addr(&client_addr);

    if (!client) {
      // New client - handshake is normally "ehllo" (SMT), keep "hello" for compatibility
      // Accept with or without trailing \r\n
      bool is_hello = (len >= 5 && (memcmp(rx_buf, "ehllo", 5) == 0 || memcmp(rx_buf, "hello", 5) == 0));

      if (is_hello) {
        client = find_free_client_slot();
        if (client) {
          client->addr           = client_addr;
          client->active         = true;
          client->last_ping_time = esp_timer_get_time();
          client->frames_sent    = 0;
          ESP_LOGI(TAG, "New client registered: %s:%d (received %d bytes)", client_ip, ntohs(client_addr.sin_port), len);
        } else {
          ESP_LOGW(TAG, "Max clients reached, ignoring hello from %s:%d", client_ip, ntohs(client_addr.sin_port));
        }
      } else {
        ESP_LOGW(TAG,
                 "Ignoring packet from unknown client %s:%d (not a hello, len=%d): [%02x %02x %02x %02x %02x]",
                 client_ip,
                 ntohs(client_addr.sin_port),
                 len,
                 rx_buf[0],
                 rx_buf[1],
                 rx_buf[2],
                 rx_buf[3],
                 rx_buf[4]);
      }
    } else {
      // Existing client - update ping time
      client->last_ping_time = esp_timer_get_time();
    }

    xSemaphoreGive(clients_mutex);
  }

  ESP_LOGI(TAG, "RX task stopped");
  vTaskDelete(NULL);
}

// ============================================================================
// TX Task - Send buffered frames to clients
// ============================================================================

static void canserver_tx_task(void *arg) {
  ESP_LOGI(TAG, "TX task started");

  while (server_running) {
    // Wait a bit to accumulate frames (keep small to limit drops)
    vTaskDelay(pdMS_TO_TICKS(10)); // 10ms batching interval

    // Remove stale clients
    remove_stale_clients();

    // Get buffered frames
    xSemaphoreTake(buffer_mutex, portMAX_DELAY);
    int frame_count = frame_buffer.count;

    if (frame_count == 0) {
      xSemaphoreGive(buffer_mutex);
      continue;
    }

    // Copy buffer for sending
    uint8_t send_buf[PANDA_MSG_SIZE * MAX_FRAMES_PER_PACKET];
    int send_len = frame_count * PANDA_MSG_SIZE;
    memcpy(send_buf, frame_buffer.data, send_len);

    // Clear buffer
    frame_buffer.count = 0;
    xSemaphoreGive(buffer_mutex);

    // Send to all active clients
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    int sent_count   = 0;
    int active_count = 0;

    for (int i = 0; i < MAX_CANSERVER_CLIENTS; i++) {
      if (clients[i].active) {
        active_count++;

        // Retry mechanism for ENOMEM (errno 12 - buffer full)
        int retry_count       = 0;
        const int max_retries = 3;
        int sent              = -1;

        while (retry_count < max_retries) {
          sent = sendto(udp_socket, send_buf, send_len, 0, (struct sockaddr *)&clients[i].addr, sizeof(clients[i].addr));

          if (sent == send_len) {
            clients[i].frames_sent += frame_count;
            sent_count++;

            // Log every 50 frames to confirm data is flowing
            if (clients[i].frames_sent % 50 < frame_count) {
              char ip_str[INET_ADDRSTRLEN];
              inet_ntop(AF_INET, &clients[i].addr.sin_addr, ip_str, sizeof(ip_str));
              ESP_LOGI(TAG, "Sent %u frames to %s:%d (batch: %d, size: %d bytes)", clients[i].frames_sent, ip_str, ntohs(clients[i].addr.sin_port), frame_count, send_len);
            }
            break;
          } else if (sent < 0 && errno == ENOMEM) {
            // Buffer full, wait and retry
            retry_count++;
            if (retry_count < max_retries) {
              vTaskDelay(pdMS_TO_TICKS(20)); // Wait 20ms before retry
            }
          } else if (sent < 0) {
            // Other error (or ENOMEM after max retries), don't retry
            if (errno == ENOMEM) {
              ESP_LOGW(TAG, "sendto() to client %d failed after %d retries: ENOMEM (buffer full)", i, max_retries);
            } else {
              ESP_LOGW(TAG, "sendto() to client %d failed: %d (%s)", i, errno, strerror(errno));
            }
            break;
          }
        }

        // Small delay between sends to avoid overwhelming the network
        vTaskDelay(pdMS_TO_TICKS(5));
      }
    }

    xSemaphoreGive(clients_mutex);

    // if (active_count > 0 && sent_count == 0) {
    //     ESP_LOGW(TAG, "Failed to send %d frames to %d active client(s)",
    //              frame_count, active_count);
    // }
  }

  ESP_LOGI(TAG, "TX task stopped");
  vTaskDelete(NULL);
}

// ============================================================================
// Public API Implementation
// ============================================================================

esp_err_t canserver_udp_server_init(void) {
  if (server_initialized) {
    return ESP_OK;
  }

  // Create mutexes
  clients_mutex = xSemaphoreCreateMutex();
  buffer_mutex  = xSemaphoreCreateMutex();
  if (!clients_mutex || !buffer_mutex) {
    ESP_LOGE(TAG, "Failed to create mutexes");
    return ESP_ERR_NO_MEM;
  }

  // Initialize client array
  memset(clients, 0, sizeof(clients));
  for (int i = 0; i < MAX_CANSERVER_CLIENTS; i++) {
    clients[i].active = false;
  }

  // Initialize frame buffer
  memset(&frame_buffer, 0, sizeof(frame_buffer));

  server_initialized = true;
  ESP_LOGI(TAG, "CANServer UDP server initialized");
  return ESP_OK;
}

esp_err_t canserver_udp_server_start(void) {
  if (!server_initialized) {
    ESP_LOGE(TAG, "Server not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (server_running) {
    ESP_LOGW(TAG, "Server already running");
    return ESP_ERR_INVALID_STATE;
  }

  // Create UDP socket
  udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udp_socket < 0) {
    ESP_LOGE(TAG, "Failed to create UDP socket: %d (%s)", errno, strerror(errno));
    return ESP_FAIL;
  }

  // Set socket to non-blocking
  int flags = fcntl(udp_socket, F_GETFL, 0);
  fcntl(udp_socket, F_SETFL, flags | O_NONBLOCK);

  // Set socket options
  int opt = 1;
  setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // Bind to port
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family      = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port        = htons(CANSERVER_UDP_PORT);

  int ret                     = bind(udp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (ret < 0) {
    ESP_LOGE(TAG, "bind() failed: %d (%s)", errno, strerror(errno));
    close(udp_socket);
    udp_socket = -1;
    return ESP_FAIL;
  }

  // Create RX task (handle incoming pings/hello)
  server_running      = true;
  BaseType_t task_ret = xTaskCreate(canserver_rx_task, "canserver_rx", 4096, NULL, 11, &rx_task_handle);

  if (task_ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create RX task");
    close(udp_socket);
    udp_socket     = -1;
    server_running = false;
    return ESP_FAIL;
  }

  // Create TX task (send buffered frames)
  task_ret = xTaskCreate(canserver_tx_task, "canserver_tx", 4096, NULL, 10, &tx_task_handle);

  if (task_ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create TX task");
    close(udp_socket);
    udp_socket     = -1;
    server_running = false;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "CANServer UDP server started on port %d", CANSERVER_UDP_PORT);
  return ESP_OK;
}

void canserver_udp_server_stop(void) {
  if (!server_running) {
    return;
  }

  ESP_LOGI(TAG, "Stopping CANServer UDP server...");

  // Stop tasks
  server_running = false;

  // Close UDP socket
  if (udp_socket >= 0) {
    close(udp_socket);
    udp_socket = -1;
  }

  // Clear all clients
  xSemaphoreTake(clients_mutex, portMAX_DELAY);
  for (int i = 0; i < MAX_CANSERVER_CLIENTS; i++) {
    clients[i].active = false;
  }
  xSemaphoreGive(clients_mutex);

  // Wait for tasks to finish
  vTaskDelay(pdMS_TO_TICKS(500));

  ESP_LOGI(TAG, "CANServer UDP server stopped");
}

bool canserver_udp_server_is_running(void) {
  return server_running;
}

int canserver_udp_server_get_client_count(void) {
  int count = 0;

  xSemaphoreTake(clients_mutex, portMAX_DELAY);
  for (int i = 0; i < MAX_CANSERVER_CLIENTS; i++) {
    if (clients[i].active) {
      count++;
    }
  }
  xSemaphoreGive(clients_mutex);

  return count;
}

void canserver_udp_broadcast_can_frame(int bus, const twai_message_t *msg) {
  if (!server_running) {
    return;
  }

  static uint32_t frame_rx_count = 0;
  frame_rx_count++;

  // Log every 100 frames to confirm CAN data is being received
  if (frame_rx_count % 100 == 0) {
    ESP_LOGI(TAG, "Received %u CAN frames for broadcast (bus %d, ID 0x%03X)", frame_rx_count, bus, msg->identifier);
  }

  // Encode frame
  uint8_t frame[PANDA_MSG_SIZE];
  encode_panda_frame(bus, msg, frame);

  // Add to buffer
  xSemaphoreTake(buffer_mutex, portMAX_DELAY);

  if (frame_buffer.count < MAX_FRAMES_PER_PACKET) {
    memcpy(&frame_buffer.data[frame_buffer.count * PANDA_MSG_SIZE], frame, PANDA_MSG_SIZE);
    frame_buffer.count++;
  }
  // Silently drop if buffer full (will be sent soon by TX task)

  xSemaphoreGive(buffer_mutex);
}

// ============================================================================
// Autostart Management (delegated to can_servers_config)
// ============================================================================

esp_err_t canserver_udp_server_set_autostart(bool autostart) {
  return can_servers_config_set_autostart(CAN_SERVER_CANSERVER, autostart);
}

bool canserver_udp_server_get_autostart(void) {
  return can_servers_config_get_autostart(CAN_SERVER_CANSERVER);
}
