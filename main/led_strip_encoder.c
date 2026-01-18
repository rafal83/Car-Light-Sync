#include "led_strip_encoder.h"

#include "esp_check.h"

typedef struct {
  rmt_encoder_t base;
  rmt_encoder_t *bytes_encoder;
  rmt_encoder_t *copy_encoder;
  int state;
  rmt_symbol_word_t reset_code;
} rmt_led_strip_encoder_t;

static size_t rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state) {
  rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
  rmt_encoder_handle_t bytes_encoder   = led_encoder->bytes_encoder;
  rmt_encoder_handle_t copy_encoder    = led_encoder->copy_encoder;
  rmt_encode_state_t session_state     = RMT_ENCODING_RESET;
  rmt_encode_state_t state             = RMT_ENCODING_RESET;
  size_t encoded_symbols               = 0;

  switch (led_encoder->state) {
  case 0: // Send RGB data
    encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
    if (session_state & RMT_ENCODING_COMPLETE) {
      led_encoder->state = 1; // proceed to reset
    }
    if (session_state & RMT_ENCODING_MEM_FULL) {
      state |= RMT_ENCODING_MEM_FULL;
      goto out;
    }
  // fall-through
  case 1: // Send reset code
    encoded_symbols += copy_encoder->encode(copy_encoder, channel, &led_encoder->reset_code, sizeof(led_encoder->reset_code), &session_state);
    if (session_state & RMT_ENCODING_COMPLETE) {
      led_encoder->state = RMT_ENCODING_RESET;
      state |= RMT_ENCODING_COMPLETE;
    }
    if (session_state & RMT_ENCODING_MEM_FULL) {
      state |= RMT_ENCODING_MEM_FULL;
      goto out;
    }
  }
out:
  *ret_state = state;
  return encoded_symbols;
}

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t *encoder) {
  rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
  rmt_del_encoder(led_encoder->bytes_encoder);
  rmt_del_encoder(led_encoder->copy_encoder);
  free(led_encoder);
  return ESP_OK;
}

static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t *encoder) {
  rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
  rmt_encoder_reset(led_encoder->bytes_encoder);
  rmt_encoder_reset(led_encoder->copy_encoder);
  led_encoder->state = RMT_ENCODING_RESET;
  return ESP_OK;
}

esp_err_t rmt_new_led_strip_encoder(const led_strip_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder) {
  esp_err_t ret                        = ESP_OK;
  rmt_led_strip_encoder_t *led_encoder = NULL;
  ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG_LED_ENCODER, "invalid argument");

  led_encoder = calloc(1, sizeof(rmt_led_strip_encoder_t));
  ESP_GOTO_ON_FALSE(led_encoder, ESP_ERR_NO_MEM, err, TAG_LED_ENCODER, "no mem for led strip encoder");

  led_encoder->base.encode                        = rmt_encode_led_strip;
  led_encoder->base.del                           = rmt_del_led_strip_encoder;
  led_encoder->base.reset                         = rmt_led_strip_encoder_reset;

  // Configuration for WS2812B
  // Bit 0: 0.4us high, 0.85us low (total 1.25us)
  // Bit 1: 0.8us high, 0.45us low (total 1.25us)
  rmt_bytes_encoder_config_t bytes_encoder_config = {
      .bit0 =
          {
              .level0    = 1,
              .duration0 = 0.4 * config->resolution / 1000000, // 0.4us
              .level1    = 0,
              .duration1 = 0.85 * config->resolution / 1000000, // 0.85us
          },
      .bit1 =
          {
              .level0    = 1,
              .duration0 = 0.8 * config->resolution / 1000000, // 0.8us
              .level1    = 0,
              .duration1 = 0.45 * config->resolution / 1000000, // 0.45us
          },
      .flags.msb_first = 1 // WS2812B uses MSB first
  };
  ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder->bytes_encoder), err, TAG_LED_ENCODER, "create bytes encoder failed");

  rmt_copy_encoder_config_t copy_encoder_config = {};
  ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &led_encoder->copy_encoder), err, TAG_LED_ENCODER, "create copy encoder failed");

  // Reset code: low level for >50us
  uint32_t reset_ticks    = config->resolution / 1000000 * 50; // 50us
  led_encoder->reset_code = (rmt_symbol_word_t){
      .level0    = 0,
      .duration0 = reset_ticks,
      .level1    = 0,
      .duration1 = reset_ticks,
  };

  *ret_encoder = &led_encoder->base;
  return ESP_OK;

err:
  if (led_encoder) {
    if (led_encoder->bytes_encoder) {
      rmt_del_encoder(led_encoder->bytes_encoder);
    }
    if (led_encoder->copy_encoder) {
      rmt_del_encoder(led_encoder->copy_encoder);
    }
    free(led_encoder);
  }
  return ret;
}
