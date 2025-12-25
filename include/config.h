#ifndef CONFIG_H
#define CONFIG_H

#include "sdkconfig.h"

#include <stdint.h>

// Configuration GPIO
#define NUM_LEDS 122

#ifdef CONFIG_IDF_TARGET_ESP32C6
// Configuration ESP32-C6
#define LED_PIN 5

// I2S pins for INMP441 microphone
#define I2S_WS_PIN 20
#define I2S_SCK_PIN 19
#define I2S_SD_PIN 18

// Pins TWAI (CAN)
// Note: On ESP32-C6, TWAI pins can be mapped to any
// GPIO
#define CAN_TX_BODY_PIN 15
#define CAN_RX_BODY_PIN 14
#define CAN_TX_CHASSIS_PIN 7
#define CAN_RX_CHASSIS_PIN 6

#else
// Configuration ESP32-S3 (default)
#define LED_PIN 5

// I2S pins for INMP441 microphone
#define I2S_WS_PIN 13
#define I2S_SCK_PIN 12
#define I2S_SD_PIN 11

// Pins TWAI (CAN)
#define CAN_TX_CHASSIS_PIN 10
#define CAN_RX_CHASSIS_PIN 9
#define CAN_TX_BODY_PIN 8
#define CAN_RX_BODY_PIN 7

#endif

// Configuration WiFi
#define WIFI_AP_SSID_BASE "CarLightSync"
#define WIFI_AP_SSID "CarLightSync" // Will be replaced dynamically
#define WIFI_AP_PASSWORD ""         // Open network (no password)
#define WIFI_MAX_CLIENTS 4

// Buffer for names with MAC suffix
extern char g_device_name_with_suffix[32];
extern char g_wifi_ssid_with_suffix[32];

// Function to initialize names with MAC suffix
void config_init_device_names(void);

// Configuration CAN
#define CAN_UPDATE_RATE_MS 100

// Configuration Web Server
#define WEB_SERVER_PORT 80

// Configuration effets
#define DEFAULT_BRIGHTNESS 128
#define DEFAULT_SPEED 50
#define MAX_BRIGHTNESS 255

// Hardware LED configuration
/**
 * Maximum number of LEDs supported by the system.
 * Limited to 200 to avoid memory issues on ESP32.
 */
#define MAX_LED_COUNT 200

#endif // CONFIG_H
