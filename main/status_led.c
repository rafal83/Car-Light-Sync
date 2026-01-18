#include "status_led.h"

#include "config.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip_encoder.h"

#include <math.h>

// GPIO for the status LED (integrated WS2812)
#if CONFIG_IDF_TARGET_ESP32S3
#define STATUS_LED_GPIO 21
#elif CONFIG_IDF_TARGET_ESP32C6
// Disabled on C6 to free RMT for the main LED strip
#define STATUS_LED_GPIO 8 // No LED on other boards
#else
#define STATUS_LED_GPIO -1 // No LED on other boards
#endif

#define STATUS_LED_MEM_BLOCK_SYMBOLS 48

#define STATUS_LED_RMT_RESOLUTION 10000000 // 10MHz

static const char *TAG                         = "StatusLED";
static rmt_channel_handle_t status_led_chan    = NULL;
static rmt_encoder_handle_t status_led_encoder = NULL;
static TaskHandle_t status_led_task_handle     = NULL;
static status_led_state_t current_state        = STATUS_LED_BOOT;
static bool led_initialized                    = false;

// Structure defining an RGB color
typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} rgb_t;

/**
 * @brief Send an RGB color to the LED
 */
static esp_err_t status_led_set_color(uint8_t r, uint8_t g, uint8_t b) {
  if (!led_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t led_data[3]             = {g * .1, r * .1, b * .1}; // WS2812 format: GRB at 10% brightness

  rmt_transmit_config_t tx_config = {
      .loop_count = 0,
  };

  return rmt_transmit(status_led_chan, status_led_encoder, led_data, sizeof(led_data), &tx_config);
}

/**
 * @brief LED animation task
 */
static void status_led_task(void *arg) {
  uint32_t counter = 0;

  while (1) {
    switch (current_state) {
    case STATUS_LED_BOOT: {
      // Fast pulsing white
      float brightness = (sinf(counter * 0.1f) + 1.0f) / 2.0f;
      uint8_t val      = (uint8_t)(brightness * 100);
      status_led_set_color(val, val, val);
      vTaskDelay(pdMS_TO_TICKS(20));
      break;
    }

    case STATUS_LED_WIFI_CONNECTING: {
      // Pulsing blue
      float brightness = (sinf(counter * 0.05f) + 1.0f) / 2.0f;
      uint8_t val      = (uint8_t)(brightness * 100);
      status_led_set_color(0, 0, val);
      vTaskDelay(pdMS_TO_TICKS(20));
      break;
    }

    case STATUS_LED_WIFI_AP: {
      // Solid orange
      status_led_set_color(255, 80, 0);
      vTaskDelay(pdMS_TO_TICKS(500));
      break;
    }

    case STATUS_LED_WIFI_STATION: {
      // Slow cyan/white alternation
      if ((counter % 40) < 20) {
        status_led_set_color(0, 100, 100); // Cyan
      } else {
        status_led_set_color(50, 50, 50); // Soft white
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      break;
    }

    case STATUS_LED_BLE_CONNECTED: {
      // Solid green
      status_led_set_color(0, 100, 0);
      vTaskDelay(pdMS_TO_TICKS(500));
      break;
    }

    case STATUS_LED_ESPNOW_PAIRING: {
      // Fast blinking blue
      if ((counter % 8) < 4) {
        status_led_set_color(0, 0, 150);
      } else {
        status_led_set_color(0, 0, 0);
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      break;
    }

    case STATUS_LED_CAN_ACTIVE: {
      // Slow pulsing purple
      float brightness = (sinf(counter * 0.02f) + 1.0f) / 2.0f;
      uint8_t val      = (uint8_t)(brightness * 80);
      status_led_set_color(val, 0, val);
      vTaskDelay(pdMS_TO_TICKS(50));
      break;
    }

    case STATUS_LED_ERROR: {
      // Fast blinking red
      if ((counter % 10) < 5) {
        status_led_set_color(255, 0, 0);
      } else {
        status_led_set_color(0, 0, 0);
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      break;
    }

    case STATUS_LED_IDLE: {
      // Slow pulsing yellow
      float brightness = (sinf(counter * 0.07f) + 1.0f) / 2.0f;
      uint8_t r        = (uint8_t)(brightness * 100);
      uint8_t g        = (uint8_t)(brightness * 80);
      status_led_set_color(r, g, 0); // Yellow
      vTaskDelay(pdMS_TO_TICKS(50));
      break;
    }

    case STATUS_LED_FACTORY_RESET: {
      // Fast red/white alternation
      if ((counter % 6) < 3) {
        status_led_set_color(255, 0, 0);
      } else {
        status_led_set_color(255, 255, 255);
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      break;
    }
    }
    counter++;
  }
}

esp_err_t status_led_init(void) {
#if STATUS_LED_GPIO < 0
  ESP_LOGI(TAG, "No status LED on this board");
  return ESP_ERR_NOT_SUPPORTED;
#endif

  ESP_LOGI(TAG, "Initializing status LED on GPIO %d", STATUS_LED_GPIO);

  // Configuration RMT
  rmt_tx_channel_config_t tx_chan_config = {
      .clk_src           = RMT_CLK_SRC_DEFAULT,
      .gpio_num          = STATUS_LED_GPIO,
      .mem_block_symbols = STATUS_LED_MEM_BLOCK_SYMBOLS,
      .resolution_hz     = STATUS_LED_RMT_RESOLUTION,
      .trans_queue_depth = 4,
  };

  esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &status_led_chan);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "RMT channel creation error: %s", esp_err_to_name(ret));
    return ret;
  }

  // Create the LED encoder
  led_strip_encoder_config_t encoder_config = {
      .resolution = STATUS_LED_RMT_RESOLUTION,
  };

  ret = rmt_new_led_strip_encoder(&encoder_config, &status_led_encoder);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Encoder creation error: %s", esp_err_to_name(ret));
    rmt_del_channel(status_led_chan);
    return ret;
  }

  ret = rmt_enable(status_led_chan);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "RMT activation error: %s", esp_err_to_name(ret));
    return ret;
  }

  led_initialized         = true;

  // Create the animation task
  BaseType_t task_created = xTaskCreatePinnedToCore(status_led_task, "status_led", 2048, NULL, 5, &status_led_task_handle, tskNO_AFFINITY);

  if (task_created != pdPASS) {
    ESP_LOGE(TAG, "LED task creation error");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Status LED initialized");
  return ESP_OK;
}

esp_err_t status_led_set_state(status_led_state_t state) {
  if (!led_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  current_state = state;
  ESP_LOGD(TAG, "LED state changed: %d", state);
  return ESP_OK;
}

status_led_state_t status_led_get_state(void) {
  return current_state;
}

esp_err_t status_led_off(void) {
  if (!led_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  return status_led_set_color(0, 0, 0);
}
