#include "ota_update.h"

#include "config_manager.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_effects.h"
#include "task_core_utils.h"
#include "version_info.h"

#include <string.h>

static const uint32_t OTA_REBOOT_DELAY_MS      = 30000;
static const uint32_t OTA_WRITE_THROTTLE_MS    = 5;

static ota_progress_t current_progress         = {0};
static esp_ota_handle_t ota_handle             = 0;
static const esp_partition_t *update_partition = NULL;
static bool ota_in_progress                    = false;
static bool ota_reboot_scheduled               = false;
static TickType_t ota_reboot_deadline          = 0;

static void ota_reboot_task(void *arg) {
  vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_DELAY_MS));
  ota_restart();
  vTaskDelete(NULL);
}

static void ota_schedule_reboot(void) {
  if (ota_reboot_scheduled) {
    return;
  }
  ota_reboot_scheduled = true;
  ota_reboot_deadline  = xTaskGetTickCount() + pdMS_TO_TICKS(OTA_REBOOT_DELAY_MS);
  if (create_task_on_general_core(ota_reboot_task, "ota_reboot", 2048, NULL, 5, NULL) != pdPASS) {
    ota_reboot_scheduled = false;
    ota_reboot_deadline  = 0;
    ESP_LOGE(TAG_OTA, "Unable to launch OTA restart task");
  }
}

esp_err_t ota_init(void) {
  memset(&current_progress, 0, sizeof(ota_progress_t));
  current_progress.state = OTA_STATE_IDLE;
  ota_reboot_scheduled   = false;
  ota_reboot_deadline    = 0;

  ESP_LOGI(TAG_OTA, "OTA initialized, version: %s", APP_VERSION_STRING);
  return ESP_OK;
}

esp_err_t ota_begin(size_t total_size) {
  if (ota_in_progress) {
    ESP_LOGW(TAG_OTA, "OTA already running");
    return ESP_ERR_INVALID_STATE;
  }

  config_manager_stop_all_events();
  led_effects_start_progress_display();
  led_effects_update_progress(0);

  // Rinitialiser la progression
  memset(&current_progress, 0, sizeof(ota_progress_t));
  current_progress.state        = OTA_STATE_RECEIVING;
  current_progress.total_size   = total_size;
  current_progress.written_size = 0;
  current_progress.progress     = 0;

  // Obtenir la partition de mise  jour
  update_partition              = esp_ota_get_next_update_partition(NULL);
  if (update_partition == NULL) {
    ESP_LOGE(TAG_OTA, "Unable to find the OTA partition");
    strncpy(current_progress.error_msg, "OTA partition not found", sizeof(current_progress.error_msg) - 1);
    current_progress.state = OTA_STATE_ERROR;
    led_effects_stop_progress_display();
    led_effects_show_upgrade_error();
    ota_schedule_reboot();
    return ESP_ERR_NOT_FOUND;
  }

  ESP_LOGI(TAG_OTA, "Starting OTA to partition: %s at 0x%08lx", update_partition->label, update_partition->address);

  // Start OTA
  esp_err_t ret = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_OTA, "esp_ota_begin error: %s", esp_err_to_name(ret));
    snprintf(current_progress.error_msg, sizeof(current_progress.error_msg), "OTA start error: %s", esp_err_to_name(ret));
    current_progress.state = OTA_STATE_ERROR;
    led_effects_stop_progress_display();
    led_effects_show_upgrade_error();
    ota_schedule_reboot();
    return ret;
  }

  ota_in_progress = true;
  ESP_LOGI(TAG_OTA, "OTA started successfully");
  return ESP_OK;
}

