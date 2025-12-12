#ifndef GVRET_TCP_SERVER_H
#define GVRET_TCP_SERVER_H

#include "esp_err.h"
#include "driver/twai.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize GVRET TCP server
 *
 * Must be called once at startup before start/stop operations
 *
 * @return ESP_OK on success
 */
esp_err_t gvret_tcp_server_init(void);

/**
 * @brief Start GVRET TCP server on port 8080
 *
 * Starts listening for SavvyCAN connections.
 * Can be called multiple times (idempotent).
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already running
 */
esp_err_t gvret_tcp_server_start(void);

/**
 * @brief Stop GVRET TCP server
 *
 * Closes all client connections and stops listening.
 * Can be called multiple times (idempotent).
 */
void gvret_tcp_server_stop(void);

/**
 * @brief Check if GVRET TCP server is running
 *
 * @return true if server is actively listening on port 8080
 */
bool gvret_tcp_server_is_running(void);

/**
 * @brief Get count of connected GVRET clients
 *
 * @return Number of active TCP connections (0-4)
 */
int gvret_tcp_server_get_client_count(void);

/**
 * @brief Broadcast CAN frame to all connected GVRET clients
 *
 * Called from CAN RX task when a frame is received.
 * Encodes the frame in GVRET binary format and sends to all active clients.
 *
 * @param bus Bus number (0 = BODY, 1 = CHASSIS)
 * @param msg Pointer to TWAI message structure
 */
void gvret_tcp_broadcast_can_frame(int bus, const twai_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif // GVRET_TCP_SERVER_H
