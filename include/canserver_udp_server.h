#ifndef CANSERVER_UDP_SERVER_H
#define CANSERVER_UDP_SERVER_H

#include "esp_err.h"
#include "driver/twai.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize CANServer UDP server (protocol Panda)
 *
 * Must be called once at startup before start/stop operations
 *
 * @return ESP_OK on success
 */
esp_err_t canserver_udp_server_init(void);

/**
 * @brief Start CANServer UDP server on port 1338
 *
 * Starts listening for CANServer/ScanMyTesla (Panda protocol) connections.
 * Can be called multiple times (idempotent).
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already running
 */
esp_err_t canserver_udp_server_start(void);

/**
 * @brief Stop CANServer UDP server
 *
 * Closes all client connections and stops listening.
 * Can be called multiple times (idempotent).
 */
void canserver_udp_server_stop(void);

/**
 * @brief Check if CANServer UDP server is running
 *
 * @return true if server is actively listening on port 1338
 */
bool canserver_udp_server_is_running(void);

/**
 * @brief Get count of connected CANServer clients
 *
 * @return Number of active UDP clients (0-4)
 */
int canserver_udp_server_get_client_count(void);

/**
 * @brief Broadcast CAN frame to all connected CANServer clients
 *
 * Called from CAN RX task when a frame is received.
 * Encodes the frame in Panda binary format and sends to all active clients.
 *
 * @param bus Bus number (0 = BODY, 1 = CHASSIS)
 * @param msg Pointer to TWAI message structure
 */
void canserver_udp_broadcast_can_frame(int bus, const twai_message_t *msg);

/**
 * @brief Set autostart preference (saved to NVS)
 *
 * @param autostart true to start server automatically on boot
 * @return ESP_OK on success
 */
esp_err_t canserver_udp_server_set_autostart(bool autostart);

/**
 * @brief Get autostart preference (from NVS)
 *
 * @return true if server should start automatically on boot
 */
bool canserver_udp_server_get_autostart(void);

#ifdef __cplusplus
}
#endif

#endif // CANSERVER_UDP_SERVER_H