esp_err_t ota_write(const void *data, size_t size) {
  if (!ota_in_progress) {
    ESP_LOGE(TAG_OTA, "OTA not started");
    return ESP_ERR_INVALID_STATE;
  }

  current_progress.state = OTA_STATE_WRITING;

  esp_err_t ret          = esp_ota_write(ota_handle, data, size);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_OTA, "esp_ota_write error: %s", esp_err_to_name(ret));
    snprintf(current_progress.error_msg, sizeof(current_progress.error_msg), "OTA write error: %s", esp_err_to_name(ret));
    current_progress.state = OTA_STATE_ERROR;
    ota_in_progress        = false;
    led_effects_stop_progress_display();
    led_effects_show_upgrade_error();
    ota_schedule_reboot();
    return ret;
  }

  current_progress.written_size += size;

  if (current_progress.total_size > 0) {
    current_progress.progress = (current_progress.written_size * 100) / current_progress.total_size;
    if (current_progress.progress > 100) {
      current_progress.progress = 100;
    }
  }
  led_effects_update_progress(current_progress.progress);
  if (OTA_WRITE_THROTTLE_MS > 0) {
    vTaskDelay(pdMS_TO_TICKS(OTA_WRITE_THROTTLE_MS));
  }

  ESP_LOGD(TAG_OTA, "OTA wrote %d bytes, total: %lu", size, current_progress.written_size);
  return ESP_OK;
}

esp_err_t ota_end(void) {
  if (!ota_in_progress) {
    ESP_LOGE(TAG_OTA, "OTA not started");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = esp_ota_end(ota_handle);
  if (ret != ESP_OK) {
    if (ret == ESP_ERR_OTA_VALIDATE_FAILED) {
      ESP_LOGE(TAG_OTA, "Image validation failed");
      strncpy(current_progress.error_msg, "Firmware validation failed", sizeof(current_progress.error_msg) - 1);
    } else {
      ESP_LOGE(TAG_OTA, "esp_ota_end error: %s", esp_err_to_name(ret));
      snprintf(current_progress.error_msg, sizeof(current_progress.error_msg), "OTA finalize error: %s", esp_err_to_name(ret));
    }
    current_progress.state = OTA_STATE_ERROR;
    ota_in_progress        = false;
    led_effects_stop_progress_display();
    led_effects_show_upgrade_error();
    ota_schedule_reboot();
    return ret;
  }

  // Set the new partition as the boot partition
  ret = esp_ota_set_boot_partition(update_partition);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_OTA, "esp_ota_set_boot_partition error: %s", esp_err_to_name(ret));
    snprintf(current_progress.error_msg, sizeof(current_progress.error_msg), "Boot configuration error: %s", esp_err_to_name(ret));
    current_progress.state = OTA_STATE_ERROR;
    ota_in_progress        = false;
    led_effects_stop_progress_display();
    led_effects_show_upgrade_error();
    ota_schedule_reboot();
    return ret;
  }

  current_progress.state    = OTA_STATE_SUCCESS;
  current_progress.progress = 100;
  ota_in_progress           = false;
  led_effects_stop_progress_display();
  led_effects_show_upgrade_ready();
  ota_schedule_reboot();

  ESP_LOGI(TAG_OTA, "OTA completed successfully! Restart required.");
  return ESP_OK;
}

void ota_abort(void) {
  if (ota_in_progress) {
    esp_ota_abort(ota_handle);
    ota_in_progress        = false;
    current_progress.state = OTA_STATE_IDLE;
    led_effects_stop_progress_display();
    ESP_LOGW(TAG_OTA, "OTA canceled");
  }
}

void ota_get_progress(ota_progress_t *progress) {
  if (progress != NULL) {
    memcpy(progress, &current_progress, sizeof(ota_progress_t));
  }
}

const char *ota_get_current_version(void) {
  return APP_VERSION_STRING;
}

void ota_restart(void) {
  ESP_LOGI(TAG_OTA, "Restarting in 2 seconds...");
  vTaskDelay(pdMS_TO_TICKS(2000));
  esp_restart();
}

esp_err_t ota_validate_current_partition(void) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;

  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      // First boot after an update, validate the new image
      esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
      if (ret == ESP_OK) {
        ESP_LOGI(TAG_OTA, "OTA partition validated");
      } else {
        ESP_LOGE(TAG_OTA, "Partition validation error: %s", esp_err_to_name(ret));
      }
      return ret;
    }
  }

  return ESP_OK;
}

int ota_get_reboot_countdown(void) {
  if (!ota_reboot_scheduled || ota_reboot_deadline == 0) {
    return -1;
  }

  TickType_t now = xTaskGetTickCount();
  if (ota_reboot_deadline <= now) {
    return 0;
  }

  TickType_t remaining_ticks = ota_reboot_deadline - now;
  uint32_t remaining_ms      = remaining_ticks * portTICK_PERIOD_MS;
  int seconds                = (remaining_ms + 999) / 1000;
  return seconds;
}
