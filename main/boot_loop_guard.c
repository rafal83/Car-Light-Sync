#include "boot_loop_guard.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "reset_button.h"

#include <string.h>

static const char *TAG = "BootLoopGuard";

/**
 * @brief Structure stockée en LP SRAM (RTC Fast Memory)
 *
 * Cette mémoire persiste pendant le deep sleep et les redémarrages,
 * mais est effacée lors d'une mise hors tension complète.
 */
typedef struct {
  uint32_t magic;        // Signature pour valider les données
  uint32_t boot_count;   // Compteur de boots consécutifs
  uint64_t last_boot_us; // Timestamp du dernier boot (en microsecondes)
} boot_loop_data_t;

// Stocker en RTC Fast Memory (LP SRAM) avec l'attribut RTC_NOINIT_ATTR
// pour que les données persistent entre les redémarrages
static RTC_NOINIT_ATTR boot_loop_data_t s_boot_data;

#define BOOT_LOOP_MAGIC 0xB007C0DE  // Signature magique pour valider les données

static TaskHandle_t watchdog_task_handle = NULL;

/**
 * @brief Tâche qui surveille le bon fonctionnement et réinitialise le compteur
 */
static void boot_watchdog_task(void *pvParameters) {
  ESP_LOGI(TAG, "Watchdog de boot démarré, attente de %d ms avant de marquer le boot comme réussi",
           BOOT_LOOP_SUCCESS_TIMEOUT_MS);

  vTaskDelay(pdMS_TO_TICKS(BOOT_LOOP_SUCCESS_TIMEOUT_MS));

  boot_loop_guard_mark_success();

  // Tâche terminée
  watchdog_task_handle = NULL;
  vTaskDelete(NULL);
}

esp_err_t boot_loop_guard_init(void) {
  uint64_t current_time_us = esp_timer_get_time();

  // Vérifier si les données en RTC sont valides
  if (s_boot_data.magic != BOOT_LOOP_MAGIC) {
    ESP_LOGI(TAG, "Première initialisation ou reset complet détecté, initialisation du compteur");
    s_boot_data.magic = BOOT_LOOP_MAGIC;
    s_boot_data.boot_count = 0;
    s_boot_data.last_boot_us = current_time_us;
  }

  // Incrémenter le compteur de boot
  s_boot_data.boot_count++;
  uint64_t time_since_last_boot_us = current_time_us - s_boot_data.last_boot_us;
  uint32_t time_since_last_boot_ms = (uint32_t)(time_since_last_boot_us / 1000);

  s_boot_data.last_boot_us = current_time_us;

  ESP_LOGI(TAG, "Boot count: %lu (temps depuis dernier boot: %lu ms)",
           s_boot_data.boot_count, time_since_last_boot_ms);

  // Vérifier si on est dans une boot loop
  if (s_boot_data.boot_count >= BOOT_LOOP_MAX_COUNT) {
    ESP_LOGE(TAG, "========================================");
    ESP_LOGE(TAG, "   BOOT LOOP DÉTECTÉ !");
    ESP_LOGE(TAG, "   %lu redémarrages consécutifs", s_boot_data.boot_count);
    ESP_LOGE(TAG, "   FACTORY RESET AUTOMATIQUE");
    ESP_LOGE(TAG, "========================================");

    // Attendre un peu pour que les logs s'affichent
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Réinitialiser le compteur avant le factory reset
    s_boot_data.boot_count = 0;

    // Déclencher un factory reset
    reset_button_factory_reset();

    // Ne devrait jamais arriver ici
    return ESP_FAIL;
  }

  // Si le temps depuis le dernier boot est long (> timeout),
  // c'est probablement un boot normal, pas une loop
  if (time_since_last_boot_ms > BOOT_LOOP_SUCCESS_TIMEOUT_MS) {
    ESP_LOGI(TAG, "Temps depuis dernier boot > timeout, reset du compteur");
    s_boot_data.boot_count = 1;
  }

  // Créer une tâche watchdog qui marquera le boot comme réussi après le timeout
  BaseType_t task_created = xTaskCreate(
    boot_watchdog_task,
    "boot_watchdog",
    2048,
    NULL,
    1,  // Priorité basse
    &watchdog_task_handle
  );

  if (task_created != pdPASS) {
    ESP_LOGE(TAG, "Erreur création tâche watchdog");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Protection boot loop initialisée (seuil: %d redémarrages)", BOOT_LOOP_MAX_COUNT);
  return ESP_OK;
}

void boot_loop_guard_mark_success(void) {
  if (s_boot_data.boot_count > 0) {
    ESP_LOGI(TAG, "Boot réussi après %lu tentatives, réinitialisation du compteur", s_boot_data.boot_count);
    s_boot_data.boot_count = 0;
  }
}

uint32_t boot_loop_guard_get_count(void) {
  return s_boot_data.boot_count;
}
