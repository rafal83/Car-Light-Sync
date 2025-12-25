#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "led_effects.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TAG_CONFIG "ConfigMgr"
#define MAX_PROFILE_SCAN_LIMIT 100 // Scan limit to avoid infinite loop
#define PROFILE_NAME_MAX_LEN 32

// Stable alphanumeric IDs for CAN events (never change)
#define EVENT_ID_NONE "NONE"
#define EVENT_ID_TURN_LEFT "TURN_LEFT"
#define EVENT_ID_TURN_RIGHT "TURN_RIGHT"
#define EVENT_ID_TURN_HAZARD "TURN_HAZARD"
#define EVENT_ID_CHARGING "CHARGING"
#define EVENT_ID_CHARGE_COMPLETE "CHARGE_COMPLETE"
#define EVENT_ID_CHARGING_STARTED "CHARGING_STARTED"
#define EVENT_ID_CHARGING_STOPPED "CHARGING_STOPPED"
#define EVENT_ID_CHARGING_CABLE_CONNECTED "CHARGING_CABLE_CONNECTED"
#define EVENT_ID_CHARGING_CABLE_DISCONNECTED "CHARGING_CABLE_DISCONNECTED"
#define EVENT_ID_CHARGING_PORT_OPENED "CHARGING_PORT_OPENED"
#define EVENT_ID_DOOR_OPEN_LEFT "DOOR_OPEN_LEFT"
#define EVENT_ID_DOOR_OPEN_RIGHT "DOOR_OPEN_RIGHT"
#define EVENT_ID_DOOR_CLOSE_LEFT "DOOR_CLOSE_LEFT"
#define EVENT_ID_DOOR_CLOSE_RIGHT "DOOR_CLOSE_RIGHT"
#define EVENT_ID_LOCKED "LOCKED"
#define EVENT_ID_UNLOCKED "UNLOCKED"
#define EVENT_ID_BRAKE_ON "BRAKE_ON"
#define EVENT_ID_BLINDSPOT_LEFT "BLINDSPOT_LEFT"
#define EVENT_ID_BLINDSPOT_RIGHT "BLINDSPOT_RIGHT"
#define EVENT_ID_BLINDSPOT_LEFT_ALERT "BLINDSPOT_LEFT_ALERT"
#define EVENT_ID_BLINDSPOT_RIGHT_ALERT "BLINDSPOT_RIGHT_ALERT"
#define EVENT_ID_SIDE_COLLISION_LEFT "SIDE_COLLISION_LEFT"
#define EVENT_ID_SIDE_COLLISION_RIGHT "SIDE_COLLISION_RIGHT"
#define EVENT_ID_FORWARD_COLLISION "FORWARD_COLLISION"
#define EVENT_ID_LANE_DEPARTURE_LEFT_LV1 "LANE_DEPARTURE_LEFT_LV1"
#define EVENT_ID_LANE_DEPARTURE_LEFT_LV2 "LANE_DEPARTURE_LEFT_LV2"
#define EVENT_ID_LANE_DEPARTURE_RIGHT_LV1 "LANE_DEPARTURE_RIGHT_LV1"
#define EVENT_ID_LANE_DEPARTURE_RIGHT_LV2 "LANE_DEPARTURE_RIGHT_LV2"
#define EVENT_ID_SPEED_THRESHOLD "SPEED_THRESHOLD"
#define EVENT_ID_AUTOPILOT_ENGAGED "AUTOPILOT_ENGAGED"
#define EVENT_ID_AUTOPILOT_DISENGAGED "AUTOPILOT_DISENGAGED"
#define EVENT_ID_AUTOPILOT_ALERT_LV1 "AUTOPILOT_ALERT_LV1"
#define EVENT_ID_AUTOPILOT_ALERT_LV2 "AUTOPILOT_ALERT_LV2"
#define EVENT_ID_GEAR_DRIVE "GEAR_DRIVE"
#define EVENT_ID_GEAR_REVERSE "GEAR_REVERSE"
#define EVENT_ID_GEAR_PARK "GEAR_PARK"
#define EVENT_ID_SENTRY_MODE_ON "SENTRY_MODE_ON"
#define EVENT_ID_SENTRY_MODE_OFF "SENTRY_MODE_OFF"
#define EVENT_ID_SENTRY_ALERT "SENTRY_ALERT"

