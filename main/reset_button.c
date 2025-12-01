#include "reset_button.h"

#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "status_led.h"
#include "status_manager.h"

static const char *TAG                       = "ResetButton";
static TaskHandle_t reset_button_task_handle = NULL;

/**
 * @brief Tâche de surveillance du bouton reset
 */
static void reset_button_task(void *arg) {
  uint32_t press_start_time = 0;
  bool button_pressed       = false;
  bool reset_led_shown      = false;

  while (1) {
    int level = gpio_get_level(RESET_BUTTON_GPIO);

    if (level == 0) { // Bouton appuyé (pull-up, donc 0 = appuyé)
      if (!button_pressed) {
        button_pressed   = true;
        reset_led_shown  = false;
        press_start_time = esp_log_timestamp();
        ESP_LOGI(TAG, "Bouton reset appuyé");
      } else {
        uint32_t press_duration = esp_log_timestamp() - press_start_time;

        // Afficher la LED de reset dès 1 seconde d'appui
        if (press_duration >= 1000 && !reset_led_shown) {
          ESP_LOGI(TAG, "Appui prolongé détecté, LED reset activée");
          status_led_set_state(STATUS_LED_FACTORY_RESET);
          reset_led_shown = true;
        }

        if (press_duration >= RESET_BUTTON_HOLD_TIME_MS) {
          ESP_LOGW(TAG, "Bouton reset maintenu pendant %lu ms, factory reset!", press_duration);
          reset_button_factory_reset();
          // Ne devrait jamais arriver ici car factory_reset redémarre
        }
      }
    } else {
      if (button_pressed) {
        uint32_t press_duration = esp_log_timestamp() - press_start_time;
        ESP_LOGI(TAG, "Bouton reset relâché après %lu ms", press_duration);
        button_pressed  = false;
        reset_led_shown = false;
        // Restaurer l'état de la LED immédiatement si le bouton est relâché
        // avant 5s
        if (press_duration < RESET_BUTTON_HOLD_TIME_MS) {
          ESP_LOGI(TAG, "Restauration de l'état LED normal");
          status_manager_update_led_now();
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // Vérifier toutes les 100ms
  }
}

esp_err_t reset_button_init(void) {
  ESP_LOGI(TAG, "Initialisation bouton reset sur GPIO %d", RESET_BUTTON_GPIO);

  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << RESET_BUTTON_GPIO),
      .mode         = GPIO_MODE_INPUT,
      .pull_up_en   = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type    = GPIO_INTR_DISABLE,
  };

  esp_err_t err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Erreur configuration GPIO bouton reset: %s", esp_err_to_name(err));
    return err;
  }

  // Créer la tâche de surveillance
  BaseType_t task_created = xTaskCreatePinnedToCore(reset_button_task, "reset_button", 2048, NULL, 5, &reset_button_task_handle, tskNO_AFFINITY);

  if (task_created != pdPASS) {
    ESP_LOGE(TAG, "Erreur création tâche bouton reset");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Bouton reset initialisé (appui 5s = factory reset)");
  return ESP_OK;
}

void reset_button_factory_reset(void) {
  ESP_LOGW(TAG, "========================================");
  ESP_LOGW(TAG, "   FACTORY RESET - EFFACEMENT NVS");
  ESP_LOGW(TAG, "========================================");

  // Changer la LED en mode reset
  status_led_set_state(STATUS_LED_FACTORY_RESET);

  // Effacer tout le NVS
  esp_err_t err = nvs_flash_erase();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Erreur effacement NVS: %s", esp_err_to_name(err));
    status_led_set_state(STATUS_LED_ERROR);
  } else {
    ESP_LOGI(TAG, "NVS effacé avec succès");
  }

  // Attendre pour que les logs s'affichent
  vTaskDelay(pdMS_TO_TICKS(1000));

  // Redémarrer l'ESP32
  ESP_LOGW(TAG, "Redémarrage dans 2 secondes...");
  vTaskDelay(pdMS_TO_TICKS(2000));
  esp_restart();
}
