#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// NVS Namespace Constants
// ============================================================================

/** @brief NVS namespace for audio configuration */
#define NVS_NAMESPACE_AUDIO "audio_config"

/** @brief NVS namespace for configuration profiles */
#define NVS_NAMESPACE_PROFILES "profiles"

/** @brief NVS namespace for general settings */
#define NVS_NAMESPACE_SETTINGS "settings"

/** @brief NVS namespace for LED hardware configuration */
#define NVS_NAMESPACE_LED_HW "led_hw"

/** @brief NVS namespace for CAN server configuration */
#define NVS_NAMESPACE_CAN_SERVERS "can_servers"

// ============================================================================
// Generic NVS Get/Set Functions
// ============================================================================

/**
 * @brief Generic NVS get/set functions for different data types
 *
 * These functions provide a centralized, error-handled interface to NVS storage.
 * All functions automatically open/close NVS handles and provide consistent error logging.
 */

/**
 * @brief Set a uint8_t value in NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param value Value to store
 * @return ESP_OK on success
 */
esp_err_t nvs_manager_set_u8(const char *namespace, const char *key, uint8_t value);

/**
 * @brief Get a uint8_t value from NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param default_value Default value if key not found
 * @return Stored value or default_value if not found
 */
uint8_t nvs_manager_get_u8(const char *namespace, const char *key, uint8_t default_value);

/**
 * @brief Set a boolean value in NVS (stored as uint8_t)
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param value Value to store
 * @return ESP_OK on success
 */
esp_err_t nvs_manager_set_bool(const char *namespace, const char *key, bool value);

/**
 * @brief Get a boolean value from NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param default_value Default value if key not found
 * @return Stored value or default_value if not found
 */
bool nvs_manager_get_bool(const char *namespace, const char *key, bool default_value);

/**
 * @brief Set a uint16_t value in NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param value Value to store
 * @return ESP_OK on success
 */
esp_err_t nvs_manager_set_u16(const char *namespace, const char *key, uint16_t value);

/**
 * @brief Get a uint16_t value from NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param default_value Default value if key not found
 * @return Stored value or default_value if not found
 */
uint16_t nvs_manager_get_u16(const char *namespace, const char *key, uint16_t default_value);

/**
 * @brief Set a uint32_t value in NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param value Value to store
 * @return ESP_OK on success
 */
esp_err_t nvs_manager_set_u32(const char *namespace, const char *key, uint32_t value);

/**
 * @brief Get a uint32_t value from NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param default_value Default value if key not found
 * @return Stored value or default_value if not found
 */
uint32_t nvs_manager_get_u32(const char *namespace, const char *key, uint32_t default_value);

/**
 * @brief Set a int32_t value in NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param value Value to store
 * @return ESP_OK on success
 */
esp_err_t nvs_manager_set_i32(const char *namespace, const char *key, int32_t value);

/**
 * @brief Get a int32_t value from NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param default_value Default value if key not found
 * @return Stored value or default_value if not found
 */
int32_t nvs_manager_get_i32(const char *namespace, const char *key, int32_t default_value);

/**
 * @brief Set a string value in NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param value String to store
 * @return ESP_OK on success
 */
esp_err_t nvs_manager_set_str(const char *namespace, const char *key, const char *value);

/**
 * @brief Get a string value from NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param out_buffer Buffer to store the string
 * @param buffer_size Size of the output buffer
 * @param default_value Default string if key not found (can be NULL)
 * @return ESP_OK on success
 */
esp_err_t nvs_manager_get_str(const char *namespace, const char *key,
                               char *out_buffer, size_t buffer_size,
                               const char *default_value);

/**
 * @brief Set a blob (binary data) value in NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param value Pointer to binary data
 * @param length Length of binary data
 * @return ESP_OK on success
 */
esp_err_t nvs_manager_set_blob(const char *namespace, const char *key,
                                const void *value, size_t length);

/**
 * @brief Get a blob (binary data) value from NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @param out_buffer Buffer to store the blob
 * @param buffer_size Size of the output buffer (in/out parameter)
 * @return ESP_OK on success
 */
esp_err_t nvs_manager_get_blob(const char *namespace, const char *key,
                                void *out_buffer, size_t *buffer_size);

/**
 * @brief Erase a key from NVS
 *
 * @param namespace NVS namespace
 * @param key NVS key
 * @return ESP_OK on success
 */
esp_err_t nvs_manager_erase_key(const char *namespace, const char *key);

/**
 * @brief Erase all keys in a namespace
 *
 * @param namespace NVS namespace
 * @return ESP_OK on success
 */
esp_err_t nvs_manager_erase_namespace(const char *namespace);

#ifdef __cplusplus
}
#endif

#endif // NVS_MANAGER_H
