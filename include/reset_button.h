#ifndef RESET_BUTTON_H
#define RESET_BUTTON_H

#include "esp_err.h"

// GPIO du bouton reset
#define RESET_BUTTON_GPIO 4

// Durée d'appui pour factory reset (5 secondes)
#define RESET_BUTTON_HOLD_TIME_MS 5000

/**
 * @brief Initialise le bouton reset
 */
esp_err_t reset_button_init(void);

/**
 * @brief Déclenche un factory reset
 */
void reset_button_factory_reset(void);

#endif // RESET_BUTTON_H
