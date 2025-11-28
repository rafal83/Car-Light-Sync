#include "led_effects.h"
#include "config.h"
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
#include "config_manager.h"
#include "audio_input.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LED_COUNT 1000 // Align with config_manager_set_led_hardware validation

// Handles pour la nouvelle API RMT
static rmt_channel_handle_t led_chan = NULL;
static rmt_encoder_handle_t led_encoder = NULL;

// Structure pour un pixel RGB
typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} rgb_t;

static rgb_t leds[MAX_LED_COUNT];
static uint8_t led_data[MAX_LED_COUNT * 3];
static rgb_t left_led_buffer[MAX_LED_COUNT / 2];
static rgb_t right_led_buffer[MAX_LED_COUNT / 2];
static effect_config_t current_config;
static bool enabled = true;
static uint32_t effect_counter = 0;
static vehicle_state_t last_vehicle_state = {0};
static bool ota_progress_mode = false;
static bool ota_ready_mode = false;
static bool ota_error_mode = false;
static uint8_t ota_progress_percent = 0;
static uint8_t ota_displayed_percent = 255;
static TickType_t ota_last_progress_refresh = 0;
static const TickType_t OTA_PROGRESS_REFRESH_LIMIT = pdMS_TO_TICKS(500);

// Accumulateur flottant pour animation de charge fluide
static float charge_anim_position = 0.0f;

// Night mode global state
static bool night_mode_active = false;
static uint8_t night_mode_brightness = 255; // 255 = 100%, default no reduction

// Dual directional mode state
static bool dual_directional_mode = false;
static effect_config_t left_directional_config;
static effect_config_t right_directional_config;

// Global LED strip direction (false = normal, true = reversed)
static bool global_reverse_direction = true;
static uint16_t led_count = NUM_LEDS;

static void cleanup_rmt_channel(void);
static bool configure_rmt_channel(void);
static uint16_t sanitize_led_count(uint16_t requested);

// Conversion couleur 0xRRGGBB vers rgb_t
static rgb_t color_to_rgb(uint32_t color) {
  rgb_t rgb;
  rgb.r = (color >> 16) & 0xFF;
  rgb.g = (color >> 8) & 0xFF;
  rgb.b = color & 0xFF;
  return rgb;
}

static void fill_solid(rgb_t color);

// Applique la luminosité à une couleur (avec prise en compte du mode nuit et audio)
static rgb_t apply_brightness(rgb_t color, uint8_t brightness) {
  rgb_t result;
  // Apply effect brightness
  result.r = (color.r * brightness) / 255;
  result.g = (color.g * brightness) / 255;
  result.b = (color.b * brightness) / 255;

  // Apply night mode brightness multiplier if active
  if (night_mode_active) {
    result.r = (result.r * night_mode_brightness) / 255;
    result.g = (result.g * night_mode_brightness) / 255;
    result.b = (result.b * night_mode_brightness) / 255;
  }

  // Apply audio reactive modulation if enabled
  if (current_config.audio_reactive && audio_input_is_enabled()) {
    audio_data_t audio_data;
    if (audio_input_get_data(&audio_data)) {
      // Moduler la luminosité avec l'amplitude audio (10% base + 90% audio)
      // Cela donne une variation très visible de 10% à 100%
      float audio_factor = 0.1f + (audio_data.amplitude * 0.9f);
      result.r = (uint8_t)(result.r * audio_factor);
      result.g = (uint8_t)(result.g * audio_factor);
      result.b = (uint8_t)(result.b * audio_factor);
    }
  }

  return result;
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
  float phase = (sinf(effect_counter * 0.25f) + 1.0f) * 0.5f;
  uint8_t intensity = 50 + (uint8_t)(phase * 205);
  rgb_t color =
      error_mode ? (rgb_t){intensity, 0, 0} : (rgb_t){0, 40, intensity};
  for (int i = 0; i < led_count; i++) {
    leds[i] = color;
  }
}

// Conversion HSV vers RGB pour rainbow
static rgb_t hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v) {
  rgb_t rgb;
  uint8_t region, remainder, p, q, t;

  if (s == 0) {
    rgb.r = v;
    rgb.g = v;
    rgb.b = v;
    return rgb;
  }

  region = h / 43;
  remainder = (h - (region * 43)) * 6;

  p = (v * (255 - s)) >> 8;
  q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

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

// Applique le masque de zone: éteint les LEDs hors de la zone spécifiée
static void apply_zone_mask(led_zone_t zone) {
  int half = led_count / 2;

  switch (zone) {
  case LED_ZONE_LEFT:
    // Éteindre la moitié droite
    for (int i = half; i < led_count; i++) {
      leds[i] = (rgb_t){0, 0, 0};
    }
    break;

  case LED_ZONE_RIGHT:
    // Éteindre la moitié gauche
    for (int i = 0; i < half; i++) {
      leds[i] = (rgb_t){0, 0, 0};
    }
    break;

  case LED_ZONE_FULL:
  default:
    // Pas de masque, tout le strip est actif
    break;
  }
}

// Envoie les données aux LEDs via RMT
static void led_strip_show(void) {
  if (led_chan == NULL || led_encoder == NULL) {
    ESP_LOGE(TAG_LED, "RMT non initialisé");
    return;
  }

  if (led_count == 0) {
    ESP_LOGW(TAG_LED, "Aucune LED configurée, affichage ignoré");
    return;
  }

  // Préparer les données au format GRB pour WS2812B
  // Appliquer le reverse global si activé
  for (int i = 0; i < led_count; i++) {
    int led_index = global_reverse_direction ? (led_count - 1 - i) : i;
    led_data[i * 3 + 0] = leds[led_index].g; // Green
    led_data[i * 3 + 1] = leds[led_index].r; // Red
    led_data[i * 3 + 2] = leds[led_index].b; // Blue
  }

  // Transmission des données
  rmt_transmit_config_t tx_config = {
      .loop_count = 0, // pas de boucle
      .flags = {
          .eot_level = 0, // Niveau EOT (end of transmission)
      }};

  // Désactiver les interruptions WiFi pendant la transmission critique
  // pour éviter le flickering
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&mux);

  esp_err_t ret = rmt_transmit(led_chan, led_encoder, led_data,
                               led_count * 3, &tx_config);

  portEXIT_CRITICAL(&mux);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG_LED, "Erreur transmission RMT: %s", esp_err_to_name(ret));
    return;
  }

  // Attendre la fin de la transmission (sans section critique)
  ret = rmt_tx_wait_all_done(led_chan, pdMS_TO_TICKS(200));
  if (ret == ESP_ERR_TIMEOUT) {
    ESP_LOGE(TAG_LED, "Timeout transmission RMT");
    rmt_disable(led_chan);
    rmt_enable(led_chan);
  } else if (ret != ESP_OK) {
    ESP_LOGE(TAG_LED, "Erreur rmt_tx_wait_all_done: %s", esp_err_to_name(ret));
  }
}

