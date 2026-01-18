#ifndef BOOT_LOOP_GUARD_H
#define BOOT_LOOP_GUARD_H

#include "esp_err.h"

#include <stdbool.h>

/**
 * @brief Maximum consecutive reboots before factory reset
 */
#define BOOT_LOOP_MAX_COUNT 10

/**
 * @brief Time in ms after which the counter resets (successful boot)
 */
#define BOOT_LOOP_SUCCESS_TIMEOUT_MS 30000 // 30 secondes

/**
 * @brief Initialize boot loop protection
 *
 * Checks the boot counter in LP SRAM and triggers a factory reset
 * if the number of consecutive reboots exceeds the threshold.
 *
 * @return ESP_OK on success, ESP_FAIL if factory reset was triggered
 */
esp_err_t boot_loop_guard_init(void);

/**
 * @brief Mark startup as successful and reset the counter
 *
 * Call after all critical components have started successfully.
 */
void boot_loop_guard_mark_success(void);

/**
 * @brief Get current boot loop count
 *
 * @return Number of consecutive reboots
 */
uint32_t boot_loop_guard_get_count(void);

#endif // BOOT_LOOP_GUARD_H
