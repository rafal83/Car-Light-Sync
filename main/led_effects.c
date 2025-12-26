/**
 * @file led_effects.c
 * @brief LED effect rendering engine for WS2812B
 *
 * Handles:
 * - RMT peripheral initialization for WS2812B
 * - 15+ LED effects (solid, rainbow, theater chase, kitt, etc.)
 * - Audio reactivity with FFT (low frequency)
 * - Automatic power limiting (max 2A to avoid brownout)
 * - LED segments with reverse/direction support
 * - Dynamic brightness linked to vehicle brightness (CAN bus)
 */

#include "led_effects.h"

#include "audio_input.h"
#include "config.h"
#include "config_manager.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip_encoder.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "soc/soc_caps.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// ESP32-C6: only 2 TX channels and 4 memory blocks (48 symbols each).
#if CONFIG_IDF_TARGET_ESP32C6
// All 4 blocks available, reserve them for the main strip
#define LED_RMT_MEM_BLOCK_SYMBOLS 96
#else
#define LED_RMT_MEM_BLOCK_SYMBOLS 64
#endif

// Power limiting to avoid brownout on USB power
#define MAX_POWER_MILLIAMPS 3000 // Max consumption in mA (USB can provide ~2A max)
#define LED_MILLIAMPS_PER_LED 40 // Max consumption per LED in white at full brightness (mA)

// Brightness and color constants
#define BRIGHTNESS_MAX 255
#define BRIGHTNESS_HALF 128
#define BRIGHTNESS_OFF 0
#define BRIGHTNESS_NO_REDUCTION 255

// Audio-reactive factors
#define AUDIO_BRIGHTNESS_MIN 0.1f // Minimum brightness in audio mode (10%)
#define AUDIO_BRIGHTNESS_MAX 0.9f // Plage de modulation audio (90%)

// Brightness factors for effects
#define EFFECT_BRIGHTNESS_TAIL 0.3f // Tail brightness (30%)
#define EFFECT_BRIGHTNESS_MID 0.5f  // Medium brightness (50%)
#define EFFECT_BRIGHTNESS_FULL 1.0f // Maximum brightness (100%)

// Animation periods (in frames)
#define ANIM_PERIOD_SLOW_MAX 120
#define ANIM_PERIOD_FAST_MIN 20
#define ANIM_PERIOD_MEDIUM 100
#define ANIM_PERIOD_SHORT 50
#define ANIM_FLASH_DUTY_CYCLE 30 // Percentage of cycle for flash
#define ANIM_FLASH_DUTY_ALERT 60 // Percentage for alert (longer)
#define ANIM_TURN_DUTY_CYCLE 70  // Percentage for turn signals

// Fade and decay factors
#define FADE_FACTOR_SLOW 95   // Slow fade: keep 95% (reduce by 5%)
#define FADE_FACTOR_MEDIUM 90 // Medium fade: keep 90% (reduce by 10%)
#define FADE_DIVISOR 100

// HSV conversion
#define HSV_HUE_REGION_SIZE 43 // Size of an HSV hue region (256/6)
#define HSV_SATURATION_MAX 255
#define HSV_VALUE_MAX 255

// Seuils de niveau de charge (%)
#define CHARGE_LEVEL_LOW 20
#define CHARGE_LEVEL_MEDIUM 50
#define CHARGE_LEVEL_HIGH 80
#define CHARGE_LEVEL_MAX 100

// Minimum charge values for detection
#define CHARGE_POWER_THRESHOLD 0.1f

// Beat detection timing (ms)
#define BEAT_FLASH_DURATION 100

// Sentinel value for uninitialized indicator
#define PROGRESS_NOT_INITIALIZED 255

// Handles for new RMT API
static rmt_channel_handle_t led_chan    = NULL;
static rmt_encoder_handle_t led_encoder = NULL;

// Structure for an RGB pixel
typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} rgb_t;

static rgb_t leds[MAX_LED_COUNT];
static uint8_t led_data[MAX_LED_COUNT * 3];
static effect_config_t current_config;
static bool enabled                                = true;
static uint32_t effect_counter                     = 0;
static vehicle_state_t last_vehicle_state          = {0};
static uint8_t max_allowed_brightness              = BRIGHTNESS_NO_REDUCTION;
static can_event_type_t active_event_context       = CAN_EVENT_NONE;
static bool ota_progress_mode                      = false;
static bool ota_ready_mode                         = false;
static bool ota_error_mode                         = false;
static uint8_t ota_progress_percent                = 0;
static uint8_t ota_displayed_percent               = PROGRESS_NOT_INITIALIZED;
static TickType_t ota_last_progress_refresh        = 0;
static const TickType_t OTA_PROGRESS_REFRESH_LIMIT = pdMS_TO_TICKS(500);

// Floating accumulator for smooth charging animation
static float charge_anim_position                  = 0.0f;

// Global LED strip direction (false = normal)
static uint16_t led_count                          = NUM_LEDS;

static void cleanup_rmt_channel(void);
static bool configure_rmt_channel(void);
static uint16_t sanitize_led_count(uint16_t requested);
static void update_max_allowed_brightness(uint16_t led_total);
static uint8_t map_user_brightness(uint8_t brightness);

// Convert 0xRRGGBB color to rgb_t
static rgb_t color_to_rgb(uint32_t color) {
  rgb_t rgb;
  rgb.r = (color >> 16) & 0xFF;
  rgb.g = (color >> 8) & 0xFF;
  rgb.b = color & 0xFF;
  return rgb;
}

static rgb_t rgb_lerp(rgb_t a, rgb_t b, float t) {
  if (t < 0.0f) {
    t = 0.0f;
  } else if (t > 1.0f) {
    t = 1.0f;
  }

  rgb_t out;
  out.r = (uint8_t)(a.r + (b.r - a.r) * t);
  out.g = (uint8_t)(a.g + (b.g - a.g) * t);
  out.b = (uint8_t)(a.b + (b.b - a.b) * t);
  return out;
}

static rgb_t color_to_rgb_fallback(uint32_t color, uint32_t fallback) {
  if (color == 0) {
    return color_to_rgb(fallback);
  }
  return color_to_rgb(color);
}

static rgb_t rgb_lerp3(rgb_t a, rgb_t b, rgb_t c, float t) {
  if (t < 0.0f) {
    t = 0.0f;
  } else if (t > 1.0f) {
    t = 1.0f;
  }

  if (t < 0.5f) {
    return rgb_lerp(a, b, t * 2.0f);
  }
  return rgb_lerp(b, c, (t - 0.5f) * 2.0f);
}

static rgb_t rgb_max(rgb_t a, rgb_t b) {
  rgb_t out;
  out.r = (a.r > b.r) ? a.r : b.r;
  out.g = (a.g > b.g) ? a.g : b.g;
  out.b = (a.b > b.b) ? a.b : b.b;
  return out;
}

static void fill_solid(rgb_t color);

// Apply brightness to a color (accounts for dynamic and audio brightness)
static rgb_t apply_brightness(rgb_t color, uint8_t brightness) {
  rgb_t result;
  uint8_t effective_brightness = map_user_brightness(brightness);
  // Apply effect brightness
  result.r = (color.r * effective_brightness) / 255;
  result.g = (color.g * effective_brightness) / 255;
  result.b = (color.b * effective_brightness) / 255;

  // Apply dynamic brightness if enabled (from active profile)
  bool dynamic_enabled;
  uint8_t dynamic_rate;
  if (config_manager_get_dynamic_brightness(&dynamic_enabled, &dynamic_rate)) {
    if (dynamic_enabled && !config_manager_is_dynamic_brightness_excluded(active_event_context)) {
      // Formula: final_brightness = effect_brightness x (vehicle_brightness x rate / 100)
      // Minimum 1% to ensure strip remains visible
      float vehicle_brightness = last_vehicle_state.brightness;                                          // 0-100 from CAN
      float rate               = (dynamic_rate ? dynamic_rate : 1) / 100.0f;                             // 0-1
      float applied_brightness = vehicle_brightness * rate / 100.0f;                                     // normalized to 0-1
      if (applied_brightness < 0.01f)
        applied_brightness = 0.01f; // Minimum 1%

      result.r = (uint8_t)(result.r * applied_brightness);
      result.g = (uint8_t)(result.g * applied_brightness);
      result.b = (uint8_t)(result.b * applied_brightness);
    }
  }

  // Apply audio reactive modulation if enabled
  if (current_config.audio_reactive && audio_input_is_enabled()) {
    audio_data_t audio_data;
    if (audio_input_get_data(&audio_data)) {
      // Modulate brightness with audio amplitude (10% base + 90% audio)
      // This yields a very visible swing from 10% to 100%
      float audio_factor = 0.1f + (audio_data.amplitude * 0.9f);
      result.r           = (uint8_t)(result.r * audio_factor);
      result.g           = (uint8_t)(result.g * audio_factor);
      result.b           = (uint8_t)(result.b * audio_factor);
    }
  }

  return result;
}

void led_effects_set_event_context(uint16_t event_id) {
  if (event_id >= CAN_EVENT_MAX) {
    active_event_context = CAN_EVENT_NONE;
  } else {
    active_event_context = (can_event_type_t)event_id;
  }
}

// static rgb_t progress_base_color = {0, 160, 32};
static rgb_t progress_base_color = {16, 255, 16};

static void render_progress_display(void) {
  if (led_count == 0) {
    return;
  }

  float ratio = (float)ota_progress_percent / 100.0f;
  if (ratio < 0.0f) {
    ratio = 0.0f;
  } else if (ratio > 1.0f) {
    ratio = 1.0f;
  }

  int lit_leds = (int)floorf(ratio * led_count + 1e-4f);
  if (ota_progress_percent > 0 && lit_leds == 0) {
    lit_leds = 1;
  }
  if (lit_leds > led_count) {
    lit_leds = led_count;
  }

  for (int i = 0; i < led_count; i++) {
    if (i < lit_leds) {
      leds[i] = progress_base_color;
    } else {
      leds[i] = (rgb_t){0, 0, 0};
    }
  }
}

static void render_status_display(bool error_mode) {
  float phase       = (sinf(effect_counter * 0.25f) + 1.0f) * 0.5f;
  uint8_t intensity = 50 + (uint8_t)(phase * 205);
  rgb_t color       = error_mode ? (rgb_t){intensity, 0, 0} : (rgb_t){0, 40, intensity};
  for (int i = 0; i < led_count; i++) {
    leds[i] = color;
  }
}