static uint16_t sanitize_led_count(uint16_t requested) {
  if (requested == 0) {
    ESP_LOGW(TAG_LED, "Configuration LED vide, retour à %d LEDs par défaut",
             NUM_LEDS);
    return NUM_LEDS;
  }

  if (requested > MAX_LED_COUNT) {
    ESP_LOGW(TAG_LED, "Configuration LED trop grande (%d), max %d appliqué",
             requested, MAX_LED_COUNT);
    return MAX_LED_COUNT;
  }

  return requested;
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
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .gpio_num = LED_PIN,
      .mem_block_symbols = 256,
      .resolution_hz = 10000000,
      .trans_queue_depth = 4,
      .flags.invert_out = false,
#if SOC_RMT_SUPPORT_DMA
      .flags.with_dma = true,
#else
      .flags.with_dma = false,
#endif
  };

  esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &led_chan);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_LED, "Erreur création canal RMT TX: %s", esp_err_to_name(ret));
    return false;
  }

  led_strip_encoder_config_t encoder_config = {
      .resolution = tx_chan_config.resolution_hz,
  };

  ret = rmt_new_led_strip_encoder(&encoder_config, &led_encoder);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_LED, "Erreur création encodeur LED: %s", esp_err_to_name(ret));
    cleanup_rmt_channel();
    return false;
  }

  ret = rmt_enable(led_chan);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_LED, "Erreur activation canal RMT: %s", esp_err_to_name(ret));
    cleanup_rmt_channel();
    return false;
  }

  return true;
}

// Remplit toutes les LEDs avec une couleur
static void fill_solid(rgb_t color) {
  for (int i = 0; i < led_count; i++) {
    leds[i] = color;
  }
}

// Effet: Couleur unie
static void effect_solid(void) {
  rgb_t color = color_to_rgb(current_config.color1);
  color = apply_brightness(color, current_config.brightness);
  fill_solid(color);
}

// Effet: Respiration
static void effect_breathing(void) {
  float breath =
      (sin(effect_counter * 0.01f * current_config.speed / 10.0f) + 1.0f) /
      2.0f;
  uint8_t brightness = (uint8_t)(current_config.brightness * breath);

  rgb_t color = color_to_rgb(current_config.color1);
  color = apply_brightness(color, brightness);
  fill_solid(color);
}

// Effet: Arc-en-ciel
static void effect_rainbow(void) {
  // Utiliser la vitesse pour contrôler la vitesse d'animation
  uint32_t speed_factor = (effect_counter * (current_config.speed + 10)) / 50;

  for (int i = 0; i < led_count; i++) {
    uint16_t hue = (i * 256 / led_count + speed_factor) % 256;
    rgb_t color = hsv_to_rgb(hue, 255, 255); // Utiliser luminosité max pour HSV
    color = apply_brightness(
        color, current_config.brightness); // Appliquer brightness ET mode nuit
    leds[i] = color;
  }
}

// Effet: Arc-en-ciel cyclique
static void effect_rainbow_cycle(void) {
  // Vitesse contrôle la rapidité du cycle (speed: 0-255)
  uint8_t speed_factor = current_config.speed;
  if (speed_factor < 10)
    speed_factor = 10; // Minimum pour éviter problèmes

  uint16_t hue = ((effect_counter * speed_factor) / 50) % 256;
  rgb_t color = hsv_to_rgb(hue, 255, 255); // Utiliser luminosité max pour HSV
  color = apply_brightness(
      color, current_config.brightness); // Appliquer brightness ET mode nuit
  fill_solid(color);
}

// Effet: Theater Chase
static void effect_theater_chase(void) {
  rgb_t color1 = color_to_rgb(current_config.color1);
  rgb_t color2 = {0, 0, 0};

  color1 = apply_brightness(color1, current_config.brightness);

  // Utiliser la vitesse pour contrôler la vitesse de défilement
  int speed_divider = 256 - current_config.speed;
  if (speed_divider < 20)
    speed_divider = 20;
  int pos = (effect_counter * 10 / speed_divider) % 3;

  for (int i = 0; i < led_count; i++) {
    if (i % 3 == pos) {
      leds[i] = color1;
    } else {
      leds[i] = color2;
    }
  }
}

// Effet: Running Lights
static void effect_running_lights(void) {
  // Utiliser la vitesse pour contrôler la vitesse de défilement
  int speed_divider = 256 - current_config.speed;
  if (speed_divider < 10)
    speed_divider = 10;
  int pos = (effect_counter * 100 / speed_divider) % led_count;

  rgb_t color = color_to_rgb(current_config.color1);

  for (int i = 0; i < led_count; i++) {
    int distance = abs(i - pos);
    if (distance > led_count / 2) {
      distance = led_count - distance;
    }

    uint8_t brightness =
        current_config.brightness * (led_count - distance * 2) / led_count;
    leds[i] = apply_brightness(color, brightness);
  }
}

// Effet: Twinkle
static void effect_twinkle(void) {
  // Diminuer progressivement toutes les LEDs
  for (int i = 0; i < led_count; i++) {
    leds[i].r = leds[i].r * 95 / 100;
    leds[i].g = leds[i].g * 95 / 100;
    leds[i].b = leds[i].b * 95 / 100;
  }

  // Allumer aléatoirement quelques LEDs
  if (esp_random() % 10 < current_config.speed / 25) {
    int pos = esp_random() % led_count;
    rgb_t color = color_to_rgb(current_config.color1);
    leds[pos] = apply_brightness(color, current_config.brightness);
  }
}

// Effet: Feu
static uint8_t heat_map[MAX_LED_COUNT]; // Carte de chaleur pour l'effet feu

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

  // Allumage aléatoire de nouvelles "flammes" sur toute la strip
  // Créer plusieurs points d'allumage pour un effet uniforme
  int num_sparks =
      3 + (current_config.speed / 50); // Plus rapide = plus de flammes
  for (int s = 0; s < num_sparks; s++) {
    if (esp_random() % 255 < 120) {
      int pos = esp_random() % led_count; // Sur toute la strip
      heat_map[pos] = heat_map[pos] + (esp_random() % 160) + 95;
      if (heat_map[pos] > 255)
        heat_map[pos] = 255;
    }
  }

  // Conversion de la chaleur en couleurs (palette de feu)
  for (int i = 0; i < led_count; i++) {
    rgb_t color;
    uint8_t heat = heat_map[i];

    // Palette: Noir -> Rouge -> Orange -> Jaune -> Blanc
    if (heat < 85) {
      // Noir à rouge foncé
      color.r = (heat * 3);
      color.g = 0;
      color.b = 0;
    } else if (heat < 170) {
      // Rouge à orange/jaune
      color.r = 255;
      color.g = ((heat - 85) * 3);
      color.b = 0;
    } else {
      // Orange/jaune à blanc chaud
      color.r = 255;
      color.g = 255;
      color.b = ((heat - 170) * 2);
    }

    leds[i] = apply_brightness(color, current_config.brightness);
  }
}

