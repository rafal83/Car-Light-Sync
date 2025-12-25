#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include "esp_err.h"

#include <stdbool.h>

#define TAG_CAPTIVE_PORTAL "CaptivePortal"

/**
 * @brief Initializes the captive portal (DNS server)
 * @return ESP_OK if successful
 */
esp_err_t captive_portal_init(void);

/**
 * @brief Starts the captive portal
 * @return ESP_OK if successful
 */
esp_err_t captive_portal_start(void);

/**
 * @brief Stops the captive portal
 * @return ESP_OK if successful
 */
esp_err_t captive_portal_stop(void);

/**
 * @brief Checks if the captive portal is active
 * @return true if active, false otherwise
 */
bool captive_portal_is_running(void);

#endif // CAPTIVE_PORTAL_H
