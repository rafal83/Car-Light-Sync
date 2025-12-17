#include "boot_loop_guard.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "reset_button.h"

#include <string.h>

static const char *TAG = "BootLoopGuard";

/**
 * @brief Structure stored in LP SRAM (RTC Fast Memory)
 *
 * This memory persists during deep sleep and reboots,
 * but is cleared on full power-off.
 */
typedef struct {
  uint32_t magic;        // Signature to validate data
  uint32_t boot_count;   // Consecutive boot counter
  uint64_t last_boot_us; // Timestamp of last boot (microseconds)
} boot_loop_data_t;

// Store in RTC Fast Memory (LP SRAM) with RTC_NOINIT_ATTR so data persists across reboots
static RTC_NOINIT_ATTR boot_loop_data_t s_boot_data;

#define BOOT_LOOP_MAGIC 0xB007C0DE  // Magic signature to validate data

static TaskHandle_t watchdog_task_handle = NULL;

/**
 * @brief Task that monitors successful boot and resets the counter
 */
static void boot_watchdog_task(void *pvParameters) {
  ESP_LOGI(TAG, "Boot watchdog started, waiting %d ms before marking boot successful",
           BOOT_LOOP_SUCCESS_TIMEOUT_MS);

  vTaskDelay(pdMS_TO_TICKS(BOOT_LOOP_SUCCESS_TIMEOUT_MS));

  boot_loop_guard_mark_success();

  // Task done
  watchdog_task_handle = NULL;
  vTaskDelete(NULL);
}

esp_err_t boot_loop_guard_init(void) {
  uint64_t current_time_us = esp_timer_get_time();

  // Check if RTC data is valid
  if (s_boot_data.magic != BOOT_LOOP_MAGIC) {
    ESP_LOGI(TAG, "First initialization or full reset detected, initializing counter");
    s_boot_data.magic = BOOT_LOOP_MAGIC;
    s_boot_data.boot_count = 0;
    s_boot_data.last_boot_us = current_time_us;
  }

  // Increment the boot counter
  s_boot_data.boot_count++;
  uint64_t time_since_last_boot_us = current_time_us - s_boot_data.last_boot_us;
  uint32_t time_since_last_boot_ms = (uint32_t)(time_since_last_boot_us / 1000);

  s_boot_data.last_boot_us = current_time_us;

  ESP_LOGI(TAG, "Boot count: %lu (time since last boot: %lu ms)",
           s_boot_data.boot_count, time_since_last_boot_ms);

  // Check for boot loop
  if (s_boot_data.boot_count >= BOOT_LOOP_MAX_COUNT) {
    ESP_LOGE(TAG, "========================================");
    ESP_LOGE(TAG, "   BOOT LOOP DETECTED!");
    ESP_LOGE(TAG, "   %lu consecutive reboots", s_boot_data.boot_count);
    ESP_LOGE(TAG, "   AUTOMATIC FACTORY RESET");
    ESP_LOGE(TAG, "========================================");

    // Small delay to flush logs
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Reset counter before factory reset
    s_boot_data.boot_count = 0;

    // Trigger factory reset
    reset_button_factory_reset();

    // Ne devrait jamais arriver ici
    return ESP_FAIL;
  }

  // If time since last boot is long (> timeout), treat as normal boot
  if (time_since_last_boot_ms > BOOT_LOOP_SUCCESS_TIMEOUT_MS) {
    ESP_LOGI(TAG, "Time since last boot > timeout, resetting counter");
    s_boot_data.boot_count = 1;
  }

  // Create a watchdog task to mark boot as successful after the timeout
  BaseType_t task_created = xTaskCreate(
    boot_watchdog_task,
    "boot_watchdog",
    2048,
    NULL,
    1,  // Low priority
    &watchdog_task_handle
  );

  if (task_created != pdPASS) {
    ESP_LOGE(TAG, "Error creating watchdog task");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Boot loop protection initialized (threshold: %d reboots)", BOOT_LOOP_MAX_COUNT);
  return ESP_OK;
}

void boot_loop_guard_mark_success(void) {
  if (s_boot_data.boot_count > 0) {
    ESP_LOGI(TAG, "Boot successful after %lu attempts, resetting counter", s_boot_data.boot_count);
    s_boot_data.boot_count = 0;
  }
}

uint32_t boot_loop_guard_get_count(void) {
  return s_boot_data.boot_count;
}