// Effet: Scan (Knight Rider)
static void effect_scan(void) {
  // Faire un fade progressif au lieu d'effacer complètement pour garder la
  // traînée
  for (int i = 0; i < led_count; i++) {
    leds[i].r = (leds[i].r * 90) / 100; // Réduire de 10% à chaque frame
    leds[i].g = (leds[i].g * 90) / 100;
    leds[i].b = (leds[i].b * 90) / 100;
  }

  // Utiliser la vitesse pour contrôler la vitesse de défilement
  int speed_divider = 256 - current_config.speed;
  if (speed_divider < 10)
    speed_divider = 10;
  int pos = (effect_counter * 100 / speed_divider) % (led_count * 2);

  if (pos >= led_count) {
    pos = led_count * 2 - pos - 1;
  }

  rgb_t base_color = color_to_rgb(current_config.color1);

  // LED principale plus brillante
  if (pos >= 0 && pos < led_count) {
    leds[pos] = apply_brightness(base_color, current_config.brightness);
  }

  // Traînée dégradée symétrique des deux côtés
  // La traînée diminue progressivement en luminosité
  for (int i = 1; i <= 5; i++) {
    // Calculer la luminosité de la traînée (diminue avec la distance)
    uint8_t trail_brightness = current_config.brightness * (6 - i) / 6;
    rgb_t trail_color = apply_brightness(base_color, trail_brightness);

    // Appliquer la traînée des deux côtés, mais seulement dans les limites du
    // strip
    if (pos - i >= 0 && pos - i < led_count) {
      leds[pos - i] = trail_color;
    }
    if (pos + i >= 0 && pos + i < led_count) {
      leds[pos + i] = trail_color;
    }
  }
}

// Effet: Knight Rider (K2000 - traînée nette)
static void effect_knight_rider(void) {
  // Effacer complètement le strip pour avoir une traînée nette
  fill_solid((rgb_t){0, 0, 0});

  // Utiliser la vitesse pour contrôler la vitesse de défilement
  int speed_divider = 256 - current_config.speed;
  if (speed_divider < 10)
    speed_divider = 10;
  int pos = (effect_counter * 100 / speed_divider) % (led_count * 2);

  if (pos >= led_count) {
    pos = led_count * 2 - pos - 1;
  }

  rgb_t base_color = color_to_rgb(current_config.color1);

  // LED principale à pleine luminosité
  if (pos >= 0 && pos < led_count) {
    leds[pos] = apply_brightness(base_color, current_config.brightness);
  }

  // Traînée nette et courte (3 LEDs de chaque côté au lieu de 5)
  // Dégradation rapide pour un effet K2000 authentique
  for (int i = 1; i <= 3; i++) {
    // Calculer la luminosité de la traînée avec dégradation exponentielle
    uint8_t trail_brightness =
        current_config.brightness / (1 << i); // Division par 2, 4, 8
    rgb_t trail_color = apply_brightness(base_color, trail_brightness);

    // Appliquer la traînée des deux côtés
    if (pos - i >= 0 && pos - i < led_count) {
      leds[pos - i] = trail_color;
    }
    if (pos + i >= 0 && pos + i < led_count) {
      leds[pos + i] = trail_color;
    }
  }
}

// Effet: Fade (in/out fluide)
static void effect_fade(void) {
  // Calculer la période en fonction de la vitesse (speed: 0-255)
  uint8_t speed_factor = current_config.speed;
  if (speed_factor < 10)
    speed_factor = 10;

  // Période complète (fade in + fade out)
  uint16_t period =
      (256 - speed_factor) * 2; // Range: ~20 (fast) to ~512 (slow)
  uint16_t cycle = effect_counter % period;

  // Calculer la luminosité en triangle (0->255->0)
  uint8_t brightness;
  if (cycle < period / 2) {
    // Fade in (0 -> 255)
    brightness = (cycle * 255) / (period / 2);
  } else {
    // Fade out (255 -> 0)
    brightness = ((period - cycle) * 255) / (period / 2);
  }

  // Appliquer à la couleur
  rgb_t color = color_to_rgb(current_config.color1);
  uint8_t final_brightness = (brightness * current_config.brightness) / 255;
  color = apply_brightness(color, final_brightness);
  fill_solid(color);
}

// Effet: Strobe (flash de toute la strip)
static void effect_strobe(void) {
  rgb_t color = color_to_rgb(current_config.color1);
  color = apply_brightness(color, current_config.brightness);

  // Période de flash basée sur le paramètre speed
  // speed: 0-255, converti en période 10-50 frames (speed élevé = période
  // courte = rapide)
  int period =
      50 - ((current_config.speed * 40) / 255); // Range: 50 (slow) to 10 (fast)
  if (period < 10)
    period = 10;
  int cycle = effect_counter % period;

  // Flash actif pendant 30% du cycle
  int flash_duration = (period * 30) / 100;

  if (cycle < flash_duration) {
    // Allumer toute la strip
    fill_solid(color);
  } else {
    // Éteindre toute la strip
    fill_solid((rgb_t){0, 0, 0});
  }
}

// Effet: Flash angle mort directionnel (blindspot avec animation directionnelle
// rapide)
static void effect_blindspot_flash(void) {
  rgb_t color = color_to_rgb(current_config.color1);
  color = apply_brightness(color, current_config.brightness);

  int half_leds = led_count / 2;

  // Période de flash rapide basée sur le paramètre speed
  // speed: 0-255, converti en période 15-100 frames (speed élevé = période
  // courte = rapide)
  int period = 100 - ((current_config.speed * 85) /
                      255); // Range: 100 (slow) to 15 (fast)
  if (period < 15)
    period = 15;
  int cycle = effect_counter % period;

  // Flash actif pendant 60% du cycle (plus long que turn signal pour l'urgence)
  int animation_duration = (period * 60) / 100;

  // Effacer tout le ruban
  fill_solid((rgb_t){0, 0, 0});

  if (cycle < animation_duration) {
    // Animation rapide : allumer progressivement avec plus d'intensité
    int lit_count = (cycle * half_leds) / animation_duration;

    // Allumer les LEDs avec animation directionnelle depuis le centre
    for (int i = 0; i < lit_count && i < half_leds; i++) {
      int led_index;

      // Animation depuis le CENTRE vers l'extérieur
      if (current_config.reverse) {
        // Côté gauche: animation du centre (half_leds-1) vers la gauche (0)
        led_index = half_leds - 1 - i;
      } else {
        // Côté droit: animation du centre (half_leds) vers la droite
        // (led_count-1)
        led_index = half_leds + i;
      }

      // Intensité plus uniforme pour l'effet d'alerte
      float brightness_factor;
      if (i < lit_count - 3) {
        brightness_factor = 0.5f; // Queue à 50% (plus visible que turn signal)
      } else {
        brightness_factor = 1.0f; // Tête à 100%
      }

      leds[led_index] = apply_brightness(
          color, (uint8_t)(current_config.brightness * brightness_factor));
    }
  }
  // Pause plus courte pour effet d'alerte
}

