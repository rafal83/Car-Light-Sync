#ifndef SLCAN_TCP_SERVER_H
#define SLCAN_TCP_SERVER_H

#include "esp_err.h"
#include "driver/twai.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SLCAN TCP server
 *
 * Must be called once at startup before start/stop operations
 *
 * @return ESP_OK on success
 */
esp_err_t slcan_tcp_server_init(void);

/**
 * @brief Start SLCAN TCP server on port 3333
 *
 * Starts listening for SLCAN/LAWICEL protocol connections.
 * Can be called multiple times (idempotent).
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already running
 */
esp_err_t slcan_tcp_server_start(void);

/**
 * @brief Stop SLCAN TCP server
 *
 * Closes all client connections and stops listening.
 * Can be called multiple times (idempotent).
 */
void slcan_tcp_server_stop(void);

/**
 * @brief Check if SLCAN TCP server is running
 *
 * @return true if server is actively listening on port 3333
 */
bool slcan_tcp_server_is_running(void);

/**
 * @brief Get count of connected SLCAN clients
 *
 * @return Number of active TCP connections (0-4)
 */
int slcan_tcp_server_get_client_count(void);

/**
 * @brief Broadcast CAN frame to all connected SLCAN clients
 *
 * Called from CAN RX task when a frame is received.
 * Encodes the frame in SLCAN ASCII format and sends to all active clients.
 *
 * @param bus Bus number (0 = BODY, 1 = CHASSIS)
 * @param msg Pointer to TWAI message structure
 */
void slcan_tcp_broadcast_can_frame(int bus, const twai_message_t *msg);

/**
 * @brief Set autostart preference (saved to NVS)
 *
 * @param autostart true to start server automatically on boot
 * @return ESP_OK on success
 */
esp_err_t slcan_tcp_server_set_autostart(bool autostart);

/**
 * @brief Get autostart preference (from NVS)
 *
 * @return true if server should start automatically on boot
 */
bool slcan_tcp_server_get_autostart(void);

#ifdef __cplusplus
}
#endif

#endif // SLCAN_TCP_SERVER_H
