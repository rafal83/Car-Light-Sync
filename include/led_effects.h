#ifndef LED_EFFECTS_H
#define LED_EFFECTS_H

#include "vehicle_can_unified.h"

#include <stdbool.h>
#include <stdint.h>

#define TAG_LED "LED"

// Stable alphanumeric IDs for effects (never change)
#define EFFECT_ID_OFF "OFF"
#define EFFECT_ID_SOLID "SOLID"
#define EFFECT_ID_BREATHING "BREATHING"
#define EFFECT_ID_RAINBOW "RAINBOW"
#define EFFECT_ID_RAINBOW_CYCLE "RAINBOW_CYCLE"
#define EFFECT_ID_THEATER_CHASE "THEATER_CHASE"
#define EFFECT_ID_RUNNING_LIGHTS "RUNNING_LIGHTS"
#define EFFECT_ID_TWINKLE "TWINKLE"
#define EFFECT_ID_FIRE "FIRE"
#define EFFECT_ID_SCAN "SCAN"
#define EFFECT_ID_KNIGHT_RIDER "KNIGHT_RIDER"
#define EFFECT_ID_FADE "FADE"
#define EFFECT_ID_STROBE "STROBE"
#define EFFECT_ID_VEHICLE_SYNC "VEHICLE_SYNC"
#define EFFECT_ID_TURN_SIGNAL "TURN_SIGNAL"
#define EFFECT_ID_BRAKE_LIGHT "BRAKE_LIGHT"
#define EFFECT_ID_CHARGE_STATUS "CHARGE_STATUS"
#define EFFECT_ID_HAZARD "HAZARD"
#define EFFECT_ID_BLINDSPOT_FLASH "BLINDSPOT_FLASH"
#define EFFECT_ID_AUDIO_REACTIVE "AUDIO_REACTIVE"
#define EFFECT_ID_AUDIO_BPM "AUDIO_BPM"
#define EFFECT_ID_FFT_SPECTRUM "FFT_SPECTRUM"
#define EFFECT_ID_FFT_BASS_PULSE "FFT_BASS_PULSE"
#define EFFECT_ID_FFT_VOCAL_WAVE "FFT_VOCAL_WAVE"
#define EFFECT_ID_FFT_ENERGY_BAR "FFT_ENERGY_BAR"
#define EFFECT_ID_COMET "COMET"
#define EFFECT_ID_METEOR_SHOWER "METEOR_SHOWER"
#define EFFECT_ID_RIPPLE_WAVE "RIPPLE_WAVE"
#define EFFECT_ID_DUAL_GRADIENT "DUAL_GRADIENT"
#define EFFECT_ID_SPARKLE_OVERLAY "SPARKLE_OVERLAY"
#define EFFECT_ID_CENTER_OUT_SCAN "CENTER_OUT_SCAN"
#define EFFECT_ID_POWER_METER "POWER_METER"
#define EFFECT_ID_POWER_METER_CENTER "POWER_METER_CENTER"

#define EFFECT_ID_MAX_LEN 32 // Max length of an effect ID

