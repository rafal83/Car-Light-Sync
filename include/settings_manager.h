#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define TAG_SETTINGS "Settings"

// Centralized configuration file
#define SETTINGS_FILE_PATH "/spiffs/config/settings.json"

// Structure for system configuration (stored in JSON in SPIFFS)
typedef struct {
  // Active profile ID
  int32_t active_profile_id;

  // LED Hardware
  uint16_t led_count;

  // Wheel control
  bool wheel_control_enabled;
  uint8_t wheel_control_speed_limit;

  // CAN Servers autostart
  bool gvret_autostart;
  bool canserver_autostart;

  // ESP-NOW
  uint8_t espnow_role;
  uint8_t espnow_type;

} system_settings_t;

/**
 * @brief Initializes the settings manager
 * @return ESP_OK if successful
 */
esp_err_t settings_manager_init(void);

/**
 * @brief Loads system settings from SPIFFS
 * @param settings Pointer to structure to fill
 * @return ESP_OK if successful
 */
esp_err_t settings_manager_load(system_settings_t *settings);

/**
 * @brief Saves system settings to SPIFFS
 * @param settings Pointer to structure to save
 * @return ESP_OK if successful
 */
esp_err_t settings_manager_save(const system_settings_t *settings);

/**
 * @brief Gets an int32 value
 */
int32_t settings_get_i32(const char *key, int32_t default_value);

/**
 * @brief Sets an int32 value
 */
esp_err_t settings_set_i32(const char *key, int32_t value);

/**
 * @brief Gets a uint8 value
 */
uint8_t settings_get_u8(const char *key, uint8_t default_value);

/**
 * @brief Sets a uint8 value
 */
esp_err_t settings_set_u8(const char *key, uint8_t value);

/**
 * @brief Gets a uint16 value
 */
uint16_t settings_get_u16(const char *key, uint16_t default_value);

/**
 * @brief Sets a uint16 value
 */
esp_err_t settings_set_u16(const char *key, uint16_t value);

/**
 * @brief Gets a bool value
 */
bool settings_get_bool(const char *key, bool default_value);

/**
 * @brief Sets a bool value
 */
esp_err_t settings_set_bool(const char *key, bool value);

/**
 * @brief Clears all settings (factory reset)
 */
esp_err_t settings_manager_clear(void);

/**
 * @brief Begins a batch update session (defers saves until commit)
 * @note Improves performance when updating multiple settings
 */
void settings_begin_batch(void);

/**
 * @brief Commits all pending batch changes to SPIFFS
 * @return ESP_OK if successful
 */
esp_err_t settings_commit_batch(void);

/**
 * @brief Checks if currently in batch mode
 * @return true if batch mode is active
 */
bool settings_is_batch_mode(void);

#endif // SETTINGS_MANAGER_H