// HSV to RGB conversion for rainbow
static rgb_t hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v) {
  rgb_t rgb;
  uint8_t region, remainder, p, q, t;

  if (s == 0) {
    rgb.r = v;
    rgb.g = v;
    rgb.b = v;
    return rgb;
  }

  region    = h / 43;
  remainder = (h - (region * 43)) * 6;

  p         = (v * (255 - s)) >> 8;
  q         = (v * (255 - ((s * remainder) >> 8))) >> 8;
  t         = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

  switch (region) {
  case 0:
    rgb.r = v;
    rgb.g = t;
    rgb.b = p;
    break;
  case 1:
    rgb.r = q;
    rgb.g = v;
    rgb.b = p;
    break;
  case 2:
    rgb.r = p;
    rgb.g = v;
    rgb.b = t;
    break;
  case 3:
    rgb.r = p;
    rgb.g = q;
    rgb.b = v;
    break;
  case 4:
    rgb.r = t;
    rgb.g = p;
    rgb.b = v;
    break;
  default:
    rgb.r = v;
    rgb.g = p;
    rgb.b = q;
    break;
  }

  return rgb;
}

// Send data to LEDs via RMT
static void led_strip_show(void) {
  if (led_chan == NULL || led_encoder == NULL) {
    ESP_LOGE(TAG_LED, "RMT not initialized");
    return;
  }

  if (led_count == 0) {
    ESP_LOGW(TAG_LED, "No LED configured, display ignored");
    return;
  }

  // Prepare data in GRB format for WS2812B
  // Apply the global reverse if enabled
  for (int i = 0; i < led_count; i++) {
    int led_index       = (led_count - 1 - i);
    led_data[i * 3 + 0] = leds[led_index].g; // Green
    led_data[i * 3 + 1] = leds[led_index].r; // Red
    led_data[i * 3 + 2] = leds[led_index].b; // Blue
  }

  // Data transmission
  rmt_transmit_config_t tx_config = {.loop_count = 0, // pas de boucle
                                     .flags      = {
                                              .eot_level = 0, // Niveau EOT (end of transmission)
                                     }};

  // Disable WiFi interrupts during critical transmission
  // to avoid flickering
  // portMUX_TYPE mux                = portMUX_INITIALIZER_UNLOCKED;
  // portENTER_CRITICAL(&mux);

  esp_err_t ret                   = rmt_transmit(led_chan, led_encoder, led_data, led_count * 3, &tx_config);

  // portEXIT_CRITICAL(&mux);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG_LED, "RMT transmission error: %s", esp_err_to_name(ret));
    return;
  }

  // Wait for transmission to complete (without critical section)
  ret = rmt_tx_wait_all_done(led_chan, pdMS_TO_TICKS(200));
  if (ret == ESP_ERR_TIMEOUT) {
    ESP_LOGE(TAG_LED, "Timeout transmission RMT");
    rmt_disable(led_chan);
    rmt_enable(led_chan);
  } else if (ret != ESP_OK) {
    ESP_LOGE(TAG_LED, "rmt_tx_wait_all_done error: %s", esp_err_to_name(ret));
  }
}

static uint16_t sanitize_led_count(uint16_t requested) {
  if (requested == 0) {
    ESP_LOGW(TAG_LED, "Empty LED configuration, falling back to %d LEDs by default", NUM_LEDS);
    return NUM_LEDS;
  }

  if (requested > MAX_LED_COUNT) {
    ESP_LOGW(TAG_LED, "LED configuration too large (%d), applying max %d", requested, MAX_LED_COUNT);
    return MAX_LED_COUNT;
  }

  return requested;
}

static uint8_t map_user_brightness(uint8_t brightness) {
  if (max_allowed_brightness >= BRIGHTNESS_NO_REDUCTION) {
    return brightness;
  }
  return (uint8_t)(((uint32_t)brightness * max_allowed_brightness) / BRIGHTNESS_NO_REDUCTION);
}

static void update_max_allowed_brightness(uint16_t led_total) {
  if (led_total == 0) {
    max_allowed_brightness = BRIGHTNESS_NO_REDUCTION;
    return;
  }

  uint32_t max_current = (uint32_t)LED_MILLIAMPS_PER_LED * led_total;
  if (max_current == 0) {
    max_allowed_brightness = BRIGHTNESS_NO_REDUCTION;
    return;
  }

  uint32_t brightness = ((uint32_t)MAX_POWER_MILLIAMPS * BRIGHTNESS_NO_REDUCTION) / max_current;
  if (brightness < 1) {
    brightness = 1;
  } else if (brightness > BRIGHTNESS_NO_REDUCTION) {
    brightness = BRIGHTNESS_NO_REDUCTION;
  }
  max_allowed_brightness = (uint8_t)brightness;
  ESP_LOGI(TAG_LED, "Power cap: %u LEDs, max brightness %u/255", led_total, max_allowed_brightness);
}

static void cleanup_rmt_channel(void) {
  if (led_encoder != NULL) {
    rmt_del_encoder(led_encoder);
    led_encoder = NULL;
  }

  if (led_chan != NULL) {
    rmt_disable(led_chan);
    rmt_del_channel(led_chan);
    led_chan = NULL;
  }
}

static bool configure_rmt_channel(void) {
  cleanup_rmt_channel();

  rmt_tx_channel_config_t tx_chan_config = {
      .clk_src           = RMT_CLK_SRC_DEFAULT,
      .gpio_num          = LED_PIN,
      .mem_block_symbols = LED_RMT_MEM_BLOCK_SYMBOLS,
      .resolution_hz     = 10000000,
      .trans_queue_depth = 4,
      .flags.invert_out  = false,
#if SOC_RMT_SUPPORT_DMA
      .flags.with_dma = true,
#else
      .flags.with_dma = false,
#endif
  };

  esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &led_chan);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_LED, "Error creating RMT TX channel: %s", esp_err_to_name(ret));
    return false;
  }

  led_strip_encoder_config_t encoder_config = {
      .resolution = tx_chan_config.resolution_hz,
  };

  ret = rmt_new_led_strip_encoder(&encoder_config, &led_encoder);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_LED, "Error creating LED encoder: %s", esp_err_to_name(ret));
    cleanup_rmt_channel();
    return false;
  }

  ret = rmt_enable(led_chan);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_LED, "RMT channel activation error: %s", esp_err_to_name(ret));
    cleanup_rmt_channel();
    return false;
  }

  return true;
}

// Fill all LEDs with a color
static void fill_solid(rgb_t color) {
  for (int i = 0; i < led_count; i++) {
    leds[i] = color;
  }
}

// Effect: Solid color
static void effect_solid(void) {
  if (led_count == 0) {
    return;
  }

  rgb_t color1 = color_to_rgb(current_config.color1);
  rgb_t color2 = color_to_rgb(current_config.color2);
  rgb_t color3 = color_to_rgb(current_config.color3);
  int seg1_end = led_count / 3;
  int seg2_end = (led_count * 2) / 3;

  for (int i = 0; i < led_count; i++) {
    rgb_t color;
    if (i < seg1_end) {
      color = color1;
    } else if (i < seg2_end) {
      color = color2;
    } else {
      color = color3;
    }
    leds[i]     = apply_brightness(color, current_config.brightness);
  }
}

// Effect: Breathing
static void effect_breathing(void) {
  float breath       = (sin(effect_counter * 0.01f * current_config.speed / 10.0f) + 1.0f) / 2.0f;
  uint8_t brightness = (uint8_t)(current_config.brightness * breath);

  rgb_t color1       = color_to_rgb(current_config.color1);
  rgb_t color2       = color_to_rgb_fallback(current_config.color2, current_config.color1);
  rgb_t color3       = color_to_rgb_fallback(current_config.color3, current_config.color1);
  rgb_t color        = rgb_lerp3(color1, color2, color3, breath);
  color              = apply_brightness(color, brightness);
  fill_solid(color);
}

// Effect: Rainbow
static void effect_rainbow(void) {
  // Use speed to control animation speed
  uint32_t speed_factor = (effect_counter * (current_config.speed + 10)) / 50;

  for (int i = 0; i < led_count; i++) {
    int led_index   = current_config.reverse ? (led_count - 1 - i) : i;
    uint16_t hue    = (i * 256 / led_count + speed_factor) % 256;
    rgb_t color     = hsv_to_rgb(hue, HSV_SATURATION_MAX, HSV_VALUE_MAX);
    color           = apply_brightness(color, current_config.brightness);
    leds[led_index] = color;
  }
}

// Effect: Rainbow cycle
static void effect_rainbow_cycle(void) {
  // Speed controls the cycle rate (speed: 0-255)
  uint8_t speed_factor = current_config.speed;
  if (speed_factor < 10)
    speed_factor = 10; // Minimum to avoid issues

  uint16_t hue = ((effect_counter * speed_factor) / 50) % 256;
  rgb_t color  = hsv_to_rgb(hue, 255, 255);                          // Use maximum brightness for HSV
  color        = apply_brightness(color, current_config.brightness); // Apply brightness AND night mode
  fill_solid(color);
}

// Effect: Theater Chase
static void effect_theater_chase(void) {
  rgb_t color1      = apply_brightness(color_to_rgb(current_config.color1), current_config.brightness);
  rgb_t color2      = apply_brightness(color_to_rgb_fallback(current_config.color2, current_config.color1), current_config.brightness);
  rgb_t color3      = apply_brightness(color_to_rgb_fallback(current_config.color3, current_config.color1), current_config.brightness);

  // Use speed to control scroll speed
  int speed_divider = 256 - current_config.speed;
  if (speed_divider < ANIM_PERIOD_FAST_MIN)
    speed_divider = ANIM_PERIOD_FAST_MIN;
  int pos = (effect_counter * 10 / speed_divider) % 3;

  int color_index = (effect_counter / 10) % 3;
  rgb_t chase_color = (color_index == 0) ? color1 : (color_index == 1 ? color2 : color3);

  for (int i = 0; i < led_count; i++) {
    int led_index = current_config.reverse ? (led_count - 1 - i) : i;
    if (i % 3 == pos) {
      leds[led_index] = chase_color;
    } else {
      leds[led_index] = (rgb_t){0, 0, 0};
    }
  }
}

// Effect: Running Lights
static void effect_running_lights(void) {
  // Use speed to control scroll speed
  int speed_divider = 256 - current_config.speed;
  if (speed_divider < 10)
    speed_divider = 10;
  int pos = (effect_counter * 100 / speed_divider) % led_count;
  if (current_config.reverse) {
    pos = led_count - 1 - pos;
  }

  rgb_t color1 = color_to_rgb(current_config.color1);
  rgb_t color2 = color_to_rgb_fallback(current_config.color2, current_config.color1);

  for (int i = 0; i < led_count; i++) {
    int distance = abs(i - pos);
    if (distance > led_count / 2) {
      distance = led_count - distance;
    }

    uint8_t brightness = current_config.brightness * (led_count - distance * 2) / led_count;
    float denom        = (led_count > 1) ? (led_count / 2.0f) : 1.0f;
    float t            = (denom > 0.0f) ? ((float)distance / denom) : 0.0f;
    rgb_t color        = rgb_lerp(color1, color2, t);
    leds[i]            = apply_brightness(color, brightness);
  }
}

