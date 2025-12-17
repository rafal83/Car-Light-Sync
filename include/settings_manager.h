#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define TAG_SETTINGS "Settings"

// Fichier de configuration centralisé
#define SETTINGS_FILE_PATH "/spiffs/config/settings.json"

// Structure pour la configuration système (stockée en JSON dans SPIFFS)
typedef struct {
  // Active profile ID
  int32_t active_profile_id;

  // LED Hardware
  uint16_t led_count;

  // Wheel control
  bool wheel_control_enabled;
  uint8_t wheel_control_speed_limit;

  // CAN Servers autostart
  bool gvret_autostart;
  bool canserver_autostart;

  // ESP-NOW
  uint8_t espnow_role;
  uint8_t espnow_type;

} system_settings_t;

/**
 * @brief Initialise le gestionnaire de paramètres
 * @return ESP_OK si succès
 */
esp_err_t settings_manager_init(void);

/**
 * @brief Charge les paramètres système depuis SPIFFS
 * @param settings Pointeur vers la structure à remplir
 * @return ESP_OK si succès
 */
esp_err_t settings_manager_load(system_settings_t *settings);

/**
 * @brief Sauvegarde les paramètres système vers SPIFFS
 * @param settings Pointeur vers la structure à sauvegarder
 * @return ESP_OK si succès
 */
esp_err_t settings_manager_save(const system_settings_t *settings);

/**
 * @brief Obtient une valeur int32
 */
int32_t settings_get_i32(const char *key, int32_t default_value);

/**
 * @brief Définit une valeur int32
 */
esp_err_t settings_set_i32(const char *key, int32_t value);

/**
 * @brief Obtient une valeur uint8
 */
uint8_t settings_get_u8(const char *key, uint8_t default_value);

/**
 * @brief Définit une valeur uint8
 */
esp_err_t settings_set_u8(const char *key, uint8_t value);

/**
 * @brief Obtient une valeur uint16
 */
uint16_t settings_get_u16(const char *key, uint16_t default_value);

/**
 * @brief Définit une valeur uint16
 */
esp_err_t settings_set_u16(const char *key, uint16_t value);

/**
 * @brief Obtient une valeur bool
 */
bool settings_get_bool(const char *key, bool default_value);

/**
 * @brief Définit une valeur bool
 */
esp_err_t settings_set_bool(const char *key, bool value);

/**
 * @brief Efface tous les paramètres (factory reset)
 */
esp_err_t settings_manager_clear(void);

#endif // SETTINGS_MANAGER_H