// Effect types (internal enum, may change)
typedef enum {
  EFFECT_OFF = 0,
  EFFECT_SOLID,
  EFFECT_BREATHING,
  EFFECT_RAINBOW,
  EFFECT_RAINBOW_CYCLE,
  EFFECT_THEATER_CHASE,
  EFFECT_RUNNING_LIGHTS,
  EFFECT_TWINKLE,
  EFFECT_FIRE,
  EFFECT_SCAN,
  EFFECT_KNIGHT_RIDER, // K2000 - clean trail without fade
  EFFECT_FADE,
  EFFECT_STROBE,
  EFFECT_VEHICLE_SYNC,    // Synchronized with vehicle state
  EFFECT_TURN_SIGNAL,     // Turn signals
  EFFECT_BRAKE_LIGHT,     // Brake lights
  EFFECT_CHARGE_STATUS,   // Charge indicator
  EFFECT_HAZARD,          // Hazards (both sides)
  EFFECT_BLINDSPOT_FLASH, // Directional flash for blind spot
  EFFECT_AUDIO_REACTIVE,  // Sound reactive effect
  EFFECT_AUDIO_BPM,       // BPM synchronized effect
  EFFECT_FFT_SPECTRUM,    // Real-time FFT spectrum (equalizer)
  EFFECT_FFT_BASS_PULSE,  // Pulse on bass (kick)
  EFFECT_FFT_VOCAL_WAVE,  // Wave reactive to vocals
  EFFECT_FFT_ENERGY_BAR,  // Spectral energy bar
  EFFECT_COMET,           // Comet with trail
  EFFECT_METEOR_SHOWER,   // Meteor shower
  EFFECT_RIPPLE_WAVE,     // Concentric wave from center
  EFFECT_DUAL_GRADIENT,   // Double gradient that breathes
  EFFECT_SPARKLE_OVERLAY, // Soft background + rare sparkles
  EFFECT_CENTER_OUT_SCAN, // Double scan center -> edges
  EFFECT_POWER_METER,     // Combined power bar (front + rear)
  EFFECT_POWER_METER_CENTER, // Center power bar (zero in middle)
  EFFECT_MAX
} led_effect_t;

// Vehicle synchronization modes
typedef enum {
  SYNC_OFF = 0,
  SYNC_DOORS,        // Reacts to door opening
  SYNC_SPEED,        // Changes with speed
  SYNC_TURN_SIGNALS, // Follows turn signals
  SYNC_BRAKE,        // Brake lights
  SYNC_CHARGE,       // Charge state
  SYNC_LOCKED,       // Lock state
  SYNC_ALL           // All events
} sync_mode_t;

/**
 * @brief Configuration of an LED effect
 *
 * Usage example:
 * @code
 * // Rainbow effect on entire strip, animated left to right
 * effect_config_t cfg = {
 *   .effect = EFFECT_RAINBOW,
 *   .brightness = 200,
 *   .speed = 50,
 *   .color1 = 0xFF0000,
 *   .reverse = false,
 *   .audio_reactive = false,
 *   .segment_start = 0,
 *   .segment_length = 0  // 0 = toute la bande
 * };
 * led_effects_set_config(&cfg);
 *
 * // Left turn signal (first half, animation to the left)
 * effect_config_t turn_left = {
 *   .effect = EFFECT_TURN_SIGNAL,
 *   .brightness = 255,
 *   .speed = 80,
 *   .color1 = 0xFF8000,  // Orange
 *   .reverse = true,     // Animation vers la gauche
 *   .segment_start = 0,
 *   .segment_length = 61  // Première moitié (0-60)
 * };
 * @endcode
 */
typedef struct {
  led_effect_t effect;
  uint8_t brightness; // 0-255
  uint8_t speed;      // 0-100 (vitesse d'animation)
  uint32_t color1;    // RGB au format 0xRRGGBB
  uint32_t color2;
  uint32_t color3;
  sync_mode_t sync_mode;
  bool reverse;                 // Animation direction: false = left->right, true = right->left
  bool audio_reactive;          // Effect reacts to microphone if enabled
  uint16_t segment_start;       // Starting index (always from left, 0-based)
  uint16_t segment_length;      // Segment length (0 = auto/full strip)
  bool accel_pedal_pos_enabled; // Enable segment_length modulation by accel_pedal_pos
  uint8_t accel_pedal_offset;   // Minimum offset for segment_length (0-100%)
} effect_config_t;

typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} led_rgb_t;

/**
 * @brief Initializes the LED system
 * @return true if successful
 */
bool led_effects_init(void);

/**
 * @brief Deinitializes the LED system and frees resources
 */
void led_effects_deinit(void);

/**
 * @brief Configure un effet
 * @param config Configuration de l'effet
 */
void led_effects_set_config(const effect_config_t *config);

/**
 * @brief Gets the current configuration
 * @param config Pointer to configuration structure
 */
void led_effects_get_config(effect_config_t *config);

/**
 * @brief Updates the LEDs (call regularly)
 */
void led_effects_update(void);

/**
 * @brief Updates with vehicle state
 * @param state Vehicle state
 */
void led_effects_update_vehicle_state(const vehicle_state_t *state);

