#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * @brief État de la mise à jour OTA
 */
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_RECEIVING,
    OTA_STATE_WRITING,
    OTA_STATE_SUCCESS,
    OTA_STATE_ERROR
} ota_state_t;

/**
 * @brief Informations sur la progression OTA
 */
typedef struct {
    ota_state_t state;
    uint32_t total_size;
    uint32_t written_size;
    uint8_t progress;       // Pourcentage 0-100
    char error_msg[128];
} ota_progress_t;

/**
 * @brief Initialise le système OTA
 * @return ESP_OK si succès
 */
esp_err_t ota_init(void);

/**
 * @brief Démarre une mise à jour OTA
 * @return ESP_OK si succès
 */
esp_err_t ota_begin(size_t total_size);

/**
 * @brief Écrit des données de firmware
 * @param data Pointeur vers les données
 * @param size Taille des données
 * @return ESP_OK si succès
 */
esp_err_t ota_write(const void *data, size_t size);

/**
 * @brief Termine la mise à jour OTA
 * @return ESP_OK si succès
 */
esp_err_t ota_end(void);

/**
 * @brief Annule la mise à jour OTA en cours
 */
void ota_abort(void);

/**
 * @brief Obtient la progression actuelle
 * @param progress Pointeur vers la structure de progression
 */
void ota_get_progress(ota_progress_t *progress);

/**
 * @brief Obtient la version actuelle du firmware
 * @return Chaîne de version
 */
const char* ota_get_current_version(void);

/**
 * @brief Redémarre l'ESP32
 */
void ota_restart(void);

/**
 * @brief Valide la partition OTA actuelle
 * @return ESP_OK si succès
 */
esp_err_t ota_validate_current_partition(void);

/**
 * @brief Retourne le nombre de secondes restantes avant redémarrage auto
 * @return Compte à rebours (>=0) ou -1 si aucun redémarrage planifié
 */
int ota_get_reboot_countdown(void);

#endif // OTA_UPDATE_H
