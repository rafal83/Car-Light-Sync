#ifndef SPIFFS_STORAGE_H
#define SPIFFS_STORAGE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TAG_SPIFFS "SPIFFS"

/**
 * @brief Initialise le système de fichiers SPIFFS
 * @return ESP_OK si succès
 */
esp_err_t spiffs_storage_init(void);

/**
 * @brief Sauvegarde des données JSON dans un fichier
 * @param path Chemin du fichier (ex: "/spiffs/config/led_count.json")
 * @param json_string Chaîne JSON à sauvegarder
 * @return ESP_OK si succès
 */
esp_err_t spiffs_save_json(const char *path, const char *json_string);

/**
 * @brief Charge des données JSON depuis un fichier
 * @param path Chemin du fichier
 * @param buffer Buffer pour stocker le JSON
 * @param buffer_size Taille du buffer
 * @return ESP_OK si succès, ESP_ERR_NOT_FOUND si fichier inexistant
 */
esp_err_t spiffs_load_json(const char *path, char *buffer, size_t buffer_size);

/**
 * @brief Supprime un fichier
 * @param path Chemin du fichier
 * @return ESP_OK si succès
 */
esp_err_t spiffs_delete_file(const char *path);

/**
 * @brief Vérifie si un fichier existe
 * @param path Chemin du fichier
 * @return true si le fichier existe
 */
bool spiffs_file_exists(const char *path);

/**
 * @brief Obtient la taille d'un fichier
 * @param path Chemin du fichier
 * @return Taille en bytes, -1 si erreur
 */
int spiffs_get_file_size(const char *path);

/**
 * @brief Obtient les statistiques du système de fichiers
 * @param total Pointeur pour stocker la taille totale
 * @param used Pointeur pour stocker l'espace utilisé
 * @return ESP_OK si succès
 */
esp_err_t spiffs_get_stats(size_t *total, size_t *used);

/**
 * @brief Formate la partition SPIFFS (EFFACE TOUT!)
 * @return ESP_OK si succès
 */
esp_err_t spiffs_format(void);

/**
 * @brief Sauvegarde des données binaires dans un fichier
 * @param path Chemin du fichier
 * @param data Pointeur vers les données
 * @param size Taille des données
 * @return ESP_OK si succès
 */
esp_err_t spiffs_save_blob(const char *path, const void *data, size_t size);

/**
 * @brief Charge des données binaires depuis un fichier
 * @param path Chemin du fichier
 * @param buffer Buffer pour stocker les données
 * @param buffer_size Taille du buffer (en entrée: taille max, en sortie: taille lue)
 * @return ESP_OK si succès
 */
esp_err_t spiffs_load_blob(const char *path, void *buffer, size_t *buffer_size);

#endif // SPIFFS_STORAGE_H
