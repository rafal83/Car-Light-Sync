#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "esp_err.h"
#include <stdbool.h>

// États de la LED de statut
typedef enum {
    STATUS_LED_BOOT,           // Démarrage (blanc pulsant)
    STATUS_LED_WIFI_CONNECTING, // Connexion WiFi (bleu pulsant)
    STATUS_LED_WIFI_AP,        // Mode AP (orange fixe)
    STATUS_LED_WIFI_STATION,   // WiFi connecté (cyan/blanc alternance lente)
    STATUS_LED_BLE_CONNECTED,  // BLE connecté (vert fixe)
    STATUS_LED_CAN_ACTIVE,     // CAN actif (violet pulsant lent)
    STATUS_LED_ERROR,          // Erreur (rouge clignotant rapide)
    STATUS_LED_IDLE,           // Idle/Aucune connexion (jaune pulsant lent)
    STATUS_LED_FACTORY_RESET   // Reset en cours (rouge/blanc alternance rapide)
} status_led_state_t;

/**
 * @brief Initialise la LED de statut
 */
esp_err_t status_led_init(void);

/**
 * @brief Change l'état de la LED
 */
esp_err_t status_led_set_state(status_led_state_t state);

/**
 * @brief Obtient l'état actuel
 */
status_led_state_t status_led_get_state(void);

/**
 * @brief Éteint la LED
 */
esp_err_t status_led_off(void);

#endif // STATUS_LED_H
