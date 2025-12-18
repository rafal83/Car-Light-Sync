#pragma once

#include "esp_err.h"

#include <stdbool.h>

#define TAG_BLE_API "BLE_API"
#define BLE_API_DEVICE_NAME "CarLightSync" // Sera remplacé par g_device_name_with_suffix
#define BLE_HTTP_LOCAL_BASE_URL "http://127.0.0.1"
#define BLE_MAX_REQUEST_LEN 16384
#define BLE_MAX_RESPONSE_BODY 8192
#define BLE_NOTIFY_CHUNK_MAX 512
#define BLE_REQUEST_QUEUE_LENGTH 3
#define BLE_HTTP_TIMEOUT_MS 4000

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

/**
 * @brief Envoie l'état du véhicule via BLE (pour le mode dashboard).
 * @param state Pointeur vers la structure vehicle_state_t.
 * @return ESP_OK en cas de succès, code d'erreur sinon.
 */
esp_err_t ble_api_service_send_vehicle_state(const void *state, size_t size);

#ifdef __cplusplus
}
#endif
