#include "status_led.h"

#include "config.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip_encoder.h"

#include <math.h>

// GPIO pour la LED de statut (WS2812 intégrée)
#if CONFIG_IDF_TARGET_ESP32S3
#define STATUS_LED_GPIO 21
#elif CONFIG_IDF_TARGET_ESP32C6
#define STATUS_LED_GPIO 8
#else
#define STATUS_LED_GPIO -1 // Pas de LED sur les autres boards
#endif

#define STATUS_LED_RMT_RESOLUTION 10000000 // 10MHz

static const char *TAG                         = "StatusLED";
static rmt_channel_handle_t status_led_chan    = NULL;
static rmt_encoder_handle_t status_led_encoder = NULL;
static TaskHandle_t status_led_task_handle     = NULL;
static status_led_state_t current_state        = STATUS_LED_BOOT;
static bool led_initialized                    = false;

// Structure pour définir une couleur RGB
typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} rgb_t;

/**
 * @brief Envoie une couleur RGB à la LED
 */
static esp_err_t status_led_set_color(uint8_t r, uint8_t g, uint8_t b) {
  if (!led_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t led_data[3]             = {g, r, b}; // WS2812 format: GRB

  rmt_transmit_config_t tx_config = {
      .loop_count = 0,
  };

  return rmt_transmit(status_led_chan, status_led_encoder, led_data, sizeof(led_data), &tx_config);
}

/**
 * @brief Convertit HSV en RGB
 */
static rgb_t hsv_to_rgb(float h, float s, float v) {
  rgb_t rgb;
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float r, g, b;

  if (h < 60) {
    r = c;
    g = x;
    b = 0;
  } else if (h < 120) {
    r = x;
    g = c;
    b = 0;
  } else if (h < 180) {
    r = 0;
    g = c;
    b = x;
  } else if (h < 240) {
    r = 0;
    g = x;
    b = c;
  } else if (h < 300) {
    r = x;
    g = 0;
    b = c;
  } else {
    r = c;
    g = 0;
    b = x;
  }

  rgb.r = (uint8_t)((r + m) * 255);
  rgb.g = (uint8_t)((g + m) * 255);
  rgb.b = (uint8_t)((b + m) * 255);

  return rgb;
}

/**
 * @brief Tâche d'animation de la LED
 */
static void status_led_task(void *arg) {
  uint32_t counter = 0;

  while (1) {
    switch (current_state) {
    case STATUS_LED_BOOT: {
      // Blanc pulsant rapide
      float brightness = (sinf(counter * 0.1f) + 1.0f) / 2.0f;
      uint8_t val      = (uint8_t)(brightness * 100);
      status_led_set_color(val, val, val);
      vTaskDelay(pdMS_TO_TICKS(20));
      break;
    }

    case STATUS_LED_WIFI_CONNECTING: {
      // Bleu pulsant
      float brightness = (sinf(counter * 0.05f) + 1.0f) / 2.0f;
      uint8_t val      = (uint8_t)(brightness * 100);
      status_led_set_color(0, 0, val);
      vTaskDelay(pdMS_TO_TICKS(20));
      break;
    }

    case STATUS_LED_WIFI_AP: {
      // Orange fixe
      status_led_set_color(255, 80, 0);
      vTaskDelay(pdMS_TO_TICKS(500));
      break;
    }

    case STATUS_LED_WIFI_STATION: {
      // Cyan/blanc alternance lente
      if ((counter % 40) < 20) {
        status_led_set_color(0, 100, 100); // Cyan
      } else {
        status_led_set_color(50, 50, 50); // Blanc doux
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      break;
    }

    case STATUS_LED_BLE_CONNECTED: {
      // Vert fixe
      status_led_set_color(0, 100, 0);
      vTaskDelay(pdMS_TO_TICKS(500));
      break;
    }

    case STATUS_LED_CAN_ACTIVE: {
      // Violet pulsant lent
      float brightness = (sinf(counter * 0.02f) + 1.0f) / 2.0f;
      uint8_t val      = (uint8_t)(brightness * 80);
      status_led_set_color(val, 0, val);
      vTaskDelay(pdMS_TO_TICKS(50));
      break;
    }

    case STATUS_LED_ERROR: {
      // Rouge clignotant rapide
      if ((counter % 10) < 5) {
        status_led_set_color(255, 0, 0);
      } else {
        status_led_set_color(0, 0, 0);
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      break;
    }

    case STATUS_LED_IDLE: {
      // Jaune pulsant lent
      float brightness = (sinf(counter * 0.07f) + 1.0f) / 2.0f;
      uint8_t r        = (uint8_t)(brightness * 100);
      uint8_t g        = (uint8_t)(brightness * 80);
      status_led_set_color(r, g, 0); // Jaune
      vTaskDelay(pdMS_TO_TICKS(50));
      break;
    }

    case STATUS_LED_FACTORY_RESET: {
      // Rouge/blanc alternance rapide
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
  ESP_LOGI(TAG, "Pas de LED de statut sur cette board");
  return ESP_ERR_NOT_SUPPORTED;
#endif

  ESP_LOGI(TAG, "Initialisation LED statut sur GPIO %d", STATUS_LED_GPIO);

  // Configuration RMT
  rmt_tx_channel_config_t tx_chan_config = {
      .clk_src           = RMT_CLK_SRC_DEFAULT,
      .gpio_num          = STATUS_LED_GPIO,
      .mem_block_symbols = 64,
      .resolution_hz     = STATUS_LED_RMT_RESOLUTION,
      .trans_queue_depth = 4,
  };

  esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &status_led_chan);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Erreur création canal RMT: %s", esp_err_to_name(ret));
    return ret;
  }

  // Créer l'encodeur LED
  led_strip_encoder_config_t encoder_config = {
      .resolution = STATUS_LED_RMT_RESOLUTION,
  };

  ret = rmt_new_led_strip_encoder(&encoder_config, &status_led_encoder);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Erreur création encodeur: %s", esp_err_to_name(ret));
    rmt_del_channel(status_led_chan);
    return ret;
  }

  ret = rmt_enable(status_led_chan);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Erreur activation RMT: %s", esp_err_to_name(ret));
    return ret;
  }

  led_initialized         = true;

  // Créer la tâche d'animation
  BaseType_t task_created = xTaskCreatePinnedToCore(status_led_task, "status_led", 2048, NULL, 5, &status_led_task_handle, tskNO_AFFINITY);

  if (task_created != pdPASS) {
    ESP_LOGE(TAG, "Erreur création tâche LED");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "LED de statut initialisée");
  return ESP_OK;
}

esp_err_t status_led_set_state(status_led_state_t state) {
  if (!led_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  current_state = state;
  ESP_LOGD(TAG, "État LED changé: %d", state);
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