// CAN event types that can trigger effects (internal enum, may
// change)
typedef enum {
  CAN_EVENT_NONE = 0,
  CAN_EVENT_TURN_LEFT,
  CAN_EVENT_TURN_RIGHT,
  CAN_EVENT_TURN_HAZARD,
  CAN_EVENT_CHARGING,
  CAN_EVENT_CHARGE_COMPLETE,
  CAN_EVENT_CHARGING_STARTED,
  CAN_EVENT_CHARGING_STOPPED,
  CAN_EVENT_CHARGING_CABLE_CONNECTED,
  CAN_EVENT_CHARGING_CABLE_DISCONNECTED,
  CAN_EVENT_CHARGING_PORT_OPENED,
  CAN_EVENT_DOOR_OPEN_LEFT,
  CAN_EVENT_DOOR_OPEN_RIGHT,
  CAN_EVENT_DOOR_CLOSE_LEFT,
  CAN_EVENT_DOOR_CLOSE_RIGHT,
  CAN_EVENT_LOCKED,
  CAN_EVENT_UNLOCKED,
  CAN_EVENT_BRAKE_ON,
  CAN_EVENT_BLINDSPOT_LEFT,
  CAN_EVENT_BLINDSPOT_RIGHT,
  CAN_EVENT_BLINDSPOT_LEFT_ALERT,
  CAN_EVENT_BLINDSPOT_RIGHT_ALERT,
  CAN_EVENT_SIDE_COLLISION_LEFT,
  CAN_EVENT_SIDE_COLLISION_RIGHT,
  CAN_EVENT_FORWARD_COLLISION,
  CAN_EVENT_LANE_DEPARTURE_LEFT_LV1,
  CAN_EVENT_LANE_DEPARTURE_LEFT_LV2,
  CAN_EVENT_LANE_DEPARTURE_RIGHT_LV1,
  CAN_EVENT_LANE_DEPARTURE_RIGHT_LV2,
  CAN_EVENT_SPEED_THRESHOLD, // Triggered when speed > threshold
  CAN_EVENT_AUTOPILOT_ENGAGED,
  CAN_EVENT_AUTOPILOT_DISENGAGED,
  CAN_EVENT_AUTOPILOT_ALERT_LV1,
  CAN_EVENT_AUTOPILOT_ALERT_LV2,
  CAN_EVENT_GEAR_DRIVE,      // Shift to Drive mode
  CAN_EVENT_GEAR_REVERSE,    // Shift to reverse
  CAN_EVENT_GEAR_PARK,       // Shift to Park mode
  CAN_EVENT_SENTRY_MODE_ON,  // Sentry mode armed
  CAN_EVENT_SENTRY_MODE_OFF, // Sentry mode disarmed
  CAN_EVENT_SENTRY_ALERT,    // Sentry detection/alarm
  CAN_EVENT_MAX
} can_event_type_t;

// Action type for a CAN event
typedef enum {
  EVENT_ACTION_APPLY_EFFECT = 0, // Apply an LED effect
  EVENT_ACTION_SWITCH_PROFILE    // Switch profile
} event_action_type_t;

/**
 * @brief Configuration of an effect for a specific CAN event
 *
 * Usage example:
 * @code
 * // Configure left turn signal on CAN event
 * effect_config_t turn_config = {
 *   .effect = EFFECT_TURN_SIGNAL,
 *   .brightness = 255,
 *   .speed = 80,
 *   .color1 = 0xFF8000,  // Orange
 *   .reverse = true,
 *   .segment_start = 0,
 *   .segment_length = 61
 * };
 *
 * config_manager_set_event_effect(
 *   0,                         // profile_id
 *   CAN_EVENT_TURN_LEFT,      // event
 *   &turn_config,              // effect_config
 *   500,                       // duration_ms (500ms)
 *   200                        // priority (haute priorité)
 * );
 * config_manager_set_event_enabled(0, CAN_EVENT_TURN_LEFT, true);
 * @endcode
 */
typedef struct {
  can_event_type_t event;
  event_action_type_t action_type; // Type of action to perform
  effect_config_t effect_config;
  uint16_t duration_ms; // Effect duration (0 = permanent until new event)
  uint8_t priority;     // Priority (0-255, higher = higher priority)
  int8_t profile_id;    // Profile ID to activate (-1 = none)
  bool enabled;         // Active or not
} can_event_effect_t;

