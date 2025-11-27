#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_wifi.h"
#include <stdbool.h>

#define TAG_WIFI "WiFi"

// État de la connexion
typedef struct {
  bool ap_started;
  bool sta_connected;
  char sta_ssid[33];
  char sta_ip[16];
  char ap_ip[16];
  uint8_t connected_clients;
} wifi_status_t;

/**
 * @brief Initialise le module WiFi
 * @return ESP_OK si succès
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Démarre le point d'accès WiFi
 * @return ESP_OK si succès
 */
esp_err_t wifi_manager_start_ap(void);

/**
 * @brief Se connecte à un réseau WiFi
 * @param ssid SSID du réseau
 * @param password Mot de passe
 * @return ESP_OK si succès
 */
esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password);

/**
 * @brief Déconnecte du réseau WiFi
 * @return ESP_OK si succès
 */
esp_err_t wifi_manager_disconnect_sta(void);

/**
 * @brief Scanne les réseaux WiFi disponibles
 * @param scan_time Temps de scan en ms
 * @return Nombre de réseaux trouvés
 */
int wifi_manager_scan_networks(uint32_t scan_time);

/**
 * @brief Obtient l'état actuel du WiFi
 * @param status Pointeur vers la structure de statut
 * @return ESP_OK si succès
 */
esp_err_t wifi_manager_get_status(wifi_status_t *status);

/**
 * @brief Arrête complètement le WiFi
 * @return ESP_OK si succès
 */
esp_err_t wifi_manager_stop(void);

/**
 * @brief Change le mode WiFi
 * @param mode Nouveau mode
 * @return ESP_OK si succès
 */
esp_err_t wifi_manager_set_mode(wifi_mode_t mode);

bool wifi_wait_for_sta(uint32_t timeout_ms);

#endif // WIFI_MANAGER_H
