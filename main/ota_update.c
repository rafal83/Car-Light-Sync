#include "ota_update.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_app_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "OTA";

// Version du firmware (à mettre à jour à chaque version)
#define FIRMWARE_VERSION "2.1.0"

static ota_progress_t current_progress = {0};
static esp_ota_handle_t ota_handle = 0;
static const esp_partition_t *update_partition = NULL;
static bool ota_in_progress = false;

esp_err_t ota_init(void) {
    memset(&current_progress, 0, sizeof(ota_progress_t));
    current_progress.state = OTA_STATE_IDLE;

    ESP_LOGI(TAG, "OTA initialisé, version: %s", FIRMWARE_VERSION);
    return ESP_OK;
}

esp_err_t ota_begin(void) {
    if (ota_in_progress) {
        ESP_LOGW(TAG, "OTA déjà en cours");
        return ESP_ERR_INVALID_STATE;
    }

    // Réinitialiser la progression
    memset(&current_progress, 0, sizeof(ota_progress_t));
    current_progress.state = OTA_STATE_RECEIVING;

    // Obtenir la partition de mise à jour
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Impossible de trouver la partition OTA");
        strncpy(current_progress.error_msg, "Partition OTA non trouvée", sizeof(current_progress.error_msg) - 1);
        current_progress.state = OTA_STATE_ERROR;
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Début OTA vers partition: %s à 0x%08lx",
             update_partition->label, update_partition->address);

    // Démarrer l'OTA
    esp_err_t ret = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur esp_ota_begin: %s", esp_err_to_name(ret));
        snprintf(current_progress.error_msg, sizeof(current_progress.error_msg),
                 "Erreur démarrage OTA: %s", esp_err_to_name(ret));
        current_progress.state = OTA_STATE_ERROR;
        return ret;
    }

    ota_in_progress = true;
    ESP_LOGI(TAG, "OTA démarré avec succès");
    return ESP_OK;
}

esp_err_t ota_write(const void *data, size_t size) {
    if (!ota_in_progress) {
        ESP_LOGE(TAG, "OTA non démarré");
        return ESP_ERR_INVALID_STATE;
    }

    current_progress.state = OTA_STATE_WRITING;

    esp_err_t ret = esp_ota_write(ota_handle, data, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur esp_ota_write: %s", esp_err_to_name(ret));
        snprintf(current_progress.error_msg, sizeof(current_progress.error_msg),
                 "Erreur écriture OTA: %s", esp_err_to_name(ret));
        current_progress.state = OTA_STATE_ERROR;
        ota_in_progress = false;
        return ret;
    }

    current_progress.written_size += size;

    // Calculer le pourcentage si on connaît la taille totale
    if (current_progress.total_size > 0) {
        current_progress.progress = (current_progress.written_size * 100) / current_progress.total_size;
        if (current_progress.progress > 100) {
            current_progress.progress = 100;
        }
    }

    ESP_LOGD(TAG, "OTA écrit %d octets, total: %lu", size, current_progress.written_size);
    return ESP_OK;
}

esp_err_t ota_end(void) {
    if (!ota_in_progress) {
        ESP_LOGE(TAG, "OTA non démarré");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Validation de l'image échouée");
            strncpy(current_progress.error_msg, "Validation du firmware échouée",
                   sizeof(current_progress.error_msg) - 1);
        } else {
            ESP_LOGE(TAG, "Erreur esp_ota_end: %s", esp_err_to_name(ret));
            snprintf(current_progress.error_msg, sizeof(current_progress.error_msg),
                     "Erreur fin OTA: %s", esp_err_to_name(ret));
        }
        current_progress.state = OTA_STATE_ERROR;
        ota_in_progress = false;
        return ret;
    }

    // Définir la nouvelle partition comme partition de boot
    ret = esp_ota_set_boot_partition(update_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur esp_ota_set_boot_partition: %s", esp_err_to_name(ret));
        snprintf(current_progress.error_msg, sizeof(current_progress.error_msg),
                 "Erreur configuration boot: %s", esp_err_to_name(ret));
        current_progress.state = OTA_STATE_ERROR;
        ota_in_progress = false;
        return ret;
    }

    current_progress.state = OTA_STATE_SUCCESS;
    current_progress.progress = 100;
    ota_in_progress = false;

    ESP_LOGI(TAG, "OTA terminé avec succès! Redémarrage nécessaire.");
    return ESP_OK;
}

void ota_abort(void) {
    if (ota_in_progress) {
        esp_ota_abort(ota_handle);
        ota_in_progress = false;
        current_progress.state = OTA_STATE_IDLE;
        ESP_LOGW(TAG, "OTA annulé");
    }
}

void ota_get_progress(ota_progress_t *progress) {
    if (progress != NULL) {
        memcpy(progress, &current_progress, sizeof(ota_progress_t));
    }
}

const char* ota_get_current_version(void) {
    return FIRMWARE_VERSION;
}

void ota_restart(void) {
    ESP_LOGI(TAG, "Redémarrage dans 2 secondes...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

esp_err_t ota_validate_current_partition(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // Première exécution après une mise à jour, valider la nouvelle image
            esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Partition OTA validée");
            } else {
                ESP_LOGE(TAG, "Erreur validation partition: %s", esp_err_to_name(ret));
            }
            return ret;
        }
    }

    return ESP_OK;
}
