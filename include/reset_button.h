#ifndef RESET_BUTTON_H
#define RESET_BUTTON_H

#include "esp_err.h"

// GPIO du bouton reset
#define RESET_BUTTON_GPIO 4

// Press duration for factory reset (5 seconds)
#define RESET_BUTTON_HOLD_TIME_MS 5000

/**
 * @brief Initializes the reset button
 */
esp_err_t reset_button_init(void);

/**
 * @brief Triggers a factory reset
 */
void reset_button_factory_reset(void);

#endif // RESET_BUTTON_H
