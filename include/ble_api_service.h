#pragma once

#include "esp_err.h"

#include <stdbool.h>

#define TAG_BLE_API "BLE_API"
#define BLE_API_DEVICE_NAME "CarLightSync" // Will be replaced by g_device_name_with_suffix
#define BLE_HTTP_LOCAL_BASE_URL "http://127.0.0.1"
#define BLE_MAX_REQUEST_LEN 16384
#define BLE_MAX_RESPONSE_BODY 16384
#define BLE_NOTIFY_CHUNK_MAX 512
#define BLE_REQUEST_QUEUE_LENGTH 3
#define BLE_HTTP_TIMEOUT_MS 4000

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the BLE service exposing HTTP APIs.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t ble_api_service_init(void);

/**
 * @brief Starts BLE advertising (to be called after WiFi/Web initialization).
 */
esp_err_t ble_api_service_start(void);

/**
 * @brief Stops BLE advertising and frees resources if necessary.
 */
esp_err_t ble_api_service_stop(void);

/**
 * @brief Indicates if a BLE client is currently connected.
 */
bool ble_api_service_is_connected(void);

bool ble_api_service_config_ack_received(void);
void ble_api_service_clear_config_ack(void);

/**
 * @brief Sends vehicle state via BLE (for dashboard mode).
 * @param state Pointer to vehicle_state_t structure.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t ble_api_service_send_vehicle_state(const void *state, size_t size);

#ifdef __cplusplus
}
#endif