// Effect: Twinkle
static void effect_twinkle(void) {
  // Gradually fade all LEDs
  for (int i = 0; i < led_count; i++) {
    leds[i].r = leds[i].r * 95 / 100;
    leds[i].g = leds[i].g * 95 / 100;
    leds[i].b = leds[i].b * 95 / 100;
  }

  // Randomly light a few LEDs
  if (esp_random() % 10 < current_config.speed / 25) {
    int pos       = esp_random() % led_count;
    uint32_t pick = esp_random() % 3;
    rgb_t color;
    if (pick == 0) {
      color = color_to_rgb(current_config.color1);
    } else if (pick == 1) {
      color = color_to_rgb_fallback(current_config.color2, current_config.color1);
    } else {
      color = color_to_rgb_fallback(current_config.color3, current_config.color1);
    }
    leds[pos] = apply_brightness(color, current_config.brightness);
  }
}

// Effect: Fire
static uint16_t heat_map[MAX_LED_COUNT]; // Heat map for the fire effect

static void effect_fire(void) {
  // Refroidissement de la carte de chaleur
  int cooling = 55 + (current_config.speed / 5);
  for (int i = 0; i < led_count; i++) {
    int cooldown = esp_random() % cooling;
    if (cooldown > heat_map[i]) {
      heat_map[i] = 0;
    } else {
      heat_map[i] = heat_map[i] - cooldown;
    }
  }

  // Propagation de la chaleur vers le haut
  for (int i = led_count - 1; i >= 2; i--) {
    heat_map[i] = (heat_map[i - 1] + heat_map[i - 2] + heat_map[i - 2]) / 3;
  }

  // Random ignition of new "flames" across the strip
  // Create multiple ignition points for a uniform effect
  int num_sparks = 3 + (current_config.speed / 50); // Faster = more flames
  for (int s = 0; s < num_sparks; s++) {
    if (esp_random() % 255 < 120) {
      int pos       = esp_random() % led_count; // Across entire strip
      heat_map[pos] = heat_map[pos] + (esp_random() % 160) + 95;
      if (heat_map[pos] > 255)
        heat_map[pos] = 255;
    }
  }

  // Convert heat to colors (fire palette)
  for (int i = 0; i < led_count; i++) {
    rgb_t color;
    uint8_t heat = heat_map[i];

    // Palette: Noir -> Rouge -> Orange -> Jaune -> Blanc
    if (heat < 85) {
      // Black to dark red
      color.r = (heat * 3);
      color.g = 0;
      color.b = 0;
    } else if (heat < 170) {
      // Red to orange/yellow
      color.r = 255;
      color.g = ((heat - 85) * 3);
      color.b = 0;
    } else {
      // Orange/yellow to warm white
      color.r = 255;
      color.g = 255;
      color.b = ((heat - 170) * 2);
    }

    leds[i] = apply_brightness(color, current_config.brightness);
  }
}

// Effect: Scan (Knight Rider)
static void effect_scan(void) {
  // Perform a progressive fade instead of clearing completely to keep the
  // trail
  for (int i = 0; i < led_count; i++) {
    leds[i].r = (leds[i].r * 90) / 100; // Reduce by 10% each frame
    leds[i].g = (leds[i].g * 90) / 100;
    leds[i].b = (leds[i].b * 90) / 100;
  }

  // Use speed to control scroll speed
  int speed_divider = 256 - current_config.speed;
  if (speed_divider < 10)
    speed_divider = 10;
  int pos = (effect_counter * 100 / speed_divider) % (led_count * 2);

  if (pos >= led_count) {
    pos = led_count * 2 - pos - 1;
  }

  if (current_config.reverse) {
    pos = led_count - 1 - pos;
  }

  rgb_t head_color  = color_to_rgb(current_config.color1);
  rgb_t trail_color = color_to_rgb_fallback(current_config.color2, current_config.color1);
  rgb_t base_color  = color_to_rgb_fallback(current_config.color3, current_config.color1);

  if (current_config.color3 != 0) {
    uint8_t base_brightness = current_config.brightness / 6;
    if (base_brightness > 0) {
      rgb_t applied_base = apply_brightness(base_color, base_brightness);
      for (int i = 0; i < led_count; i++) {
        leds[i] = rgb_max(leds[i], applied_base);
      }
    }
  }

  // LED principale plus brillante
  if (pos >= 0 && pos < led_count) {
    leds[pos] = apply_brightness(head_color, current_config.brightness);
  }

  // Gradient trail symmetrical on both sides
  // The trail gradually decreases in brightness
  for (int i = 1; i <= 5; i++) {
    // Calculate trail brightness (decreases with distance)
    uint8_t trail_brightness = current_config.brightness * (6 - i) / 6;
    rgb_t applied_trail      = apply_brightness(trail_color, trail_brightness);

    // Apply the trail on both sides, but only within the limits of the
    // strip
    if (pos - i >= 0 && pos - i < led_count) {
      leds[pos - i] = applied_trail;
    }
    if (pos + i >= 0 && pos + i < led_count) {
      leds[pos + i] = applied_trail;
    }
  }
}

// Effect: Knight Rider (K2000 - sharp trail)
static void effect_knight_rider(void) {
  // Clear the strip to keep a sharp trail
  fill_solid((rgb_t){0, 0, 0});

  // Use speed to control scroll speed
  int speed_divider = 256 - current_config.speed;
  if (speed_divider < 10)
    speed_divider = 10;
  int pos = (effect_counter * 100 / speed_divider) % (led_count * 2);

  if (pos >= led_count) {
    pos = led_count * 2 - pos - 1;
  }

  if (current_config.reverse) {
    pos = led_count - 1 - pos;
  }

  rgb_t head_color  = color_to_rgb(current_config.color1);
  rgb_t trail_color = color_to_rgb_fallback(current_config.color2, current_config.color1);

  // Main LED at full brightness
  if (pos >= 0 && pos < led_count) {
    leds[pos] = apply_brightness(head_color, current_config.brightness);
  }

  // Sharp, short trail (3 LEDs on each side instead of 5)
  // Fast decay for an authentic K2000 look
  for (int i = 1; i <= 3; i++) {
    // Calculate trail brightness with exponential decay
    uint8_t trail_brightness = current_config.brightness / (1 << i); // Division par 2, 4, 8
    rgb_t applied_trail      = apply_brightness(trail_color, trail_brightness);

    // Apply the trail on both sides
    if (pos - i >= 0 && pos - i < led_count) {
      leds[pos - i] = applied_trail;
    }
    if (pos + i >= 0 && pos + i < led_count) {
      leds[pos + i] = applied_trail;
    }
  }
}

// Effect: Fade (smooth in/out)
static void effect_fade(void) {
  // Calculate the period based on speed (speed: 0-255)
  uint8_t speed_factor = current_config.speed;
  if (speed_factor < 10)
    speed_factor = 10;

  // Full period (fade in + fade out)
  uint16_t period = (256 - speed_factor) * 2; // Range: ~20 (fast) to ~512 (slow)
  uint16_t cycle  = effect_counter % period;

  // Calculate triangle brightness (0->255->0)
  uint8_t brightness;
  if (cycle < period / 2) {
    // Fade in (0 -> 255)
    brightness = (cycle * 255) / (period / 2);
  } else {
    // Fade out (255 -> 0)
    brightness = ((period - cycle) * 255) / (period / 2);
  }

  // Apply to the color (cycle across 3 colors)
  rgb_t color1             = color_to_rgb(current_config.color1);
  rgb_t color2             = color_to_rgb_fallback(current_config.color2, current_config.color1);
  rgb_t color3             = color_to_rgb_fallback(current_config.color3, current_config.color1);
  float color_phase        = (float)effect_counter / (float)period;
  color_phase              = fmodf(color_phase, 1.0f);
  rgb_t color              = rgb_lerp3(color1, color2, color3, color_phase);
  uint8_t final_brightness = (brightness * current_config.brightness) / 255;
  color                    = apply_brightness(color, final_brightness);
  fill_solid(color);
}

// Effect: Strobe (full strip flash)
static void effect_strobe(void) {
  rgb_t color1 = color_to_rgb(current_config.color1);
  rgb_t color2 = color_to_rgb_fallback(current_config.color2, current_config.color1);
  rgb_t color3 = color_to_rgb_fallback(current_config.color3, current_config.color1);

  // Flash period based on the speed parameter
  // speed: 0-255, converted to a 10-50 frame period (higher speed = shorter period
  // short = fast)
  int period  = 50 - ((current_config.speed * 40) / 255); // Range: 50 (slow) to 10 (fast)
  if (period < 10)
    period = 10;
  int cycle          = effect_counter % period;
  int color_index    = (effect_counter / period) % 3;
  rgb_t color        = (color_index == 0) ? color1 : (color_index == 1 ? color2 : color3);
  color              = apply_brightness(color, current_config.brightness);

  // Flash actif pendant 30% du cycle
  int flash_duration = (period * 30) / 100;

  if (cycle < flash_duration) {
    // Allumer toute la strip
    fill_solid(color);
  } else {
    // Turn off the entire strip
    fill_solid((rgb_t){0, 0, 0});
  }
}

// Effect: Directional blindspot flash (blindspot with fast directional
// animation)
static void effect_blindspot_flash(void) {
  rgb_t color   = color_to_rgb(current_config.color1);
  color         = apply_brightness(color, current_config.brightness);

  int half_leds = led_count / 2;

  // Fast flash period based on the speed parameter
  // speed: 0-255, converted to a 15-100 frame period (higher speed = shorter period
  // short = fast)
  int period    = 100 - ((current_config.speed * 85) / 255); // Range: 100 (slow) to 15 (fast)
  if (period < 15)
    period = 15;
  int cycle              = effect_counter % period;

  // Flash active for 60% of cycle (longer than turn signal for urgency)
  int animation_duration = (period * 60) / 100;

  // Clear entire strip
  fill_solid((rgb_t){0, 0, 0});

  if (cycle < animation_duration) {
    // Fast animation: light up gradually with more intensity
    int lit_count = (cycle * half_leds) / animation_duration;

    // Light up LEDs with directional animation from center
    for (int i = 0; i < lit_count && i < half_leds; i++) {
      int led_index;

      // Animate from the CENTER outward
      if (current_config.reverse) {
        // Left side: animate from center (half_leds-1) toward the left (0)
        led_index = half_leds - 1 - i;
      } else {
        // Right side: animate from center (half_leds) toward the right
        // (led_count-1)
        led_index = half_leds + i;
      }

      // More even intensity for the alert effect
      float brightness_factor;
      if (i < lit_count - 3) {
        brightness_factor = 0.5f; // Tail at 50% (more visible than turn signal)
      } else {
        brightness_factor = 1.0f; // Head at 100%
      }

      leds[led_index] = apply_brightness(color, (uint8_t)(current_config.brightness * brightness_factor));
    }
  }
  // Shorter pause for alert effect
}

