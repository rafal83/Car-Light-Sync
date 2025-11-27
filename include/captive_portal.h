#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include "esp_err.h"
#include <stdbool.h>

#define TAG_CAPTIVE_PORTAL "CaptivePortal"

/**
 * @brief Initialise le portail captif (serveur DNS)
 * @return ESP_OK si succès
 */
esp_err_t captive_portal_init(void);

/**
 * @brief Démarre le portail captif
 * @return ESP_OK si succès
 */
esp_err_t captive_portal_start(void);

/**
 * @brief Arrête le portail captif
 * @return ESP_OK si succès
 */
esp_err_t captive_portal_stop(void);

/**
 * @brief Vérifie si le portail captif est actif
 * @return true si actif, false sinon
 */
bool captive_portal_is_running(void);

#endif // CAPTIVE_PORTAL_H