/**
 * @brief Sets the active event context for rendering (0 = none)
 * @param event_id Numeric ID of CAN event
 */
void led_effects_set_event_context(uint16_t event_id);

/**
 * @brief Gets the name of an effect
 * @param effect Effect type
 * @return Effect name
 */
const char *led_effects_get_name(led_effect_t effect);

/**
 * @brief Resets to default configuration
 */
void led_effects_reset_config(void);

/**
 * @brief Gets the night mode state
 * @return true if night mode is active
 */
bool led_effects_get_night_mode(void);

/**
 * @brief Gets the night mode brightness
 * @return Brightness (0-255)
 */
uint8_t led_effects_get_night_brightness(void);

/**
 * @brief Converts an effect enum to alphanumeric ID
 * @param effect Effect type
 * @return Alphanumeric ID (static constant)
 */
const char *led_effects_enum_to_id(led_effect_t effect);

/**
 * @brief Converts an alphanumeric ID to effect enum
 * @param id Alphanumeric ID
 * @return Effect type (EFFECT_OFF if ID unknown)
 */
led_effect_t led_effects_id_to_enum(const char *id);

/**
 * @brief Checks if an effect requires CAN data to function
 * @param effect Effect type
 * @return true if effect requires CAN bus, false otherwise
 */
bool led_effects_requires_can(led_effect_t effect);

/**
 * @brief Checks if an effect requires audio FFT
 * @param effect Effect type
 * @return true if effect requires FFT, false otherwise
 */
bool led_effects_requires_fft(led_effect_t effect);

/**
 * @brief Checks if an effect is audio-reactive (thus not selectable in
 * CAN events)
 * @param effect Effect type
 * @return true if effect is audio-reactive, false otherwise
 */
bool led_effects_is_audio_effect(led_effect_t effect);

/**
 * @brief Enables OTA progress display on the strip
 */
void led_effects_start_progress_display(void);

/**
 * @brief Updates the displayed OTA progress percentage
 * @param percent Percentage 0-100
 */
void led_effects_update_progress(uint8_t percent);

/**
 * @brief Disables OTA progress display
 */
void led_effects_stop_progress_display(void);

bool led_effects_is_ota_display_active(void);

/**
 * @brief Shows an effect indicating the device is ready to restart
 *        after a successful OTA update.
 */
void led_effects_show_upgrade_ready(void);

/**
 * @brief Shows an effect indicating an OTA update failed
 *        but the device will restart automatically.
 */
void led_effects_show_upgrade_error(void);

/**
 * @brief Changes the number of LEDs
 * @param led_count Number of LEDs
 * @return true if change was successful
 */
bool led_effects_set_led_count(uint16_t led_count);

/**
 * @brief Renders an effect to a buffer without sending to LEDs
 */
void led_effects_render_to_buffer(const effect_config_t *config, uint16_t segment_start, uint16_t segment_length, uint32_t frame_counter, led_rgb_t *out_buffer);

/**
 * @brief Displays a pre-calculated buffer
 */
void led_effects_show_buffer(const led_rgb_t *buffer);

uint32_t led_effects_get_frame_counter(void);
void led_effects_advance_frame_counter(void);
uint16_t led_effects_get_led_count(void);
uint8_t led_effects_get_accel_pedal_pos(void);

/**
 * @brief Applies accel_pedal_pos modulation to a segment length
 * @param original_length Original segment length
 * @param accel_pedal_pos Pedal position (0-100)
 * @param offset_percent Minimum offset (0-100%)
 * @return Modulated length (minimum 1)
 */
uint16_t led_effects_apply_accel_modulation(uint16_t original_length, uint8_t accel_pedal_pos, uint8_t offset_percent);

/**
 * @brief Normalizes a segment (start, length) to be within strip limits
 * @param segment_start Pointer to start (will be modified)
 * @param segment_length Pointer to length (will be modified, 0 = full strip)
 * @param total_leds Total number of LEDs
 */
void led_effects_normalize_segment(uint16_t *segment_start, uint16_t *segment_length, uint16_t total_leds);

#endif // LED_EFFECTS_H