// Effect: Turn signals with sequential scrolling (configurable segment)
static void effect_turn_signal(void) {
  rgb_t base_color = color_to_rgb(current_config.color1);

  int segment_len  = led_count;
  if (segment_len <= 0) {
    fill_solid((rgb_t){0, 0, 0});
    return;
  }

  int period = 120 - ((current_config.speed * 100) / 255); // Range: 120 (slow) to 20 (fast)
  if (period < 20)
    period = 20;
  int cycle              = effect_counter % period;

  int animation_duration = (period * 70) / 100;

  fill_solid((rgb_t){0, 0, 0});

  if (cycle < animation_duration) {
    int lit_count = (cycle * segment_len) / animation_duration;

    for (int i = 0; i < lit_count && i < segment_len; i++) {
      int led_index           = current_config.reverse ? (segment_len - 1 - i) : i;

      float brightness_factor = (i < lit_count - 5) ? 0.3f : 1.0f;
      leds[led_index]         = apply_brightness(base_color, (uint8_t)(current_config.brightness * brightness_factor));
    }
  }
}

// Effect: Hazards (turn signals on both sides simultaneously)
static void effect_hazard(void) {
  rgb_t color   = color_to_rgb(current_config.color1);
  color         = apply_brightness(color, current_config.brightness);

  int half_leds = led_count / 2;

  // Full animation period based on the speed parameter
  // speed: 0-255, converted to a 20-120 frame period (higher speed = shorter period
  // short = fast)
  int period    = 120 - ((current_config.speed * 100) / 255); // Range: 120 (slow) to 20 (fast)
  if (period < 20)
    period = 20;
  int cycle              = effect_counter % period;

  // Phase 1: Progressive scroll (70% of the cycle)
  int animation_duration = (period * 70) / 100;

  // Clear entire strip
  fill_solid((rgb_t){0, 0, 0});

  if (cycle < animation_duration) {
    // Smooth scrolling: gradually light both sides from the
    // centre
    int lit_count = (cycle * half_leds) / animation_duration;

    // Light LEDs on both sides with a gradient for smoothness
    for (int i = 0; i < lit_count && i < half_leds; i++) {
      // Decreasing brightness: the beginning (tail) is dimmer
      float brightness_factor;
      if (i < lit_count - 5) {
        brightness_factor = 0.3f; // Tail at 30%
      } else {
        brightness_factor = 1.0f; // Head at 100%
      }

      rgb_t dimmed_color      = apply_brightness(color, (uint8_t)(current_config.brightness * brightness_factor));

      // Left side: animate from center (half_leds-1) toward the left (0)
      leds[half_leds - 1 - i] = dimmed_color;

      // Right side: animate from center (half_leds) toward the right
      // (led_count-1)
      leds[half_leds + i]     = dimmed_color;
    }
  }
  // Otherwise everything stays off (pause)
}

// Effect: Comet with soft trail
static void effect_comet(void) {
  fill_solid((rgb_t){0, 0, 0});

  if (led_count == 0) {
    return;
  }

  int trail_length = led_count / 8;
  if (trail_length < 3)
    trail_length = 3;
  if (trail_length > 20)
    trail_length = 20;

  int speed_divider = 256 - current_config.speed;
  if (speed_divider < 10)
    speed_divider = 10;
  int head_pos = (effect_counter * 100 / speed_divider) % led_count;
  if (current_config.reverse) {
    head_pos = (led_count - 1) - head_pos;
  }

  rgb_t head_color  = color_to_rgb(current_config.color1);
  rgb_t trail_color = color_to_rgb_fallback(current_config.color2, current_config.color1);

  for (int i = 0; i < trail_length; i++) {
    int offset = current_config.reverse ? i : -i;
    int idx    = head_pos + offset;
    if (idx < 0 || idx >= led_count) {
      continue;
    }

    uint8_t trail_brightness = (uint8_t)((current_config.brightness * (trail_length - i)) / trail_length);
    rgb_t color              = apply_brightness((i == 0) ? head_color : trail_color, trail_brightness);
    leds[idx]                = color;
  }
}

// Effect: Multiple meteor shower
static void effect_meteor_shower(void) {
  fill_solid((rgb_t){0, 0, 0});

  if (led_count == 0) {
    return;
  }

  int tail_length = led_count / 10;
  if (tail_length < 4)
    tail_length = 4;
  if (tail_length > 24)
    tail_length = 24;

  int meteor_count  = 3;
  int speed_divider = 80 - ((current_config.speed * 65) / 255);
  if (speed_divider < 5)
    speed_divider = 5;

  int cycle          = led_count + tail_length;
  int step           = (effect_counter * 100 / speed_divider) % cycle;

  for (int m = 0; m < meteor_count; m++) {
    int offset   = (m * cycle) / meteor_count;
    int head_pos = (step + offset) % cycle;
    uint32_t pick = esp_random() % 3;
    rgb_t meteor_color;
    if (pick == 0) {
      meteor_color = color_to_rgb(current_config.color1);
    } else if (pick == 1) {
      meteor_color = color_to_rgb_fallback(current_config.color2, current_config.color1);
    } else {
      meteor_color = color_to_rgb_fallback(current_config.color3, current_config.color1);
    }

    for (int t = 0; t < tail_length; t++) {
      int pos = head_pos - t;
      if (pos < 0) {
        pos += cycle;
      }
      if (pos >= led_count) {
        continue; // portion hors du ruban visible
      }

      int idx                  = current_config.reverse ? (led_count - 1 - pos) : pos;

      uint8_t trail_brightness = (uint8_t)((current_config.brightness * (tail_length - t)) / tail_length);
      rgb_t color              = apply_brightness(meteor_color, trail_brightness);
      leds[idx]                = rgb_max(leds[idx], color);
    }
  }
}

// Effect: Concentric wave propagating from center
static void effect_ripple_wave(void) {
  fill_solid((rgb_t){0, 0, 0});

  if (led_count == 0) {
    return;
  }

  float center     = (led_count - 1) / 2.0f;
  float max_radius = center;
  float thickness  = (float)led_count / 12.0f + 1.5f;
  if (thickness < 2.0f)
    thickness = 2.0f;

  float speed_factor = (current_config.speed + 10) / 12.0f;
  float radius       = fmodf(effect_counter * speed_factor, max_radius + thickness);
  if (current_config.reverse) {
    radius = max_radius - radius;
  }

  rgb_t color1 = color_to_rgb(current_config.color1);
  rgb_t color2 = color_to_rgb_fallback(current_config.color2, current_config.color1);
  rgb_t color3 = color_to_rgb_fallback(current_config.color3, current_config.color1);

  for (int i = 0; i < led_count; i++) {
    float dist  = fabsf((float)i - center);
    float delta = fabsf(dist - radius);
    if (delta > thickness) {
      continue;
    }

    float intensity       = 1.0f - (delta / thickness);
    uint8_t px_brightness = (uint8_t)(current_config.brightness * intensity);
    float color_t         = (max_radius > 0.0f) ? (dist / max_radius) : 0.0f;
    rgb_t color           = rgb_lerp3(color1, color2, color3, color_t);
    int target_idx        = current_config.reverse ? (led_count - 1 - i) : i;
    leds[target_idx]      = apply_brightness(color, px_brightness);
  }
}

// Effect: Slow breathing double gradient
static void effect_dual_gradient(void) {
  if (led_count == 0) {
    return;
  }

  float period            = 400.0f + (255 - current_config.speed) * 3.0f; // speed modulates the breathing
  float phase             = fmodf(effect_counter, period) / period;       // 0-1
  float blend             = phase < 0.5f ? (phase * 2.0f) : (2.0f - phase * 2.0f);

  rgb_t color1            = color_to_rgb(current_config.color1);
  rgb_t color2            = color_to_rgb_fallback(current_config.color2, current_config.color1);
  rgb_t color3            = color_to_rgb_fallback(current_config.color3, current_config.color1);

  float breath            = 0.6f + 0.4f * (1.0f - fabsf(blend - 0.5f) * 2.0f);
  uint8_t base_brightness = (uint8_t)(current_config.brightness * breath);

  int denom               = (led_count > 1) ? (led_count - 1) : 1;
  for (int i = 0; i < led_count; i++) {
    float pos      = (float)i / denom;
    rgb_t grad_a   = rgb_lerp(color1, color2, pos);
    rgb_t grad_b   = rgb_lerp(color2, color3, pos);
    rgb_t color    = rgb_lerp(grad_a, grad_b, blend);
    int idx        = current_config.reverse ? (led_count - 1 - i) : i;
    leds[idx]      = apply_brightness(color, base_brightness);
  }
}

// Effect: Subtle background + short sparkles
static void effect_sparkle_overlay(void) {
  if (led_count == 0) {
    return;
  }

  // Fast fade to keep sparkles short
  for (int i = 0; i < led_count; i++) {
    leds[i].r = (leds[i].r * 92) / 100;
    leds[i].g = (leds[i].g * 92) / 100;
    leds[i].b = (leds[i].b * 92) / 100;
  }

  rgb_t base_color   = color_to_rgb(current_config.color1);
  rgb_t base_applied = apply_brightness(base_color, current_config.brightness / 4);
  for (int i = 0; i < led_count; i++) {
    leds[i] = rgb_max(leds[i], base_applied);
  }

  int sparkle_slots    = 1 + (current_config.speed / 128);
  uint8_t spawn_chance = (uint8_t)(4 + current_config.speed / 10); // %
  if (spawn_chance > 90)
    spawn_chance = 90;

  for (int s = 0; s < sparkle_slots; s++) {
    if ((esp_random() % 100) < spawn_chance) {
      int idx       = esp_random() % led_count;
      uint32_t pick = esp_random() % 2;
      rgb_t sparkle_color =
          (pick == 0)
              ? color_to_rgb_fallback(current_config.color2, current_config.color1)
              : color_to_rgb_fallback(current_config.color3, current_config.color1);
      rgb_t applied = apply_brightness(sparkle_color, current_config.brightness);
      leds[idx]     = rgb_max(leds[idx], applied);
    }
  }
}