// Complete configuration profile
// NOTE: Profiles stored in SPIFFS (176KB) instead of NVS (1984 bytes limit)
typedef struct {
  char name[PROFILE_NAME_MAX_LEN];
  // Metadata
  bool active; // Active profile
  effect_config_t default_effect; // Default effect

  can_event_effect_t event_effects[CAN_EVENT_MAX]; // Effects by event

  // General parameters - Dynamic brightness
  bool dynamic_brightness_enabled; // Enable dynamic brightness linked to vehicle brightness
  uint8_t dynamic_brightness_rate; // Vehicle brightness application rate (0-100%)
  uint64_t dynamic_brightness_exclude_mask; // Mask of events excluded from dynamic brightness

} config_profile_t;

// Binary file format for SPIFFS storage (with versioning)
#define PROFILE_FILE_MAGIC 0x50524F46  // "PROF" en ASCII
#define PROFILE_FILE_VERSION 1
#define PROFILE_FILE_MIN_VERSION 1

typedef struct __attribute__((packed)) {
  uint32_t magic;           // 0x50524F46 ("PROF") for validation
  uint16_t version;         // Format version (1 for v1)
  uint16_t data_size;       // Size of config_profile_t (for verification)
  config_profile_t data;    // Profile data
  uint32_t checksum;        // Simple checksum of data for integrity
} profile_file_t;

/**
 * @brief Initializes the configuration manager
 * @return true if successful
 */
bool config_manager_init(void);

/**
 * @brief Saves a profile to NVS
 * @param profile_id Profile ID (0-999)
 * @param profile Profile to save
 * @return true if successful
 */
bool config_manager_save_profile(uint16_t profile_id, const config_profile_t *profile);

/**
 * @brief Loads a profile from NVS
 * @param profile_id Profile ID
 * @param profile Pointer to profile
 * @return true if successful
 */
bool config_manager_load_profile(uint16_t profile_id, config_profile_t *profile);

/**
 * @brief Deletes a profile
 * @param profile_id Profile ID
 * @return true if successful
 */
bool config_manager_delete_profile(uint16_t profile_id);

/**
 * @brief Activates a profile
 * @param profile_id Profile ID
 * @return true if successful
 */
bool config_manager_activate_profile(uint16_t profile_id);

/**
 * @brief Renames a profile
 * @param profile_id Profile ID
 * @param new_name New profile name
 * @return true if successful
 */
bool config_manager_rename_profile(uint16_t profile_id, const char *new_name);

/**
 * @brief Gets the active profile
 * @param profile Pointer to profile
 * @return true if a profile is active
 */
bool config_manager_get_active_profile(config_profile_t *profile);

/**
 * @brief Gets the event cache of the active profile
 * @return Pointer to event array (or NULL if no active profile)
 */
can_event_effect_t *config_manager_get_active_events(void);

/**
 * @brief Gets the ID of the active profile
 * @return Active profile ID (-1 if none)
 */
int config_manager_get_active_profile_id(void);

/**
 * @brief Cycles the active profile to the previous/next available
 * @param direction +1 = next, -1 = previous
 * @return true if a profile was activated
 */
bool config_manager_cycle_active_profile(int direction);

/**
 * @brief Gets dynamic brightness parameters of the active profile
 * @param enabled Pointer to store if dynamic brightness is enabled
 * @param rate Pointer to store the application rate (0-100%)
 * @return true if active profile exists
 */
bool config_manager_get_dynamic_brightness(bool *enabled, uint8_t *rate);
bool config_manager_is_dynamic_brightness_excluded(can_event_type_t event);

/**
 * @brief Lists all available profiles
 * @param profiles Array to store profiles
 * @param max_profiles Max size of array
 * @return Number of profiles found
 */
int config_manager_list_profiles(config_profile_t *profiles, int max_profiles);

/**
 * @brief Creates a default profile
 * @param profile Pointer to profile
 * @param name Profile name
 */
void config_manager_create_default_profile(config_profile_t *profile, const char *name);
void config_manager_create_off_profile(config_profile_t *profile, const char *name);

// Steering wheel profile control (opt-in)
bool config_manager_get_wheel_control_enabled(void);
bool config_manager_set_wheel_control_enabled(bool enabled);
int config_manager_get_wheel_control_speed_limit(void);
bool config_manager_set_wheel_control_speed_limit(int speed_kph);

/**
 * @brief Loads an event from SPIFFS
 * @param profile_id Profile ID
 * @param event Event type
 * @param event_effect Pointer to structure to fill
 * @return true if successful
 */
