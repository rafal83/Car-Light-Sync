#pragma once

#include "driver/rmt_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAG_LED_ENCODER "led_encoder"

/**
 * @brief Configuration pour l'encodeur LED strip
 */
typedef struct {
  uint32_t resolution; // Résolution de l'horloge RMT en Hz
} led_strip_encoder_config_t;

/**
 * @brief Créer un encodeur RMT pour LED strip (WS2812)
 *
 * @param[in] config Configuration de l'encodeur
 * @param[out] ret_encoder Handle de l'encodeur créé
 * @return
 *      - ESP_OK: Encodeur créé avec succès
 *      - ESP_ERR_INVALID_ARG: Argument invalide
 *      - ESP_ERR_NO_MEM: Pas assez de mémoire
 */
esp_err_t rmt_new_led_strip_encoder(const led_strip_encoder_config_t *config,
                                    rmt_encoder_handle_t *ret_encoder);

#ifdef __cplusplus
}
#endif
