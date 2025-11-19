#ifndef CONFIG_H
#define CONFIG_H

// Configuration GPIO
#define LED_PIN             5
#define NUM_LEDS            94
#define LED_TYPE            WS2812B
#define COLOR_ORDER         GRB

// Configuration WiFi
#define WIFI_AP_SSID        "Tesla-Strip"
#define WIFI_AP_PASSWORD    "tesla123"
#define WIFI_MAX_CLIENTS    4

// Configuration CAN
#define CAN_UPDATE_RATE_MS  100

// Configuration Web Server
#define WEB_SERVER_PORT     80

// Configuration effets
#define DEFAULT_BRIGHTNESS  128
#define DEFAULT_SPEED       50
#define MAX_BRIGHTNESS      255

// Pins disponibles pour futures extensions
#define DOOR_SENSOR_PIN     -1  // Non utilisé par défaut
#define MOTION_SENSOR_PIN   -1  // Non utilisé par défaut

#endif // CONFIG_H