// Effect: Double scan center <-> edges (reverse = edges -> center)
static void effect_center_out_scan(void) {
  fill_solid((rgb_t){0, 0, 0});

  if (led_count == 0) {
    return;
  }

  int half          = led_count / 2;
  bool has_center   = (led_count % 2) != 0;
  int speed_divider = 256 - current_config.speed;
  if (speed_divider < 10)
    speed_divider = 10;

  int max_pos = half;
  int pos     = (effect_counter * 100 / speed_divider) % (max_pos + 1);

  int width   = 3;
  if (width > half) {
    width = (half > 0) ? half : 1;
  }

  rgb_t head_color = color_to_rgb(current_config.color1);
  rgb_t mid_color  = color_to_rgb_fallback(current_config.color2, current_config.color1);
  rgb_t tail_color = color_to_rgb_fallback(current_config.color3, current_config.color1);

  if (!current_config.reverse) {
    // Centre -> bords
    int left_head  = half - 1 - pos;
    int right_head = has_center ? (half + pos + 1) : (half + pos);

    if (has_center && pos == 0) {
      leds[half] = apply_brightness(head_color, current_config.brightness);
    }

    if (left_head >= 0 && left_head < led_count) {
      leds[left_head] = apply_brightness(head_color, current_config.brightness);
    }
    if (right_head >= 0 && right_head < led_count) {
      leds[right_head] = apply_brightness(head_color, current_config.brightness);
    }

    for (int t = 1; t <= width; t++) {
      rgb_t trail_color         = (t == 1) ? mid_color : tail_color;
      uint8_t trail_brightness  = (uint8_t)((current_config.brightness * (width - (t - 1))) / (width + 1));

      int l_raw                 = left_head + t;
      if (l_raw >= 0 && l_raw < led_count) {
        leds[l_raw] = apply_brightness(trail_color, trail_brightness);
      }

      int r_raw = right_head - t;
      if (r_raw >= 0 && r_raw < led_count) {
        leds[r_raw] = apply_brightness(trail_color, trail_brightness);
      }
    }
  } else {
    // Bords -> centre
    int left_head  = pos;
    int right_head = led_count - 1 - pos;

    // Light the center when the heads meet
    if (has_center && pos >= half) {
      leds[half] = apply_brightness(head_color, current_config.brightness);
    } else if (!has_center && pos >= (half - 1)) {
      int c1 = half - 1;
      int c2 = half;
      if (c1 >= 0 && c1 < led_count) {
        leds[c1] = apply_brightness(head_color, current_config.brightness);
      }
      if (c2 >= 0 && c2 < led_count) {
        leds[c2] = apply_brightness(head_color, current_config.brightness);
      }
    }

    if (left_head >= 0 && left_head < led_count) {
      leds[left_head] = apply_brightness(head_color, current_config.brightness);
    }
    if (right_head >= 0 && right_head < led_count) {
      leds[right_head] = apply_brightness(head_color, current_config.brightness);
    }

    for (int t = 1; t <= width; t++) {
      rgb_t trail_color        = (t == 1) ? mid_color : tail_color;
      uint8_t trail_brightness = (uint8_t)((current_config.brightness * (width - (t - 1))) / (width + 1));

      int l_raw                = left_head + t;
      if (l_raw >= 0 && l_raw < led_count && l_raw <= right_head) {
        leds[l_raw] = apply_brightness(trail_color, trail_brightness);
      }

      int r_raw = right_head - t;
      if (r_raw >= 0 && r_raw < led_count && r_raw >= left_head) {
        leds[r_raw] = apply_brightness(trail_color, trail_brightness);
      }
    }
  }
}

// Effect: Brake lights
static void effect_brake_light(void) {
  rgb_t color = last_vehicle_state.brake_pressed ? (rgb_t){255, 0, 0} : (rgb_t){64, 0, 0};
  color       = apply_brightness(color, current_config.brightness);
  fill_solid(color);
}

// Effect: Charge state
static uint8_t simulated_charge = 0; // Simulated charge level (0-100)

static void effect_charge_status(void) {
  // Simuler l'augmentation progressive de la charge
  // Increase ULTRA slowly up to 100%, then restart
  if (effect_counter % 50 == 0) { // Update every 50 frames (10x slower)
    simulated_charge++;
    if (simulated_charge > 100)
      simulated_charge = 0;
  }

  // Use the simulated or real charge level
  uint8_t charge_level = last_vehicle_state.charging ? last_vehicle_state.soc_percent : simulated_charge;

  int target_led       = (led_count * charge_level) / 100;
  if (target_led >= led_count)
    target_led = led_count - 1;

  // Clear all
  fill_solid((rgb_t){0, 0, 0});

  // Display charge bar (static) with color based on level
  for (int i = 0; i < target_led; i++) {
    rgb_t color;

    if (charge_level < CHARGE_LEVEL_LOW) {
      // Rouge en dessous de 20%
      color = (rgb_t){255, 0, 0};
    } else if (charge_level < CHARGE_LEVEL_MEDIUM) {
      // Jaune/Orange entre 20% et 50%
      color = (rgb_t){255, 200, 0};
    } else if (charge_level < CHARGE_LEVEL_HIGH) {
      // Jaune/Vert entre 50% et 80%
      color = (rgb_t){200, 255, 0};
    } else {
      // Vert au-dessus de 80%
      color = (rgb_t){0, 255, 0};
    }

    int led_index   = current_config.reverse ? (led_count - 1 - i) : i;
    leds[led_index] = apply_brightness(color, current_config.brightness);
  }

  // Animated pixel coming from the end (smooth stacking)
  // Animation speed based on CAN charge power
  float speed_factor; // Pixels per frame (more = faster)

  if (last_vehicle_state.charging && last_vehicle_state.charge_power_kw > 0.1f) {
    // With real charging: speed proportional to power
    // Supercharger V3 max = 250 kW, V2 = 150 kW, AC = 11 kW
    float power  = last_vehicle_state.charge_power_kw;

    // Normalize:
    // 3 kW (plug) -> 0.017 px/frame (slow)
    // 150 kW (V2)  -> 0.5 px/frame (fast)
    // 250 kW (V3)  -> 1.0 px/frame (very fast)
    speed_factor = power / 250.0f; // Max 1 pixel per frame

    // Limits
    if (speed_factor < 0.017f)
      speed_factor = 0.017f;
    if (speed_factor > 1.0f)
      speed_factor = 1.0f;
  } else {
    // Simulation mode: speed based on the speed parameter
    // speed 0 -> 0.017 px/frame (slow)
    // speed 255 -> 1.0 px/frame (fast)
    speed_factor = 0.017f + (current_config.speed / 255.0f) * 0.983f;
  }

  // Pixel position: starts at end (led_count-1) and goes to 0
  // Full cycle: from end to charge level + trail length
  // This lets the entire trail be "consumed" before restarting
  const int TRAIL_LENGTH = 5;
  int cycle_length       = led_count - target_led + TRAIL_LENGTH;

  if (cycle_length > TRAIL_LENGTH) {
    // Increment the position accumulator smoothly
    charge_anim_position += speed_factor;

    // Reset the accumulator when the cycle ends
    if (charge_anim_position >= (float)cycle_length) {
      charge_anim_position -= (float)cycle_length;
    }

    // Current pixel position (convert to integer for display)
    int anim_pos         = (int)charge_anim_position;
    // Calculate pixel position based on direction
    int moving_pixel_pos = current_config.reverse ? anim_pos : (led_count - 1 - anim_pos);

    // Extract the RGB components of the configured color
    rgb_t trail_color    = color_to_rgb(current_config.color1);

    // Bright main pixel (configured color)
    // Display only if in visible zone
    if (moving_pixel_pos >= 0 && moving_pixel_pos < led_count) {
      leds[moving_pixel_pos] = apply_brightness(trail_color, current_config.brightness);
    }

    // Same-color trail behind the pixel (8 pixels for the trail
    // longue et fluide)
    for (int trail = 1; trail <= TRAIL_LENGTH; trail++) {
      // Trail direction based on reverse
      int trail_pos        = current_config.reverse ? (moving_pixel_pos - trail) : (moving_pixel_pos + trail);

      // Display trail only if:
      // 1. It is in visible zone (< led_count)
      // 2. It has not yet been "consumed" by the charge bar
      bool in_visible_zone = current_config.reverse ? (trail_pos >= 0 && trail_pos < target_led) : (trail_pos >= target_led && trail_pos < led_count);

      if (in_visible_zone) {
        // Aggressive exponential decay for a visible trail
        // trail 1 = 80% (204), trail 2 = 60% (153), trail 3 = 40% (102), trail
        // 4 = 25% (64), trail 5 = 10% (25)
        uint8_t fade_factor = 255 - (trail * trail * trail * 255) / 125;

        // Apply the colored trail with progressive fade
        // Reduce the intensity of each RGB component according to fade_factor
        rgb_t faded_color   = {(trail_color.r * fade_factor) / 255, (trail_color.g * fade_factor) / 255, (trail_color.b * fade_factor) / 255};

        leds[trail_pos]     = apply_brightness(faded_color, current_config.brightness);
      }
    }
  }
}

// Effect: Power meter (combined front + rear)
static void effect_power_meter(void) {
  const float fallback_max_power = 200.0f;
  float rear_power               = last_vehicle_state.rear_power;
  float front_power              = last_vehicle_state.front_power;
  float rear_limit               = last_vehicle_state.rear_power_limit;
  float front_limit              = last_vehicle_state.front_power_limit;
  float regen_limit              = last_vehicle_state.max_regen;

  if (last_vehicle_state.train_type == 1) { // RWD: ignore front motor
    front_power = 0.0f;
    front_limit = 0.0f;
  }

  float total_power = rear_power + front_power;
  float total_limit = rear_limit + front_limit;
  if (total_limit <= 0.1f) {
    total_limit = fallback_max_power;
  }
  if (regen_limit <= 0.1f) {
    regen_limit = fallback_max_power;
  }

  float limit = (total_power >= 0.0f) ? total_limit : regen_limit;
  if (limit <= 0.1f) {
    limit = fallback_max_power;
  }

  float percent = fabsf(total_power) / limit;
  if (percent > 1.0f) {
    percent = 1.0f;
  }

  int lit_leds = (int)floorf(percent * led_count + 1e-4f);
  if (percent > 0.0f && lit_leds == 0) {
    lit_leds = 1;
  }
  if (lit_leds > led_count) {
    lit_leds = led_count;
  }

  bool is_negative = (total_power < 0.0f);
  bool reverse_dir = current_config.reverse ^ is_negative;

  rgb_t pos_color  = color_to_rgb(current_config.color1);
  rgb_t neg_color  = color_to_rgb(current_config.color2);
  pos_color        = apply_brightness(pos_color, current_config.brightness);
  neg_color        = apply_brightness(neg_color, current_config.brightness);

  for (int i = 0; i < led_count; i++) {
    int led_index = reverse_dir ? (led_count - 1 - i) : i;
    if (i < lit_leds) {
      leds[led_index] = is_negative ? neg_color : pos_color;
    } else {
      leds[led_index] = (rgb_t){0, 0, 0};
    }
  }
}

