#include "reset_button.h"

#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "status_led.h"
#include "status_manager.h"
#include "espnow_link.h"

static const char *TAG                       = "ResetButton";
static TaskHandle_t reset_button_task_handle = NULL;

#define SHORT_PRESS_MIN_MS 50
#define SHORT_PRESS_MAX_MS 2000

/**
 * @brief Reset button monitoring task
 */
static void reset_button_task(void *arg) {
  uint32_t press_start_time = 0;
  bool button_pressed       = false;
  bool reset_led_shown      = false;

  while (1) {
    int level = gpio_get_level(RESET_BUTTON_GPIO);

    if (level == 0) { // Button pressed (pull-up, so 0 = pressed)
      if (!button_pressed) {
        button_pressed   = true;
        reset_led_shown  = false;
        press_start_time = esp_log_timestamp();
        ESP_LOGI(TAG, "Reset button pressed");
      } else {
        uint32_t press_duration = esp_log_timestamp() - press_start_time;

        // Show the reset LED after 1 second of press
        if (press_duration >= 1000 && !reset_led_shown) {
          ESP_LOGI(TAG, "Long press detected, reset LED activated");
          status_led_set_state(STATUS_LED_FACTORY_RESET);
          reset_led_shown = true;
        }

        if (press_duration >= RESET_BUTTON_HOLD_TIME_MS) {
          ESP_LOGW(TAG, "Reset button held for %lu ms, factory reset!", press_duration);
          reset_button_factory_reset();
          // Should never reach here because factory_reset restarts
        }
      }
    } else { // Button released
      if (button_pressed) {
        uint32_t press_duration = esp_log_timestamp() - press_start_time;
        ESP_LOGI(TAG, "Reset button released after %lu ms", press_duration);
        button_pressed  = false;
        reset_led_shown = false;

        if (press_duration >= SHORT_PRESS_MIN_MS && press_duration < SHORT_PRESS_MAX_MS) {
            // Short press
            ESP_LOGI(TAG, "Short press detected");
            if (espnow_link_get_role() == ESP_NOW_ROLE_SLAVE) {
                ESP_LOGI(TAG, "Slave mode, triggering ESP-NOW pairing");
                espnow_link_trigger_slave_pairing();
            } else {
                ESP_LOGI(TAG, "Master mode, short press does nothing");
                status_manager_update_led_now();
            }
        } else if (press_duration < RESET_BUTTON_HOLD_TIME_MS) {
            // Press too short or released before factory reset
            ESP_LOGI(TAG, "Restoring normal LED state");
            status_manager_update_led_now();
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
  }
}

esp_err_t reset_button_init(void) {
  ESP_LOGI(TAG, "Reset button init on GPIO %d", RESET_BUTTON_GPIO);

  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << RESET_BUTTON_GPIO),
      .mode         = GPIO_MODE_INPUT,
      .pull_up_en   = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type    = GPIO_INTR_DISABLE,
  };

  esp_err_t err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Reset button GPIO config error: %s", esp_err_to_name(err));
    return err;
  }

  // Create the monitoring task
  BaseType_t task_created = xTaskCreatePinnedToCore(reset_button_task, "reset_button", 2560, NULL, 5, &reset_button_task_handle, tskNO_AFFINITY);

  if (task_created != pdPASS) {
    ESP_LOGE(TAG, "Reset button task creation error");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Reset button initialized (short press = pairing, 5s press = factory reset)");
  return ESP_OK;
}

void reset_button_factory_reset(void) {
  ESP_LOGW(TAG, "========================================");
  ESP_LOGW(TAG, "   FACTORY RESET - NVS ERASE");
  ESP_LOGW(TAG, "========================================");

  // Set the LED to reset mode
  status_led_set_state(STATUS_LED_FACTORY_RESET);

  // Erase all NVS
  esp_err_t err = nvs_flash_erase();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS erase error: %s", esp_err_to_name(err));
    status_led_set_state(STATUS_LED_ERROR);
  } else {
    ESP_LOGI(TAG, "NVS erased successfully");
  }

  // Wait so logs are printed
  vTaskDelay(pdMS_TO_TICKS(1000));

  // Restart the ESP32
  ESP_LOGW(TAG, "Restarting in 2 seconds...");
  vTaskDelay(pdMS_TO_TICKS(2000));
  esp_restart();
}