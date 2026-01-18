#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TAG_OTA "OTA"

/**
 * @brief OTA update state
 */
typedef enum {
  OTA_STATE_IDLE = 0,
  OTA_STATE_RECEIVING,
  OTA_STATE_WRITING,
  OTA_STATE_SUCCESS,
  OTA_STATE_ERROR
} ota_state_t;

/**
 * @brief Information about OTA progress
 */
typedef struct {
  ota_state_t state;
  uint32_t total_size;
  uint32_t written_size;
  uint8_t progress; // Percentage 0-100
  char error_msg[128];
} ota_progress_t;

/**
 * @brief Initializes the OTA system
 * @return ESP_OK if successful
 */
esp_err_t ota_init(void);

/**
 * @brief Starts an OTA update
 * @return ESP_OK if successful
 */
esp_err_t ota_begin(size_t total_size);

/**
 * @brief Writes firmware data
 * @param data Pointer to data
 * @param size Size of data
 * @return ESP_OK if successful
 */
esp_err_t ota_write(const void *data, size_t size);

/**
 * @brief Finishes the OTA update
 * @return ESP_OK if successful
 */
esp_err_t ota_end(void);

/**
 * @brief Cancels the current OTA update
 */
void ota_abort(void);

/**
 * @brief Gets the current progress
 * @param progress Pointer to progress structure
 */
void ota_get_progress(ota_progress_t *progress);

/**
 * @brief Gets the current firmware version
 * @return Version string
 */
const char *ota_get_current_version(void);

/**
 * @brief Restarts the ESP32
 */
void ota_restart(void);

/**
 * @brief Validates the current OTA partition
 * @return ESP_OK if successful
 */
esp_err_t ota_validate_current_partition(void);

/**
 * @brief Returns the number of seconds remaining before auto-restart
 * @return Countdown (>=0) or -1 if no restart scheduled
 */
int ota_get_reboot_countdown(void);

#endif // OTA_UPDATE_H