bool config_manager_load_event(uint16_t profile_id, can_event_type_t event, can_event_effect_t *event_effect);

/**
 * @brief Saves an event to SPIFFS
 * @param profile_id Profile ID
 * @param event Event type
 * @param event_effect Pointer to event structure
 * @return true if successful
 */
bool config_manager_save_event(uint16_t profile_id, can_event_type_t event, const can_event_effect_t *event_effect);

/**
 * @brief Associates an effect with a CAN event
 * @param profile_id Profile ID
 * @param event Event type
 * @param effect_config Effect configuration
 * @param duration_ms Effect duration
 * @param priority Priority
 * @return true if successful
 */
bool config_manager_set_event_effect(uint16_t profile_id, can_event_type_t event, const effect_config_t *effect_config, uint16_t duration_ms, uint8_t priority);

/**
 * @brief Enables or disables an event
 * @param profile_id Profile ID
 * @param event Event type
 * @param enabled true to enable, false to disable
 * @return true if successful
 */
bool config_manager_set_event_enabled(uint16_t profile_id, can_event_type_t event, bool enabled);

/**
 * @brief Processes a CAN event and applies the corresponding effect
 * @param event Event type
 * @return true if an effect was applied
 */
bool config_manager_process_can_event(can_event_type_t event);

/**
 * @brief Manually stops an active event
 * @param event Event type to stop
 */
void config_manager_stop_event(can_event_type_t event);
void config_manager_stop_all_events(void);

/**
 * @brief Updates effects based on time
 * Manages temporary effects and returns to default effect
 */
void config_manager_update(void);

/**
 * @brief Indicates if active events override the default effect
 * @return true if events are active
 */
bool config_manager_has_active_events(void);

/**
 * @brief Exports a profile to JSON
 * @param profile_id Profile ID
 * @param json_buffer Buffer for JSON
 * @param buffer_size Buffer size
 * @return true if successful
 */
bool config_manager_export_profile(uint16_t profile_id, char *json_buffer, size_t buffer_size);

/**
 * @brief Imports a profile from JSON and saves in binary
 * @param profile_id Profile ID (0-99)
 * @param json_string Profile JSON
 * @return true if successful
 */
bool config_manager_import_profile_direct(uint16_t profile_id, const char *json_string);

/**
 * @brief Gets the effect configuration for a specific event
 * @param event Event type
 * @param event_effect Pointer to effect structure
 * @return true if successful (active profile exists)
 */
bool config_manager_get_effect_for_event(can_event_type_t event, can_event_effect_t *event_effect);

/**
 * @brief Gets the configured number of LEDs
 * @return Number of LEDs
 */
uint16_t config_manager_get_led_count(void);

/**
 * @brief Sets the number of LEDs
 * @param led_count Number of LEDs (1-200)
 * @return true if successful and saved to NVS
 */
bool config_manager_set_led_count(uint16_t led_count);

/**
 * @brief Converts an event enum to alphanumeric ID
 * @param event Event type
 * @return Alphanumeric ID (static constant)
 */
const char *config_manager_enum_to_id(can_event_type_t event);

/**
 * @brief Converts an alphanumeric ID to event enum
 * @param id Alphanumeric ID
 * @return Event type (CAN_EVENT_NONE if ID unknown)
 */
can_event_type_t config_manager_id_to_enum(const char *id);

/**
 * @brief Checks if an event can trigger a profile change
 * @param event Event type
 * @return true if the event can change profile
 */
bool config_manager_event_can_switch_profile(can_event_type_t event);

/**
 * @brief Resets all settings to factory defaults
 * Deletes all profiles and creates a default profile
 * @return true if successful
 */
bool config_manager_factory_reset(void);

/**
 * @brief Reapplies the default effect of the active profile
 * Useful after audio module initialization to enable audio effects
 */
void config_manager_reapply_default_effect(void);

/**
 * @brief Checks if NVS space allows creating a new profile
 * @return true if sufficient space available
 */
bool config_manager_can_create_profile(void);

/**
 * @brief Imports a profile from a JSON string into a config_profile_t structure
 * @param json_string JSON string of preset
 * @param profile Pointer to structure to fill
 * @return true if successful, false otherwise
 */
bool config_manager_import_profile_from_json(const char *json_string, config_profile_t *profile);

#endif // CONFIG_MANAGER_H
