#ifndef PANDA_TCP_SERVER_H
#define PANDA_TCP_SERVER_H

#include "esp_err.h"
#include "driver/twai.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Panda TCP server
 *
 * Must be called once at startup before start/stop operations
 *
 * @return ESP_OK on success
 */
esp_err_t panda_tcp_server_init(void);

/**
 * @brief Start Panda TCP server on port 1338
 *
 * Starts listening for comma.ai Panda/cabana connections.
 * Can be called multiple times (idempotent).
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already running
 */
esp_err_t panda_tcp_server_start(void);

/**
 * @brief Stop Panda TCP server
 *
 * Closes all client connections and stops listening.
 * Can be called multiple times (idempotent).
 */
void panda_tcp_server_stop(void);

/**
 * @brief Check if Panda TCP server is running
 *
 * @return true if server is actively listening on port 1338
 */
bool panda_tcp_server_is_running(void);

/**
 * @brief Get count of connected Panda clients
 *
 * @return Number of active TCP connections (0-4)
 */
int panda_tcp_server_get_client_count(void);

/**
 * @brief Broadcast CAN frame to all connected Panda clients
 *
 * Called from CAN RX task when a frame is received.
 * Encodes the frame in Panda binary format and sends to all active clients.
 *
 * @param bus Bus number (0 = BODY, 1 = CHASSIS)
 * @param msg Pointer to TWAI message structure
 */
void panda_tcp_broadcast_can_frame(int bus, const twai_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif // PANDA_TCP_SERVER_H
