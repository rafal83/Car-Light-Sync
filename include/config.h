#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// Configuration GPIO
#define LED_PIN 5
#define NUM_LEDS 94
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

// Configuration WiFi
#define WIFI_AP_SSID_BASE "Car-Light-Sync"
#define WIFI_AP_SSID "Car-Light-Sync" // Sera remplacé dynamiquement
#define WIFI_AP_PASSWORD ""           // Réseau ouvert (pas de mot de passe)
#define WIFI_MAX_CLIENTS 4

// Buffer pour les noms avec suffixe MAC
extern char g_device_name_with_suffix[32];
extern char g_wifi_ssid_with_suffix[32];

// Fonction pour initialiser les noms avec suffixe MAC
void config_init_device_names(void);

// Configuration CAN
#define CAN_UPDATE_RATE_MS 100

// Configuration Web Server
#define WEB_SERVER_PORT 80

// Configuration effets
#define DEFAULT_BRIGHTNESS 128
#define DEFAULT_SPEED 50
#define MAX_BRIGHTNESS 255

// Pins disponibles pour futures extensions
#define DOOR_SENSOR_PIN -1   // Non utilisé par défaut
#define MOTION_SENSOR_PIN -1 // Non utilisé par défaut

#endif // CONFIG_H