// Effet: Clignotants avec défilement séquentiel (style moderne)
static void effect_turn_signal(void) {
  rgb_t color = color_to_rgb(current_config.color1);
  color = apply_brightness(color, current_config.brightness);

  // Configuration: utiliser la moitié du ruban selon la direction
  // reverse = true : côté gauche (animation du centre vers la gauche)
  // reverse = false : côté droit (animation du centre vers la droite)
  int half_leds = led_count / 2;

  // Période complète d'animation basée sur le paramètre speed
  // speed: 0-255, converti en période 20-120 frames (speed élevé = période
  // courte = rapide)
  int period = 120 - ((current_config.speed * 100) /
                      255); // Range: 120 (slow) to 20 (fast)
  if (period < 20)
    period = 20;
  int cycle = effect_counter % period;

  // Phase 1: Défilement progressif (70% du cycle)
  int animation_duration = (period * 70) / 100;

  // Effacer tout le ruban
  fill_solid((rgb_t){0, 0, 0});

  if (cycle < animation_duration) {
    // Défilement fluide : allumer progressivement
    int lit_count = (cycle * half_leds) / animation_duration;

    // Allumer les LEDs avec un dégradé pour la fluidité (même style que hazard)
    for (int i = 0; i < lit_count && i < half_leds; i++) {
      int led_index;

      // Animation depuis le CENTRE vers l'extérieur
      if (current_config.reverse) {
        // Côté gauche: animation du centre (half_leds-1) vers la gauche (0)
        led_index = half_leds - 1 - i;
      } else {
        // Côté droit: animation du centre (half_leds) vers la droite
        // (led_count-1)
        led_index = half_leds + i;
      }

      // Luminosité décroissante : même style que hazard
      float brightness_factor;
      if (i < lit_count - 5) {
        brightness_factor = 0.3f; // Queue à 30%
      } else {
        brightness_factor = 1.0f; // Tête à 100%
      }

      leds[led_index] = apply_brightness(
          color, (uint8_t)(current_config.brightness * brightness_factor));
    }
  }
  // Sinon tout reste éteint (pause)
}

// Effet: Warnings (clignotants des deux côtés simultanément)
static void effect_hazard(void) {
  rgb_t color = color_to_rgb(current_config.color1);
  color = apply_brightness(color, current_config.brightness);

  int half_leds = led_count / 2;

  // Période complète d'animation basée sur le paramètre speed
  // speed: 0-255, converti en période 20-120 frames (speed élevé = période
  // courte = rapide)
  int period = 120 - ((current_config.speed * 100) /
                      255); // Range: 120 (slow) to 20 (fast)
  if (period < 20)
    period = 20;
  int cycle = effect_counter % period;

  // Phase 1: Défilement progressif (70% du cycle)
  int animation_duration = (period * 70) / 100;

  // Effacer tout le ruban
  fill_solid((rgb_t){0, 0, 0});

  if (cycle < animation_duration) {
    // Défilement fluide : allumer progressivement les deux côtés depuis le
    // centre
    int lit_count = (cycle * half_leds) / animation_duration;

    // Allumer les LEDs des deux côtés avec un dégradé pour la fluidité
    for (int i = 0; i < lit_count && i < half_leds; i++) {
      // Luminosité décroissante : le début (queue) est moins brillant
      float brightness_factor;
      if (i < lit_count - 5) {
        brightness_factor = 0.3f; // Queue à 30%
      } else {
        brightness_factor = 1.0f; // Tête à 100%
      }

      rgb_t dimmed_color = apply_brightness(
          color, (uint8_t)(current_config.brightness * brightness_factor));

      // Côté gauche: animation du centre (half_leds-1) vers la gauche (0)
      leds[half_leds - 1 - i] = dimmed_color;

      // Côté droit: animation du centre (half_leds) vers la droite (led_count-1)
      leds[half_leds + i] = dimmed_color;
    }
  }
  // Sinon tout reste éteint (pause)
}

// Effet: Feux de stop
static void effect_brake_light(void) {
  rgb_t color =
      last_vehicle_state.brake_pressed ? (rgb_t){255, 0, 0} : (rgb_t){64, 0, 0};
  color = apply_brightness(color, current_config.brightness);
  fill_solid(color);
}

// Effet: État de charge
static uint8_t simulated_charge = 0; // Niveau de charge simulé (0-100)

