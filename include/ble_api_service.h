#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise le service BLE exposant les API HTTP.
 *
 * @return ESP_OK en cas de succès, code d'erreur sinon.
 */
esp_err_t ble_api_service_init(void);

/**
 * @brief Démarre la publicité BLE (à appeler après l'initialisation WiFi/Web).
 */
esp_err_t ble_api_service_start(void);

/**
 * @brief Arrête la publicité BLE et libère les ressources si nécessaire.
 */
esp_err_t ble_api_service_stop(void);

/**
 * @brief Indique si un client BLE est actuellement connecté.
 */
bool ble_api_service_is_connected(void);

#ifdef __cplusplus
}
#endif

