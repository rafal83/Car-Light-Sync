#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "esp_err.h"

#include <stdbool.h>

// Status LED states
typedef enum {
  STATUS_LED_BOOT,            // Boot (white pulsing)
  STATUS_LED_WIFI_CONNECTING, // WiFi connecting (blue pulsing)
  STATUS_LED_WIFI_AP,         // AP mode (solid orange)
  STATUS_LED_WIFI_STATION,    // WiFi connected (cyan/white slow alternation)
  STATUS_LED_BLE_CONNECTED,   // BLE connected (solid green)
  STATUS_LED_ESPNOW_PAIRING,  // ESP-NOW pairing (blue fast blinking)
  STATUS_LED_CAN_ACTIVE,      // CAN active (purple slow pulsing)
  STATUS_LED_ERROR,           // Error (red fast blinking)
  STATUS_LED_IDLE,            // Idle/No connection (yellow slow pulsing)
  STATUS_LED_FACTORY_RESET    // Reset in progress (red/white fast alternation)
} status_led_state_t;

/**
 * @brief Initializes the status LED
 */
esp_err_t status_led_init(void);

/**
 * @brief Changes the LED state
 */
esp_err_t status_led_set_state(status_led_state_t state);

/**
 * @brief Gets the current state
 */
status_led_state_t status_led_get_state(void);

/**
 * @brief Turns off the LED
 */
esp_err_t status_led_off(void);

#endif // STATUS_LED_H
