#pragma once

#include "driver/rmt_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAG_LED_ENCODER "led_encoder"

/**
 * @brief Configuration for LED strip encoder
 */
typedef struct {
  uint32_t resolution; // RMT clock resolution in Hz
} led_strip_encoder_config_t;

/**
 * @brief Create RMT encoder for LED strip (WS2812)
 *
 * @param[in] config Encoder configuration
 * @param[out] ret_encoder Handle to created encoder
 * @return
 *      - ESP_OK: Encoder created successfully
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 *      - ESP_ERR_NO_MEM: Not enough memory
 */
esp_err_t rmt_new_led_strip_encoder(const led_strip_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder);

#ifdef __cplusplus
}
#endif