// Effect: Power meter centered (zero in the middle)
static void effect_power_meter_center(void) {
  const float fallback_max_power = 200.0f;
  float rear_power               = last_vehicle_state.rear_power;
  float front_power              = last_vehicle_state.front_power;
  float rear_limit               = last_vehicle_state.rear_power_limit;
  float front_limit              = last_vehicle_state.front_power_limit;
  float regen_limit              = last_vehicle_state.max_regen;

  if (last_vehicle_state.train_type == 1) { // RWD: ignore front motor
    front_power = 0.0f;
    front_limit = 0.0f;
  }

  float total_power = rear_power + front_power;
  float total_limit = rear_limit + front_limit;
  if (total_limit <= 0.1f) {
    total_limit = fallback_max_power;
  }
  if (regen_limit <= 0.1f) {
    regen_limit = fallback_max_power;
  }

  float limit = (total_power >= 0.0f) ? total_limit : regen_limit;
  if (limit <= 0.1f) {
    limit = fallback_max_power;
  }

  float percent = fabsf(total_power) / limit;
  if (percent > 1.0f) {
    percent = 1.0f;
  }

  int half_len = led_count / 2;
  int lit_side = (int)floorf(percent * half_len + 1e-4f);
  if (percent > 0.0f && lit_side == 0 && half_len > 0) {
    lit_side = 1;
  }
  if (lit_side > half_len) {
    lit_side = half_len;
  }

  bool is_negative = (total_power < 0.0f);
  rgb_t pos_color  = color_to_rgb(current_config.color1);
  rgb_t neg_color  = color_to_rgb(current_config.color2);
  pos_color        = apply_brightness(pos_color, current_config.brightness);
  neg_color        = apply_brightness(neg_color, current_config.brightness);
  rgb_t color      = is_negative ? neg_color : pos_color;

  fill_solid((rgb_t){0, 0, 0});

  if (led_count == 0) {
    return;
  }

  bool use_right_side = !is_negative;
  if (current_config.reverse) {
    use_right_side = !use_right_side;
  }

  if (led_count % 2 == 1) {
    int center = led_count / 2;
    if (percent > 0.0f) {
      leds[center] = color;
    }

    if (use_right_side) {
      for (int i = 0; i < lit_side; i++) {
        int idx = center + 1 + i;
        if (idx >= led_count) {
          break;
        }
        leds[idx] = color;
      }
    } else {
      for (int i = 0; i < lit_side; i++) {
        int idx = center - 1 - i;
        if (idx < 0) {
          break;
        }
        leds[idx] = color;
      }
    }
  } else {
    int left_center  = (led_count / 2) - 1;
    int right_center = led_count / 2;

    if (use_right_side) {
      for (int i = 0; i < lit_side; i++) {
        int idx = right_center + i;
        if (idx >= led_count) {
          break;
        }
        leds[idx] = color;
      }
    } else {
      for (int i = 0; i < lit_side; i++) {
        int idx = left_center - i;
        if (idx < 0) {
          break;
        }
        leds[idx] = color;
      }
    }
  }
}

// Effect: Vehicle sync
static void effect_vehicle_sync(void) {
  // Combine multiple indicators
  rgb_t base_color = {0, 0, 0};

  // Doors open = red
  if (last_vehicle_state.door_front_left_open + last_vehicle_state.door_front_right_open + last_vehicle_state.door_rear_left_open + last_vehicle_state.door_rear_right_open > 0) {
    base_color = (rgb_t){255, 0, 0};
  }
  // Charging = green
  else if (last_vehicle_state.charging) {
    base_color = (rgb_t){0, 255, 0};
  }
  // In motion = blue
  else if (last_vehicle_state.speed_kph > 5.0f) {
    uint16_t intensity = (uint8_t)(last_vehicle_state.speed_kph * 2);
    if (intensity > 255)
      intensity = 255;
    base_color = (rgb_t){0, 0, intensity};
  }
  // Locked = dim white
  else if (last_vehicle_state.locked) {
    base_color = (rgb_t){32, 32, 32};
  }

  base_color = apply_brightness(base_color, current_config.brightness);
  fill_solid(base_color);
}

// Effect: Audio Reactive (VU meter)
static void effect_audio_reactive(void) {
  audio_data_t audio_data;
  if (!audio_input_get_data(&audio_data)) {
    // No audio data, show black
    fill_solid((rgb_t){0, 0, 0});
    return;
  }

  // VU meter: fill based on amplitude
  int lit_leds = (int)(audio_data.amplitude * led_count);
  if (lit_leds > led_count)
    lit_leds = led_count;

  rgb_t color1 = color_to_rgb(current_config.color1);
  rgb_t color2 = color_to_rgb_fallback(current_config.color2, current_config.color1);
  rgb_t color3 = color_to_rgb_fallback(current_config.color3, current_config.color1);

  for (int i = 0; i < led_count; i++) {
    int led_index = current_config.reverse ? (led_count - 1 - i) : i;
    if (i < lit_leds) {
      // Gradient color based on level
      float intensity = (float)(i + 1) / lit_leds;
      rgb_t color     = rgb_lerp3(color1, color2, color3, intensity);
      leds[led_index] = apply_brightness(color, current_config.brightness);
    } else {
      leds[led_index] = (rgb_t){0, 0, 0};
    }
  }
}

// Effect: Audio BPM (flash on beats)
static void effect_audio_bpm(void) {
  audio_data_t audio_data;
  if (!audio_input_get_data(&audio_data)) {
    // No audio data, show black
    fill_solid((rgb_t){0, 0, 0});
    return;
  }

  rgb_t color1             = color_to_rgb(current_config.color1);
  rgb_t color2             = color_to_rgb_fallback(current_config.color2, current_config.color1);
  rgb_t color3             = color_to_rgb_fallback(current_config.color3, current_config.color1);
  int color_index          = (effect_counter / 10) % 3;
  rgb_t color              = (color_index == 0) ? color1 : (color_index == 1 ? color2 : color3);

  // If a beat was detected recently (within the last 100ms)
  uint32_t now             = xTaskGetTickCount() * portTICK_PERIOD_MS;
  uint32_t time_since_beat = now - audio_data.last_beat_ms;

  if (audio_data.beat_detected || time_since_beat < 100) {
    // Flash on beat with decay
    float decay = 1.0f - (time_since_beat / 100.0f);
    if (decay < 0.0f)
      decay = 0.0f;

    uint8_t flash_brightness = (uint8_t)(current_config.brightness * decay);
    color                    = apply_brightness(color, flash_brightness);
    fill_solid(color);
  } else {
    // Couleur faible entre les battements
    color = apply_brightness(color, current_config.brightness / 4);
    fill_solid(color);
  }
}

// ============================================================================
// ADVANCED FFT EFFECTS
// ============================================================================

// FFT effect: Full spectrum (visual equalizer)
static void effect_fft_spectrum(void) {
  audio_fft_data_t fft_data;
  if (!audio_input_get_fft_data(&fft_data)) {
    // FFT non disponible, afficher noir
    fill_solid((rgb_t){0, 0, 0});
    return;
  }

  // Number of LEDs per band
  int leds_per_band = led_count / AUDIO_FFT_BANDS;
  if (leds_per_band < 1)
    leds_per_band = 1;

  rgb_t color1 = color_to_rgb(current_config.color1);
  rgb_t color2 = color_to_rgb_fallback(current_config.color2, current_config.color1);
  rgb_t color3 = color_to_rgb_fallback(current_config.color3, current_config.color1);

  for (int band = 0; band < AUDIO_FFT_BANDS; band++) {
    float t          = (AUDIO_FFT_BANDS > 1) ? ((float)band / (AUDIO_FFT_BANDS - 1)) : 0.0f;
    rgb_t band_color = rgb_lerp3(color1, color2, color3, t);

    // Hauteur proportionnelle au niveau de la bande
    int height       = (int)(fft_data.bands[band] * leds_per_band);
    if (height > leds_per_band)
      height = leds_per_band;

    // Light up LEDs for this band
    for (int i = 0; i < leds_per_band && (band * leds_per_band + i) < led_count; i++) {
      int pos     = band * leds_per_band + i;
      int led_idx = current_config.reverse ? (led_count - 1 - pos) : pos;
      if (i < height) {
        // Gradient from bottom to top
        float intensity = (float)(i + 1) / height;
        rgb_t color;
        color.r       = (uint8_t)(band_color.r * intensity);
        color.g       = (uint8_t)(band_color.g * intensity);
        color.b       = (uint8_t)(band_color.b * intensity);
        leds[led_idx] = apply_brightness(color, current_config.brightness);
      } else {
        leds[led_idx] = (rgb_t){0, 0, 0};
      }
    }
  }
}

// Effet FFT: Bass Pulse (pulse uniquement sur les kicks)
static void effect_fft_bass_pulse(void) {
  audio_fft_data_t fft_data;
  if (!audio_input_get_fft_data(&fft_data)) {
    fill_solid((rgb_t){0, 0, 0});
    return;
  }

  rgb_t color          = color_to_rgb(current_config.color1);

  // Pulse based on low-frequency energy
  float bass_intensity = fft_data.bass_energy;

  // Pulse effect if a kick is detected
  if (fft_data.kick_detected) {
    bass_intensity = 1.0f; // Flash complet
  }

  // Apply the intensity
  uint8_t pulse_brightness = (uint8_t)(current_config.brightness * bass_intensity);
  color                    = apply_brightness(color, pulse_brightness);
  fill_solid(color);
}

// FFT effect: Vocal Wave (voice-reactive wave)
static void effect_fft_vocal_wave(void) {
  audio_fft_data_t fft_data;
  if (!audio_input_get_fft_data(&fft_data)) {
    fill_solid((rgb_t){0, 0, 0});
    return;
  }

  rgb_t base_color  = color_to_rgb(current_config.color1);
  rgb_t vocal_color = color_to_rgb_fallback(current_config.color2, current_config.color1);
  rgb_t tail_color  = color_to_rgb_fallback(current_config.color3, current_config.color1);

  // Wave position based on the spectral centroid
  // The higher the centroid (treble), the further the wave moves
  float wave_pos    = (fft_data.spectral_centroid - 500.0f) / 3500.0f; // Normalize 500-4000 Hz
  if (wave_pos < 0.0f)
    wave_pos = 0.0f;
  if (wave_pos > 1.0f)
    wave_pos = 1.0f;

  int center = (int)(wave_pos * led_count);
  int width  = (int)(fft_data.mid_energy * 20.0f); // Width proportional to mid energy

  for (int i = 0; i < led_count; i++) {
    int led_index = current_config.reverse ? (led_count - 1 - i) : i;
    int distance  = abs(i - center);
    if (distance < width) {
      // Gradient toward the voice
      float mix = (float)distance / width;
      rgb_t color;
      color.r         = (uint8_t)(vocal_color.r * (1.0f - mix) + base_color.r * mix);
      color.g         = (uint8_t)(vocal_color.g * (1.0f - mix) + base_color.g * mix);
      color.b         = (uint8_t)(vocal_color.b * (1.0f - mix) + base_color.b * mix);
      leds[led_index] = apply_brightness(color, current_config.brightness);
    } else {
      leds[led_index] = apply_brightness(tail_color, current_config.brightness / 4);
    }
  }
}

