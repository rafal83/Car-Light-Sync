#ifndef CAN_SERVERS_CONFIG_H
#define CAN_SERVERS_CONFIG_H

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CAN server types
 */
typedef enum {
  CAN_SERVER_GVRET,
  CAN_SERVER_CANSERVER,
  CAN_SERVER_SLCAN,
  CAN_SERVER_COUNT
} can_server_type_t;

/**
 * @brief Set autostart preference for a CAN server (saved to NVS)
 *
 * @param server_type Type of CAN server
 * @param autostart true to start server automatically on boot
 * @return ESP_OK on success
 */
esp_err_t can_servers_config_set_autostart(can_server_type_t server_type, bool autostart);

/**
 * @brief Get autostart preference for a CAN server (from NVS)
 *
 * @param server_type Type of CAN server
 * @return true if server should start automatically on boot
 */
bool can_servers_config_get_autostart(can_server_type_t server_type);

#ifdef __cplusplus
}
#endif

#endif // CAN_SERVERS_CONFIG_H