static void effect_charge_status(void) {
  // Simuler l'augmentation progressive de la charge
  // Augmente ULTRA lentement jusqu'à 100%, puis redémarre
  if (effect_counter % 50 ==
      0) { // Mise à jour toutes les 50 frames (ralenti 10x)
    simulated_charge++;
    if (simulated_charge > 100)
      simulated_charge = 0;
  }

  // Utiliser le niveau de charge simulé ou réel
  uint8_t charge_level = last_vehicle_state.charging
                             ? last_vehicle_state.soc_percent
                             : simulated_charge;

  int target_led = (led_count * charge_level) / 100;
  if (target_led >= led_count)
    target_led = led_count - 1;

  // Effacer tout
  fill_solid((rgb_t){0, 0, 0});

  // Afficher la barre de charge (statique) avec couleur selon le niveau
  for (int i = 0; i < target_led; i++) {
    rgb_t color;

    if (charge_level < 20) {
      // Rouge en dessous de 20%
      color = (rgb_t){255, 0, 0};
    } else if (charge_level < 50) {
      // Jaune/Orange entre 20% et 50%
      color = (rgb_t){255, 200, 0};
    } else if (charge_level < 80) {
      // Jaune/Vert entre 50% et 80%
      color = (rgb_t){200, 255, 0};
    } else {
      // Vert au-dessus de 80%
      color = (rgb_t){0, 255, 0};
    }

    leds[i] = apply_brightness(color, current_config.brightness);
  }

  // Pixel animé qui vient de l'extrémité (empilement fluide)
  // Vitesse de l'animation basée sur la puissance de charge CAN
  float speed_factor; // Pixels par frame (plus = plus rapide)

  if (last_vehicle_state.charging &&
      last_vehicle_state.charge_power_kw > 0.1f) {
    // En charge réelle : vitesse proportionnelle à la puissance
    // Supercharger V3 max = 250 kW, V2 = 150 kW, AC = 11 kW
    float power = last_vehicle_state.charge_power_kw;

    // Normaliser :
    // 3 kW (prise) -> 0.017 px/frame (lent)
    // 150 kW (V2)  -> 0.5 px/frame (rapide)
    // 250 kW (V3)  -> 1.0 px/frame (très rapide)
    speed_factor = power / 250.0f; // Max 1 pixel par frame

    // Limites
    if (speed_factor < 0.017f)
      speed_factor = 0.017f;
    if (speed_factor > 1.0f)
      speed_factor = 1.0f;
  } else {
    // Mode simulation : vitesse basée sur le paramètre speed
    // speed 0 -> 0.017 px/frame (lent)
    // speed 255 -> 1.0 px/frame (rapide)
    speed_factor = 0.017f + (current_config.speed / 255.0f) * 0.983f;
  }

  // Position du pixel: commence au bout (led_count-1) et va vers 0
  // Cycle complet: du bout jusqu'au niveau de charge + longueur de la traînée
  // Cela permet à toute la traînée d'être "avalée" avant de recommencer
  const int TRAIL_LENGTH = 5;
  int cycle_length = led_count - target_led + TRAIL_LENGTH;

  if (cycle_length > TRAIL_LENGTH) {
    // Incrémenter l'accumulateur de position de manière fluide
    charge_anim_position += speed_factor;

    // Réinitialiser l'accumulateur quand le cycle est terminé
    if (charge_anim_position >= (float)cycle_length) {
      charge_anim_position -= (float)cycle_length;
    }

    // Position actuelle du pixel (conversion en entier pour affichage)
    int anim_pos = (int)charge_anim_position;
    int moving_pixel_pos = led_count - 1 - anim_pos;

    // Extraire les composantes RGB de la couleur configurée
    rgb_t trail_color = color_to_rgb(current_config.color1);

    // Pixel principal brillant (couleur configurée)
    // Afficher seulement si dans la zone visible
    if (moving_pixel_pos >= 0 && moving_pixel_pos < led_count) {
      leds[moving_pixel_pos] =
          apply_brightness(trail_color, current_config.brightness);
    }

    // Traînée de la même couleur derrière le pixel (8 pixels pour traînée
    // longue et fluide)
    for (int trail = 1; trail <= TRAIL_LENGTH; trail++) {
      int trail_pos = moving_pixel_pos + trail;

      // Afficher la traînée seulement si:
      // 1. Elle est dans la zone visible (< led_count)
      // 2. Elle n'a pas encore été "avalée" par la barre de charge (>=
      // target_led)
      if (trail_pos >= target_led && trail_pos < led_count) {
        // Décroissance exponentielle agressive pour une traînée bien visible
        // trail 1 = 80% (204), trail 2 = 60% (153), trail 3 = 40% (102), trail
        // 4 = 25% (64), trail 5 = 10% (25)
        uint8_t fade_factor = 255 - (trail * trail * trail * 255) / 125;

        // Appliquer la traînée colorée avec fade progressif
        // Réduire l'intensité de chaque composante RGB selon le fade_factor
        rgb_t faded_color = {(trail_color.r * fade_factor) / 255,
                             (trail_color.g * fade_factor) / 255,
                             (trail_color.b * fade_factor) / 255};

        leds[trail_pos] =
            apply_brightness(faded_color, current_config.brightness);
      }
    }
  }
}

// Effet: Synchronisation véhicule
static void effect_vehicle_sync(void) {
  // Combine plusieurs indicateurs
  rgb_t base_color = {0, 0, 0};

  // Portes ouvertes = rouge
  if (last_vehicle_state.doors_open_count > 0) {
    base_color = (rgb_t){255, 0, 0};
  }
  // En charge = vert
  else if (last_vehicle_state.charging) {
    base_color = (rgb_t){0, 255, 0};
  }
  // En mouvement = bleu
  else if (last_vehicle_state.speed_kph > 5.0f) {
    uint8_t intensity = (uint8_t)(last_vehicle_state.speed_kph * 2);
    if (intensity > 255)
      intensity = 255;
    base_color = (rgb_t){0, 0, intensity};
  }
  // Verrouillé = blanc faible
  else if (last_vehicle_state.locked) {
    base_color = (rgb_t){32, 32, 32};
  }

  base_color = apply_brightness(base_color, current_config.brightness);
  fill_solid(base_color);
}

// Effet: Audio Réactif (VU-mètre)
static void effect_audio_reactive(void) {
  audio_data_t audio_data;
  if (!audio_input_get_data(&audio_data)) {
    // Pas de données audio, afficher noir
    fill_solid((rgb_t){0, 0, 0});
    return;
  }

  // VU-mètre: remplir selon l'amplitude
  int lit_leds = (int)(audio_data.amplitude * led_count);
  if (lit_leds > led_count) lit_leds = led_count;

  rgb_t base_color = color_to_rgb(current_config.color1);

  for (int i = 0; i < led_count; i++) {
    if (i < lit_leds) {
      // Couleur dégradée selon le niveau
      float intensity = (float)(i + 1) / lit_leds;
      rgb_t color;
      color.r = (uint8_t)(base_color.r * intensity);
      color.g = (uint8_t)(base_color.g * intensity);
      color.b = (uint8_t)(base_color.b * intensity);
      leds[i] = apply_brightness(color, current_config.brightness);
    } else {
      leds[i] = (rgb_t){0, 0, 0};
    }
  }
}

// Effet: Audio BPM (flash sur les battements)
static void effect_audio_bpm(void) {
  audio_data_t audio_data;
  if (!audio_input_get_data(&audio_data)) {
    // Pas de données audio, afficher noir
    fill_solid((rgb_t){0, 0, 0});
    return;
  }

  rgb_t color = color_to_rgb(current_config.color1);

  // Si battement détecté récemment (dans les dernières 100ms)
  uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
  uint32_t time_since_beat = now - audio_data.last_beat_ms;

  if (audio_data.beat_detected || time_since_beat < 100) {
    // Flash sur le battement avec decay
    float decay = 1.0f - (time_since_beat / 100.0f);
    if (decay < 0.0f) decay = 0.0f;

    uint8_t flash_brightness = (uint8_t)(current_config.brightness * decay);
    color = apply_brightness(color, flash_brightness);
    fill_solid(color);
  } else {
    // Couleur faible entre les battements
    color = apply_brightness(color, current_config.brightness / 4);
    fill_solid(color);
  }
}

