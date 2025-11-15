#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Initialise le serveur web
 * @return ESP_OK si succès
 */
esp_err_t web_server_init(void);

/**
 * @brief Démarre le serveur web
 * @return ESP_OK si succès
 */
esp_err_t web_server_start(void);

/**
 * @brief Arrête le serveur web
 * @return ESP_OK si succès
 */
esp_err_t web_server_stop(void);

/**
 * @brief Vérifie si le serveur web est actif
 * @return true si actif
 */
bool web_server_is_running(void);

#endif // WEB_SERVER_H