// FFT effect: Energy Bar (spectral energy bar)
static void effect_fft_energy_bar(void) {
  audio_fft_data_t fft_data;
  if (!audio_input_get_fft_data(&fft_data)) {
    fill_solid((rgb_t){0, 0, 0});
    return;
  }

  // Diviser le ruban en 3 sections: Bass, Mid, Treble
  int section_size = led_count / 3;

  rgb_t bass_color   = color_to_rgb(current_config.color1);
  rgb_t mid_color    = color_to_rgb_fallback(current_config.color2, current_config.color1);
  rgb_t treble_color = color_to_rgb_fallback(current_config.color3, current_config.color1);

  // Section Bass
  int bass_leds    = (int)(fft_data.bass_energy * section_size);
  for (int i = 0; i < section_size; i++) {
    int led_index = current_config.reverse ? (led_count - 1 - i) : i;
    if (i < bass_leds) {
      leds[led_index] = apply_brightness(bass_color, current_config.brightness);
    } else {
      leds[led_index] = (rgb_t){0, 0, 0};
    }
  }

  // Section Mid
  int mid_leds = (int)(fft_data.mid_energy * section_size);
  for (int i = 0; i < section_size; i++) {
    int pos     = section_size + i;
    int led_idx = current_config.reverse ? (led_count - 1 - pos) : pos;
    if (i < mid_leds && pos < led_count) {
      leds[led_idx] = apply_brightness(mid_color, current_config.brightness);
    } else if (pos < led_count) {
      leds[led_idx] = (rgb_t){0, 0, 0};
    }
  }

  // Section Treble
  int treble_leds = (int)(fft_data.treble_energy * section_size);
  for (int i = 0; i < section_size; i++) {
    int pos     = section_size * 2 + i;
    int led_idx = current_config.reverse ? (led_count - 1 - pos) : pos;
    if (i < treble_leds && pos < led_count) {
      leds[led_idx] = apply_brightness(treble_color, current_config.brightness);
    } else if (pos < led_count) {
      leds[led_idx] = (rgb_t){0, 0, 0};
    }
  }
}

// Effect function table
typedef void (*effect_func_t)(void);
static const effect_func_t effect_functions[] = {
    [EFFECT_OFF]             = NULL,
    [EFFECT_SOLID]           = effect_solid,
    [EFFECT_BREATHING]       = effect_breathing,
    [EFFECT_RAINBOW]         = effect_rainbow,
    [EFFECT_RAINBOW_CYCLE]   = effect_rainbow_cycle,
    [EFFECT_THEATER_CHASE]   = effect_theater_chase,
    [EFFECT_RUNNING_LIGHTS]  = effect_running_lights,
    [EFFECT_TWINKLE]         = effect_twinkle,
    [EFFECT_FIRE]            = effect_fire,
    [EFFECT_SCAN]            = effect_scan,
    [EFFECT_KNIGHT_RIDER]    = effect_knight_rider,
    [EFFECT_FADE]            = effect_fade,
    [EFFECT_STROBE]          = effect_strobe,
    [EFFECT_VEHICLE_SYNC]    = effect_vehicle_sync,
    [EFFECT_TURN_SIGNAL]     = effect_turn_signal,
    [EFFECT_HAZARD]          = effect_hazard,
    [EFFECT_BRAKE_LIGHT]     = effect_brake_light,
    [EFFECT_CHARGE_STATUS]   = effect_charge_status,
    [EFFECT_BLINDSPOT_FLASH] = effect_blindspot_flash,
    [EFFECT_POWER_METER]     = effect_power_meter,
    [EFFECT_POWER_METER_CENTER] = effect_power_meter_center,
    [EFFECT_AUDIO_REACTIVE]  = effect_audio_reactive,
    [EFFECT_AUDIO_BPM]       = effect_audio_bpm,
    [EFFECT_FFT_SPECTRUM]    = effect_fft_spectrum,
    [EFFECT_FFT_BASS_PULSE]  = effect_fft_bass_pulse,
    [EFFECT_FFT_VOCAL_WAVE]  = effect_fft_vocal_wave,
    [EFFECT_FFT_ENERGY_BAR]  = effect_fft_energy_bar,
    [EFFECT_COMET]           = effect_comet,
    [EFFECT_METEOR_SHOWER]   = effect_meteor_shower,
    [EFFECT_RIPPLE_WAVE]     = effect_ripple_wave,
    [EFFECT_DUAL_GRADIENT]   = effect_dual_gradient,
    [EFFECT_SPARKLE_OVERLAY] = effect_sparkle_overlay,
    [EFFECT_CENTER_OUT_SCAN] = effect_center_out_scan,
};

typedef struct {
  led_effect_t effect;
  const char *id;
  const char *name;
  bool requires_can;
} led_effect_descriptor_t;

static const led_effect_descriptor_t effect_descriptors[] = {
    {EFFECT_OFF, EFFECT_ID_OFF, "Off", false},
    {EFFECT_SOLID, EFFECT_ID_SOLID, "Solid", false},
    {EFFECT_BREATHING, EFFECT_ID_BREATHING, "Breathing", false},
    {EFFECT_RAINBOW, EFFECT_ID_RAINBOW, "Rainbow", false},
    {EFFECT_RAINBOW_CYCLE, EFFECT_ID_RAINBOW_CYCLE, "Rainbow Cycle", false},
    {EFFECT_THEATER_CHASE, EFFECT_ID_THEATER_CHASE, "Theater Chase", false},
    {EFFECT_RUNNING_LIGHTS, EFFECT_ID_RUNNING_LIGHTS, "Running Lights", false},
    {EFFECT_TWINKLE, EFFECT_ID_TWINKLE, "Twinkle", false},
    {EFFECT_FIRE, EFFECT_ID_FIRE, "Fire", false},
    {EFFECT_SCAN, EFFECT_ID_SCAN, "Scan", false},
    {EFFECT_KNIGHT_RIDER, EFFECT_ID_KNIGHT_RIDER, "Knight Rider", false},
    {EFFECT_FADE, EFFECT_ID_FADE, "Fade", false},
    {EFFECT_STROBE, EFFECT_ID_STROBE, "Strobe", false},
    {EFFECT_VEHICLE_SYNC, EFFECT_ID_VEHICLE_SYNC, "Vehicle Sync", true},
    {EFFECT_TURN_SIGNAL, EFFECT_ID_TURN_SIGNAL, "Turn Signal", true},
    {EFFECT_BRAKE_LIGHT, EFFECT_ID_BRAKE_LIGHT, "Brake Light", true},
    {EFFECT_CHARGE_STATUS, EFFECT_ID_CHARGE_STATUS, "Charge Status", true},
    {EFFECT_HAZARD, EFFECT_ID_HAZARD, "Hazard", true},
    {EFFECT_BLINDSPOT_FLASH, EFFECT_ID_BLINDSPOT_FLASH, "Blindspot Flash", true},
    {EFFECT_POWER_METER, EFFECT_ID_POWER_METER, "Power Meter", true},
    {EFFECT_POWER_METER_CENTER, EFFECT_ID_POWER_METER_CENTER, "Power Meter Center", true},
    {EFFECT_AUDIO_REACTIVE, EFFECT_ID_AUDIO_REACTIVE, "Audio Reactive", false},
    {EFFECT_AUDIO_BPM, EFFECT_ID_AUDIO_BPM, "Audio BPM", false},
    {EFFECT_FFT_SPECTRUM, EFFECT_ID_FFT_SPECTRUM, "FFT Spectrum", false},
    {EFFECT_FFT_BASS_PULSE, EFFECT_ID_FFT_BASS_PULSE, "FFT Bass Pulse", false},
    {EFFECT_FFT_VOCAL_WAVE, EFFECT_ID_FFT_VOCAL_WAVE, "FFT Vocal Wave", false},
    {EFFECT_FFT_ENERGY_BAR, EFFECT_ID_FFT_ENERGY_BAR, "FFT Energy Bar", false},
    {EFFECT_COMET, EFFECT_ID_COMET, "Comet", false},
    {EFFECT_METEOR_SHOWER, EFFECT_ID_METEOR_SHOWER, "Meteor Shower", false},
    {EFFECT_RIPPLE_WAVE, EFFECT_ID_RIPPLE_WAVE, "Ripple Wave", false},
    {EFFECT_DUAL_GRADIENT, EFFECT_ID_DUAL_GRADIENT, "Dual Gradient", false},
    {EFFECT_SPARKLE_OVERLAY, EFFECT_ID_SPARKLE_OVERLAY, "Sparkle Overlay", false},
    {EFFECT_CENTER_OUT_SCAN, EFFECT_ID_CENTER_OUT_SCAN, "Center Out Scan", false},
};

#define EFFECT_DESCRIPTOR_COUNT (sizeof(effect_descriptors) / sizeof(effect_descriptors[0]))
typedef char effect_descriptor_size_mismatch[(EFFECT_MAX == EFFECT_DESCRIPTOR_COUNT) ? 1 : -1];

static const led_effect_descriptor_t *find_effect_descriptor(led_effect_t effect) {
  for (size_t i = 0; i < EFFECT_DESCRIPTOR_COUNT; ++i) {
    if (effect_descriptors[i].effect == effect) {
      return &effect_descriptors[i];
    }
  }
  return NULL;
}

static const led_effect_descriptor_t *find_effect_descriptor_by_id(const char *id) {
  if (id == NULL) {
    return NULL;
  }
  for (size_t i = 0; i < EFFECT_DESCRIPTOR_COUNT; ++i) {
    if (strcmp(effect_descriptors[i].id, id) == 0) {
      return &effect_descriptors[i];
    }
  }
  return NULL;
}

bool led_effects_init(void) {
  uint16_t configured_leds = config_manager_get_led_count();
  led_count                = sanitize_led_count(configured_leds);
  update_max_allowed_brightness(led_count);

  if (!configure_rmt_channel()) {
    return false;
  }

  // Default configuration
  led_effects_reset_config();

  // LED configuration is now managed by config_manager through profiles

  ESP_LOGI(TAG_LED, "LEDs initialized (%d LEDs on GPIO %d)", led_count, LED_PIN);
  return true;
}

void led_effects_deinit(void) {
  // Turn off all LEDs
  fill_solid((rgb_t){0, 0, 0});
  led_strip_show();

  cleanup_rmt_channel();

  ESP_LOGI(TAG_LED, "LEDs deinitialized");
}

bool led_effects_set_led_count(uint16_t requested_led_count) {
  if (requested_led_count < 1 || requested_led_count > MAX_LED_COUNT) {
    ESP_LOGE(TAG_LED, "LED count %d invalide", requested_led_count);
    return false;
  }

  led_count = requested_led_count;
  update_max_allowed_brightness(led_count);
  ESP_LOGI(TAG_LED, "LED count updated: %d", led_count);
  return true;
}

uint16_t led_effects_get_led_count(void) {
  return led_count;
}

uint8_t led_effects_get_accel_pedal_pos(void) {
  return last_vehicle_state.accel_pedal_pos;
}