// ============================================================================
// EFFETS FFT AVANCÉS
// ============================================================================

// Effet FFT: Spectre complet (égaliseur visuel)
static void effect_fft_spectrum(void) {
  audio_fft_data_t fft_data;
  if (!audio_input_get_fft_data(&fft_data)) {
    // FFT non disponible, afficher noir
    fill_solid((rgb_t){0, 0, 0});
    return;
  }

  // Nombre de LEDs par bande
  int leds_per_band = led_count / AUDIO_FFT_BANDS;
  if (leds_per_band < 1) leds_per_band = 1;

  for (int band = 0; band < AUDIO_FFT_BANDS; band++) {
    // Couleur arc-en-ciel selon la bande (bass=rouge, treble=bleu)
    uint8_t hue = (band * 255) / AUDIO_FFT_BANDS;
    rgb_t band_color = hsv_to_rgb(hue, 255, 255);

    // Hauteur proportionnelle au niveau de la bande
    int height = (int)(fft_data.bands[band] * leds_per_band);
    if (height > leds_per_band) height = leds_per_band;

    // Allumer les LEDs pour cette bande
    for (int i = 0; i < leds_per_band && (band * leds_per_band + i) < led_count; i++) {
      int led_idx = band * leds_per_band + i;
      if (i < height) {
        // Dégradé de bas en haut
        float intensity = (float)(i + 1) / height;
        rgb_t color;
        color.r = (uint8_t)(band_color.r * intensity);
        color.g = (uint8_t)(band_color.g * intensity);
        color.b = (uint8_t)(band_color.b * intensity);
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

  rgb_t color = color_to_rgb(current_config.color1);

  // Pulse basé sur l'énergie des basses
  float bass_intensity = fft_data.bass_energy;

  // Effet de pulse si kick détecté
  if (fft_data.kick_detected) {
    bass_intensity = 1.0f; // Flash complet
  }

  // Appliquer l'intensité
  uint8_t pulse_brightness = (uint8_t)(current_config.brightness * bass_intensity);
  color = apply_brightness(color, pulse_brightness);
  fill_solid(color);
}

// Effet FFT: Vocal Wave (vague réactive aux voix)
static void effect_fft_vocal_wave(void) {
  audio_fft_data_t fft_data;
  if (!audio_input_get_fft_data(&fft_data)) {
    fill_solid((rgb_t){0, 0, 0});
    return;
  }

  rgb_t base_color = color_to_rgb(current_config.color1);
  rgb_t vocal_color = color_to_rgb(current_config.color2);

  // Position de la vague basée sur le centroïde spectral
  // Plus le centroïde est haut (aigus), plus la vague avance
  float wave_pos = (fft_data.spectral_centroid - 500.0f) / 3500.0f; // Normaliser 500-4000 Hz
  if (wave_pos < 0.0f) wave_pos = 0.0f;
  if (wave_pos > 1.0f) wave_pos = 1.0f;

  int center = (int)(wave_pos * led_count);
  int width = (int)(fft_data.mid_energy * 20.0f); // Largeur proportionnelle à l'énergie mid

  for (int i = 0; i < led_count; i++) {
    int distance = abs(i - center);
    if (distance < width) {
      // Dégradé vers la voix
      float mix = (float)distance / width;
      rgb_t color;
      color.r = (uint8_t)(vocal_color.r * (1.0f - mix) + base_color.r * mix);
      color.g = (uint8_t)(vocal_color.g * (1.0f - mix) + base_color.g * mix);
      color.b = (uint8_t)(vocal_color.b * (1.0f - mix) + base_color.b * mix);
      leds[i] = apply_brightness(color, current_config.brightness);
    } else {
      leds[i] = apply_brightness(base_color, current_config.brightness / 4);
    }
  }
}

// Effet FFT: Energy Bar (barre d'énergie spectrale)
static void effect_fft_energy_bar(void) {
  audio_fft_data_t fft_data;
  if (!audio_input_get_fft_data(&fft_data)) {
    fill_solid((rgb_t){0, 0, 0});
    return;
  }

  // Diviser le ruban en 3 sections: Bass, Mid, Treble
  int section_size = led_count / 3;

  // Section Bass (rouge)
  int bass_leds = (int)(fft_data.bass_energy * section_size);
  for (int i = 0; i < section_size; i++) {
    if (i < bass_leds) {
      rgb_t color = {255, 0, 0}; // Rouge
      leds[i] = apply_brightness(color, current_config.brightness);
    } else {
      leds[i] = (rgb_t){0, 0, 0};
    }
  }

  // Section Mid (vert)
  int mid_leds = (int)(fft_data.mid_energy * section_size);
  for (int i = 0; i < section_size; i++) {
    int led_idx = section_size + i;
    if (i < mid_leds && led_idx < led_count) {
      rgb_t color = {0, 255, 0}; // Vert
      leds[led_idx] = apply_brightness(color, current_config.brightness);
    } else if (led_idx < led_count) {
      leds[led_idx] = (rgb_t){0, 0, 0};
    }
  }

  // Section Treble (bleu)
  int treble_leds = (int)(fft_data.treble_energy * section_size);
  for (int i = 0; i < section_size; i++) {
    int led_idx = section_size * 2 + i;
    if (i < treble_leds && led_idx < led_count) {
      rgb_t color = {0, 0, 255}; // Bleu
      leds[led_idx] = apply_brightness(color, current_config.brightness);
    } else if (led_idx < led_count) {
      leds[led_idx] = (rgb_t){0, 0, 0};
    }
  }
}

// Table des fonctions d'effets
typedef void (*effect_func_t)(void);
static const effect_func_t effect_functions[] = {
    [EFFECT_OFF] = NULL,
    [EFFECT_SOLID] = effect_solid,
    [EFFECT_BREATHING] = effect_breathing,
    [EFFECT_RAINBOW] = effect_rainbow,
    [EFFECT_RAINBOW_CYCLE] = effect_rainbow_cycle,
    [EFFECT_THEATER_CHASE] = effect_theater_chase,
    [EFFECT_RUNNING_LIGHTS] = effect_running_lights,
    [EFFECT_TWINKLE] = effect_twinkle,
    [EFFECT_FIRE] = effect_fire,
    [EFFECT_SCAN] = effect_scan,
    [EFFECT_KNIGHT_RIDER] = effect_knight_rider,
    [EFFECT_FADE] = effect_fade,
    [EFFECT_STROBE] = effect_strobe,
    [EFFECT_VEHICLE_SYNC] = effect_vehicle_sync,
    [EFFECT_TURN_SIGNAL] = effect_turn_signal,
    [EFFECT_HAZARD] = effect_hazard,
    [EFFECT_BRAKE_LIGHT] = effect_brake_light,
    [EFFECT_CHARGE_STATUS] = effect_charge_status,
    [EFFECT_BLINDSPOT_FLASH] = effect_blindspot_flash,
    [EFFECT_AUDIO_REACTIVE] = effect_audio_reactive,
    [EFFECT_AUDIO_BPM] = effect_audio_bpm,
    [EFFECT_FFT_SPECTRUM] = effect_fft_spectrum,
    [EFFECT_FFT_BASS_PULSE] = effect_fft_bass_pulse,
    [EFFECT_FFT_VOCAL_WAVE] = effect_fft_vocal_wave,
    [EFFECT_FFT_ENERGY_BAR] = effect_fft_energy_bar,
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
    {EFFECT_BLINDSPOT_FLASH, EFFECT_ID_BLINDSPOT_FLASH, "Blindspot Flash",
     true},
    {EFFECT_AUDIO_REACTIVE, EFFECT_ID_AUDIO_REACTIVE, "Audio Reactive", false},
    {EFFECT_AUDIO_BPM, EFFECT_ID_AUDIO_BPM, "Audio BPM", false},
    {EFFECT_FFT_SPECTRUM, EFFECT_ID_FFT_SPECTRUM, "FFT Spectrum", false},
    {EFFECT_FFT_BASS_PULSE, EFFECT_ID_FFT_BASS_PULSE, "FFT Bass Pulse", false},
    {EFFECT_FFT_VOCAL_WAVE, EFFECT_ID_FFT_VOCAL_WAVE, "FFT Vocal Wave", false},
    {EFFECT_FFT_ENERGY_BAR, EFFECT_ID_FFT_ENERGY_BAR, "FFT Energy Bar", false},
};

#define EFFECT_DESCRIPTOR_COUNT                                                \
  (sizeof(effect_descriptors) / sizeof(effect_descriptors[0]))
typedef char effect_descriptor_size_mismatch
    [(EFFECT_MAX == EFFECT_DESCRIPTOR_COUNT) ? 1 : -1];

static const led_effect_descriptor_t *
find_effect_descriptor(led_effect_t effect) {
  for (size_t i = 0; i < EFFECT_DESCRIPTOR_COUNT; ++i) {
    if (effect_descriptors[i].effect == effect) {
      return &effect_descriptors[i];
    }
  }
  return NULL;
}

static const led_effect_descriptor_t *
find_effect_descriptor_by_id(const char *id) {
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
  led_count = sanitize_led_count(configured_leds);

  if (!configure_rmt_channel()) {
    return false;
  }

  // Configuration par défaut
  led_effects_reset_config();

  // Charger la config sauvegardée
  led_effects_load_config();

  ESP_LOGI(TAG_LED, "LEDs initialisées (%d LEDs sur GPIO %d)", led_count, LED_PIN);
  return true;
}

void led_effects_deinit(void) {
  // Éteindre toutes les LEDs
  fill_solid((rgb_t){0, 0, 0});
  led_strip_show();

  cleanup_rmt_channel();

  ESP_LOGI(TAG_LED, "LEDs désinitalisées");
}

bool led_effects_set_led_count(uint16_t requested_led_count) {
  if (requested_led_count < 1 || requested_led_count > MAX_LED_COUNT) {
    ESP_LOGE(TAG_LED, "LED count %d invalide", requested_led_count);
    return false;
  }

  led_count = requested_led_count;
  ESP_LOGI(TAG_LED, "Nombre de LEDs mis à jour: %d", led_count);
  return true;
}

void led_effects_set_config(const effect_config_t *config) {
  if (config != NULL) {
    memcpy(&current_config, config, sizeof(effect_config_t));

    // Activer/désactiver automatiquement le FFT selon l'effet
    bool needs_fft = led_effects_requires_fft(current_config.effect);
    audio_input_set_fft_enabled(needs_fft);

    ESP_LOGI(TAG_LED, "Effet configuré: %d, audio_reactive=%d, FFT %s",
             current_config.effect, current_config.audio_reactive,
             needs_fft ? "activé" : "désactivé");
  }
}

void led_effects_set_dual_directional(const effect_config_t *left_config,
                                      const effect_config_t *right_config) {
  // Active le mode dual directionnel et stocke les configurations
  dual_directional_mode = true;

  if (left_config != NULL) {
    memcpy(&left_directional_config, left_config, sizeof(effect_config_t));
    left_directional_config.reverse = true; // Force côté gauche
  }

  if (right_config != NULL) {
    memcpy(&right_directional_config, right_config, sizeof(effect_config_t));
    right_directional_config.reverse = false; // Force côté droit
  }
}

void led_effects_get_config(effect_config_t *config) {
  if (config != NULL) {
    memcpy(config, &current_config, sizeof(effect_config_t));
  }
}

void led_effects_update(void) {
  if (!enabled && !ota_progress_mode && !ota_ready_mode && !ota_error_mode) {
    fill_solid((rgb_t){0, 0, 0});
    led_strip_show();
    dual_directional_mode = false;
    return;
  }

  if (ota_progress_mode) {
    TickType_t now = xTaskGetTickCount();
    bool needs_refresh = false;

    if (ota_progress_percent != ota_displayed_percent) {
      ota_displayed_percent = ota_progress_percent;
      needs_refresh = true;
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

  // Mode dual directionnel (deux effets simultanés gauche/droite)
  if (dual_directional_mode) {
    int half = led_count / 2;

    // Initialiser à noir
    for (int i = 0; i < half; i++) {
      left_led_buffer[i] = (rgb_t){0, 0, 0};
      right_led_buffer[i] = (rgb_t){0, 0, 0};
    }

    // Rendu effet gauche
    if (left_directional_config.effect != EFFECT_OFF) {
      effect_config_t saved = current_config;
      memcpy(&current_config, &left_directional_config,
             sizeof(effect_config_t));
      current_config.zone = LED_ZONE_LEFT; // Force zone gauche
      fill_solid((rgb_t){0, 0, 0});

      if (current_config.effect < EFFECT_MAX &&
          effect_functions[current_config.effect] != NULL) {
        effect_functions[current_config.effect]();
      }

      // Appliquer le masque de zone AVANT de copier
      apply_zone_mask(LED_ZONE_LEFT);

      for (int i = 0; i < half; i++) {
        left_led_buffer[i] = leds[i];
      }
      memcpy(&current_config, &saved, sizeof(effect_config_t));
    }

    // Rendu effet droit
    if (right_directional_config.effect != EFFECT_OFF) {
      memcpy(&current_config, &right_directional_config,
             sizeof(effect_config_t));
      current_config.zone = LED_ZONE_RIGHT; // Force zone droite
      fill_solid((rgb_t){0, 0, 0});

      if (current_config.effect < EFFECT_MAX &&
          effect_functions[current_config.effect] != NULL) {
        effect_functions[current_config.effect]();
      }

      // Appliquer le masque de zone AVANT de copier
      apply_zone_mask(LED_ZONE_RIGHT);

      for (int i = 0; i < half; i++) {
        right_led_buffer[i] = leds[half + i];
      }
    }

    // Combiner
    for (int i = 0; i < half; i++) {
      leds[i] = left_led_buffer[i];
      leds[half + i] = right_led_buffer[i];
    }

    // Réinitialiser le mode après affichage
    dual_directional_mode = false;
  } else {
    // Mode normal: un seul effet
    if (current_config.effect == EFFECT_OFF) {
      fill_solid((rgb_t){0, 0, 0});
    } else if (current_config.effect < EFFECT_MAX &&
               effect_functions[current_config.effect] != NULL) {
      effect_functions[current_config.effect]();
      // Appliquer le masque de zone après le rendu
      apply_zone_mask(current_config.zone);
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

void led_effects_set_enabled(bool enable) {
  enabled = enable;
  if (!enabled) {
    fill_solid((rgb_t){0, 0, 0});
    led_strip_show();
  }
}

void led_effects_set_brightness(uint8_t brightness) {
  current_config.brightness = brightness;
}

void led_effects_set_reverse(bool reverse) {
  global_reverse_direction = reverse;
}

bool led_effects_get_reverse(void) { return global_reverse_direction; }

void led_effects_set_speed(uint8_t speed) { current_config.speed = speed; }

void led_effects_set_solid_color(uint32_t color) {
  current_config.effect = EFFECT_SOLID;
  current_config.color1 = color;
}

void led_effects_start_progress_display(void) {
  ota_ready_mode = false;
  ota_error_mode = false;
  ota_progress_mode = true;
  ota_progress_percent = 0;
  ota_displayed_percent = 255;
  ota_last_progress_refresh = 0;
  dual_directional_mode = false;
}

void led_effects_update_progress(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  ota_progress_percent = percent;
}

void led_effects_stop_progress_display(void) {
  ota_progress_mode = false;
  ota_progress_percent = 0;
  ota_ready_mode = false;
  ota_error_mode = false;
  ota_displayed_percent = 255;
  ota_last_progress_refresh = 0;
}

void led_effects_show_upgrade_ready(void) {
  ota_progress_mode = false;
  ota_progress_percent = 100;
  ota_error_mode = false;
  ota_ready_mode = true;
  ota_displayed_percent = 255;
}

void led_effects_show_upgrade_error(void) {
  ota_progress_mode = false;
  ota_ready_mode = false;
  ota_error_mode = true;
  ota_displayed_percent = 255;
}

const char *led_effects_get_name(led_effect_t effect) {
  const led_effect_descriptor_t *desc = find_effect_descriptor(effect);
  return desc ? desc->name : "Unknown";
}

// Table de correspondance enum -> ID alphanumérique
const char *led_effects_enum_to_id(led_effect_t effect) {
  const led_effect_descriptor_t *desc = find_effect_descriptor(effect);
  return desc ? desc->id : EFFECT_ID_OFF;
}

// Table de correspondance ID alphanumérique -> enum
led_effect_t led_effects_id_to_enum(const char *id) {
  const led_effect_descriptor_t *desc = find_effect_descriptor_by_id(id);
  if (desc) {
    return desc->effect;
  }
  ESP_LOGW(TAG_LED, "ID d'effet inconnu: %s", id ? id : "NULL");
  return EFFECT_OFF;
}

// Vérifie si un effet nécessite des données CAN pour fonctionner
bool led_effects_requires_can(led_effect_t effect) {
  const led_effect_descriptor_t *desc = find_effect_descriptor(effect);
  return desc ? desc->requires_can : false;
}

bool led_effects_requires_fft(led_effect_t effect) {
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

bool led_effects_is_audio_effect(led_effect_t effect) {
  // Tous les effets audio ne peuvent pas être assignés aux événements CAN
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

bool led_effects_save_config(void) {
  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open("led_config", NVS_READWRITE, &nvs_handle);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG_LED, "Erreur ouverture NVS: %s", esp_err_to_name(ret));
    return false;
  }

  ret = nvs_set_blob(nvs_handle, "config", &current_config,
                     sizeof(effect_config_t));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_LED, "Erreur sauvegarde config: %s", esp_err_to_name(ret));
    nvs_close(nvs_handle);
    return false;
  }

  nvs_commit(nvs_handle);
  nvs_close(nvs_handle);

  ESP_LOGI(TAG_LED, "Configuration sauvegardée");
  return true;
}

bool led_effects_load_config(void) {
  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open("led_config", NVS_READONLY, &nvs_handle);

  if (ret != ESP_OK) {
    ESP_LOGW(TAG_LED, "Pas de config sauvegardée");
    return false;
  }

  size_t required_size = sizeof(effect_config_t);
  ret = nvs_get_blob(nvs_handle, "config", &current_config, &required_size);
  nvs_close(nvs_handle);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG_LED, "Erreur lecture config: %s", esp_err_to_name(ret));
    return false;
  }

  ESP_LOGI(TAG_LED, "Configuration chargée");
  return true;
}

void led_effects_reset_config(void) {
  current_config.effect = EFFECT_RAINBOW;
  current_config.brightness = DEFAULT_BRIGHTNESS;
  current_config.speed = DEFAULT_SPEED;
  current_config.color1 = 0xFF0000; // Rouge
  current_config.color2 = 0x00FF00; // Vert
  current_config.color3 = 0x0000FF; // Bleu
  current_config.sync_mode = SYNC_OFF;
  current_config.reverse = false;
  current_config.audio_reactive = false;

  ESP_LOGI(TAG_LED, "Configuration réinitialisée");
}

void led_effects_set_night_mode(bool active, uint8_t brightness) {
  night_mode_active = active;
  night_mode_brightness = brightness;
  ESP_LOGI(TAG_LED, "Night mode: %s, brightness: %d%%", active ? "ON" : "OFF",
           (brightness * 100) / 255);
}

bool led_effects_get_night_mode(void) { return night_mode_active; }

uint8_t led_effects_get_night_brightness(void) { return night_mode_brightness; }