uint16_t led_effects_apply_accel_modulation(uint16_t original_length, uint8_t accel_pedal_pos, uint8_t offset_percent) {
  // Normalize values
  uint8_t accel_percent = accel_pedal_pos;
  if (accel_percent > 100)
    accel_percent = 100;

  if (offset_percent > 100)
    offset_percent = 100;

  // Calculate effective percentage: offset + (accel x (100 - offset) / 100)
  uint8_t effective_percent = offset_percent + ((accel_percent * (100 - offset_percent)) / 100);

  // Apply this percentage to the length
  uint16_t modulated_length = (original_length * effective_percent) / 100;
  if (modulated_length < 1)
    modulated_length = 1; // Au moins 1 LED

  return modulated_length;
}

void led_effects_normalize_segment(uint16_t *segment_start, uint16_t *segment_length, uint16_t total_leds) {
  if (segment_start == NULL || segment_length == NULL) {
    return;
  }

  // Normalize length (0 = full strip)
  if (*segment_length == 0 || *segment_length > total_leds) {
    *segment_length = total_leds;
  }

  // Normalize start
  if (*segment_start >= total_leds) {
    *segment_start = 0;
  }

  // Adjust if overflowing
  if ((*segment_start + *segment_length) > total_leds) {
    *segment_length = total_leds - *segment_start;
  }
}

void led_effects_set_config(const effect_config_t *config) {
  if (config != NULL) {
    memcpy(&current_config, config, sizeof(effect_config_t));

    // Normalize segment
    led_effects_normalize_segment(&current_config.segment_start, &current_config.segment_length, led_count);

    // Automatically enable/disable FFT based on the effect
    bool needs_fft = led_effects_requires_fft(current_config.effect);
    audio_input_set_fft_enabled(needs_fft);

    ESP_LOGI(TAG_LED, "Configured effect: %d, audio_reactive=%d, FFT %s", current_config.effect, current_config.audio_reactive, needs_fft ? "enabled" : "disabled");
  }
}

void led_effects_get_config(effect_config_t *config) {
  if (config != NULL) {
    memcpy(config, &current_config, sizeof(effect_config_t));
  }
}

void led_effects_update(void) {
  led_effects_set_event_context(CAN_EVENT_NONE);
  // Display nothing if config_manager handles active events
  if (config_manager_has_active_events()) {
    effect_counter++;
    return;
  }

  if (!enabled && !ota_progress_mode && !ota_ready_mode && !ota_error_mode) {
    fill_solid((rgb_t){0, 0, 0});
    led_strip_show();
    return;
  }

  if (ota_progress_mode) {
    TickType_t now     = xTaskGetTickCount();
    bool needs_refresh = false;

    if (ota_progress_percent != ota_displayed_percent) {
      ota_displayed_percent = ota_progress_percent;
      needs_refresh         = true;
    } else if ((now - ota_last_progress_refresh) > OTA_PROGRESS_REFRESH_LIMIT) {
      needs_refresh = true;
    }

    if (needs_refresh) {
      render_progress_display();
      led_strip_show();
      ota_last_progress_refresh = now;
    }

    effect_counter++;
    return;
  }

  if (ota_error_mode) {
    render_status_display(true);
    led_strip_show();
    effect_counter++;
    return;
  }

  if (ota_ready_mode) {
    render_status_display(false);
    led_strip_show();
    effect_counter++;
    return;
  }

  // Mode normal
  if (current_config.effect == EFFECT_OFF) {
    fill_solid((rgb_t){0, 0, 0});
  } else if (current_config.effect < EFFECT_MAX && effect_functions[current_config.effect] != NULL) {
    // If a segment is defined, render only that segment
    uint16_t segment_start  = current_config.segment_start;
    uint16_t segment_length = current_config.segment_length;

    // Normalize segment
    led_effects_normalize_segment(&segment_start, &segment_length, led_count);

    // Calculate dynamic length based on accel_pedal_pos if enabled
    if (current_config.accel_pedal_pos_enabled) {
      segment_length = led_effects_apply_accel_modulation(segment_length, last_vehicle_state.accel_pedal_pos, current_config.accel_pedal_offset);
    }

    // Optimization: full strip without custom segment
    if (segment_start == 0 && segment_length == led_count) {
      effect_functions[current_config.effect]();
    }
    // Custom segment or reduced strip
    else {
      uint16_t saved_led_count = led_count;

      // Set everything to black first
      fill_solid((rgb_t){0, 0, 0});

      // Work on segment only
      led_count = segment_length;
      effect_functions[current_config.effect]();

      // Copy the result into the destination segment
      rgb_t segment_buffer[MAX_LED_COUNT];
      for (uint16_t i = 0; i < segment_length; i++) {
        segment_buffer[i] = leds[i];
      }

      // Restore led_count and reset everything to black
      led_count = saved_led_count;
      fill_solid((rgb_t){0, 0, 0});

      // Apply segment at correct location
      for (uint16_t i = 0; i < segment_length; i++) {
        uint16_t idx = segment_start + i;
        if (idx < led_count) {
          leds[idx] = segment_buffer[i];
        }
      }
    }
  }

  led_strip_show();
  effect_counter++;
}

void led_effects_update_vehicle_state(const vehicle_state_t *state) {
  if (state != NULL) {
    memcpy(&last_vehicle_state, state, sizeof(vehicle_state_t));
  }
}

void led_effects_start_progress_display(void) {
  ota_ready_mode            = false;
  ota_error_mode            = false;
  ota_progress_mode         = true;
  ota_progress_percent      = 0;
  ota_displayed_percent     = 255;
  ota_last_progress_refresh = 0;
}

void led_effects_update_progress(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  ota_progress_percent = percent;
}

void led_effects_stop_progress_display(void) {
  ota_progress_mode         = false;
  ota_progress_percent      = 0;
  ota_ready_mode            = false;
  ota_error_mode            = false;
  ota_displayed_percent     = 255;
  ota_last_progress_refresh = 0;
}

bool led_effects_is_ota_display_active(void) {
  return ota_progress_mode || ota_ready_mode || ota_error_mode;
}

void led_effects_show_upgrade_ready(void) {
  ota_progress_mode     = false;
  ota_progress_percent  = 100;
  ota_error_mode        = false;
  ota_ready_mode        = true;
  ota_displayed_percent = 255;
}

void led_effects_show_upgrade_error(void) {
  ota_progress_mode     = false;
  ota_ready_mode        = false;
  ota_error_mode        = true;
  ota_displayed_percent = 255;
}

const char *led_effects_get_name(led_effect_t effect) {
  const led_effect_descriptor_t *desc = find_effect_descriptor(effect);
  return desc ? desc->name : "Unknown";
}

// Mapping table enum -> alphanumeric ID
const char *led_effects_enum_to_id(led_effect_t effect) {
  const led_effect_descriptor_t *desc = find_effect_descriptor(effect);
  return desc ? desc->id : EFFECT_ID_OFF;
}

// Mapping table alphanumeric ID -> enum
led_effect_t led_effects_id_to_enum(const char *id) {
  const led_effect_descriptor_t *desc = find_effect_descriptor_by_id(id);
  if (desc) {
    return desc->effect;
  }
  ESP_LOGW(TAG_LED, "Unknown effect ID: %s", id ? id : "NULL");
  return EFFECT_OFF;
}

// Checks if an effect needs CAN data to run
bool led_effects_requires_can(led_effect_t effect) {
  const led_effect_descriptor_t *desc = find_effect_descriptor(effect);
  return desc ? desc->requires_can : false;
}

bool led_effects_requires_fft(led_effect_t effect) {
  switch (effect) {
  case EFFECT_FFT_SPECTRUM:
  case EFFECT_FFT_BASS_PULSE:
  case EFFECT_FFT_VOCAL_WAVE:
  case EFFECT_FFT_ENERGY_BAR:
    return true;
  default:
    return false;
  }
}

bool led_effects_is_audio_effect(led_effect_t effect) {
  // Not all audio effects can be assigned to CAN events
  switch (effect) {
  case EFFECT_AUDIO_REACTIVE:
  case EFFECT_AUDIO_BPM:
  case EFFECT_FFT_SPECTRUM:
  case EFFECT_FFT_BASS_PULSE:
  case EFFECT_FFT_VOCAL_WAVE:
  case EFFECT_FFT_ENERGY_BAR:
    return true;
  default:
    return false;
  }
}

void led_effects_reset_config(void) {
  current_config.effect         = EFFECT_RAINBOW;
  current_config.brightness     = DEFAULT_BRIGHTNESS;
  current_config.speed          = DEFAULT_SPEED;
  current_config.color1         = 0xFF0000; // Rouge
  current_config.color2         = 0x00FF00; // Vert
  current_config.color3         = 0x0000FF; // Bleu
  current_config.sync_mode      = SYNC_OFF;
  current_config.reverse        = false;
  current_config.audio_reactive = false;
  current_config.segment_start  = 0;
  current_config.segment_length = 0;

  ESP_LOGI(TAG_LED, "Configuration reset");
}

uint32_t led_effects_get_frame_counter(void) {
  return effect_counter;
}

void led_effects_advance_frame_counter(void) {
  effect_counter++;
}

void led_effects_render_to_buffer(const effect_config_t *config, uint16_t segment_start, uint16_t segment_length, uint32_t frame_counter, led_rgb_t *out_buffer) {
  if (config == NULL || out_buffer == NULL) {
    return;
  }

  // segment_length == 0 signifie "utiliser toute la strip"
  if (segment_length == 0) {
    segment_length = led_count;
  }

  // Valider segment_start
  if (segment_start >= led_count) {
    return;
  }

  // Adjust length if the segment overflows
  if ((segment_start + segment_length) > led_count) {
    segment_length = led_count - segment_start;
  }

  // Save current state
  effect_config_t saved_config = current_config;
  uint16_t saved_led_count     = led_count;
  uint32_t saved_counter       = effect_counter;

  // Work on segment only
  led_count                    = segment_length;
  effect_counter               = frame_counter;
  current_config               = *config;

  fill_solid((rgb_t){0, 0, 0});
  if (current_config.effect == EFFECT_OFF) {
    // rien a faire
  } else if (current_config.effect < EFFECT_MAX && effect_functions[current_config.effect] != NULL) {
    effect_functions[current_config.effect]();
  }

  for (uint16_t i = 0; i < segment_length; i++) {
    uint16_t idx = segment_start + i;
    if (idx >= saved_led_count) {
      break;
    }
    out_buffer[idx].r = leds[i].r;
    out_buffer[idx].g = leds[i].g;
    out_buffer[idx].b = leds[i].b;
  }

  // Restaurer l'etat
  current_config = saved_config;
  led_count      = saved_led_count;
  effect_counter = saved_counter;
}

void led_effects_show_buffer(const led_rgb_t *buffer) {
  if (buffer == NULL) {
    return;
  }

  if (led_count == 0) {
    return;
  }

  // Copy the buffer
  for (uint16_t i = 0; i < led_count; i++) {
    leds[i].r             = buffer[i].r;
    leds[i].g             = buffer[i].g;
    leds[i].b             = buffer[i].b;
  }

  led_strip_show();
}
