/**
 * @file config_manager.c
 * @brief Profile and CAN event manager
 *
 * Handles:
 * - LED effect profiles with SPIFFS storage (JSON)
 * - CAN events with priorities and segments
 * - Multi-pass rendering pipeline: reservation -> default effect -> events
 * - Profile JSON import/export
 * - Enable/disable events
 */

#include "config_manager.h"

#include "audio_input.h"
#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_effects.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "settings_manager.h"
#include "spiffs_storage.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

// Buffer for JSON export/import (full profile with all events)
#define JSON_EXPORT_BUFFER_SIZE 16384 // 16KB buffer for JSON

// Forward declaration
static bool export_profile_to_json(const config_profile_t *profile, uint16_t profile_id, char *json_buffer, size_t buffer_size);

// ============================================================================
// Simple checksum to validate binary profile integrity
// ============================================================================

static uint32_t calculate_checksum(const void *data, size_t length) {
  uint32_t sum       = 0;
  const uint8_t *buf = (const uint8_t *)data;

  for (size_t i = 0; i < length; i++) {
    sum += buf[i];
  }

  return sum;
}

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t version;
  uint16_t data_size;
} profile_header_t;

static void migrate_profile_if_needed(config_profile_t *profile, uint16_t version) {
  if (profile == NULL) {
    return;
  }
}

// RAM cache: active profile (stored in SPIFFS, ~2KB in RAM)
static config_profile_t active_profile;
static int active_profile_id      = -1;
static bool active_profile_loaded = false;

// Profile ID registry for O(1) profile existence checks (128 bytes for 1000 profiles)
#define PROFILE_REGISTRY_SIZE ((MAX_PROFILE_SCAN_LIMIT + 7) / 8) // Bitmap: 100 profiles = 13 bytes
static uint8_t profile_registry[PROFILE_REGISTRY_SIZE] = {0};
static bool profile_registry_initialized               = false;

// Steering wheel control (opt-in)
static bool wheel_control_enabled                      = false;
static uint8_t wheel_control_speed_limit               = 5; // km/h

// Multiple event system
#define MAX_ACTIVE_EVENTS 10
typedef struct {
  can_event_type_t event;
  effect_config_t effect_config;
  uint32_t start_time;
  uint16_t duration_ms;
  uint8_t priority;
  bool active;
} active_event_t;

static active_event_t active_events[MAX_ACTIVE_EVENTS];
static bool effect_override_active = false;

// Buffers for composed LED rendering
static led_rgb_t composed_buffer[MAX_LED_COUNT];
static led_rgb_t temp_buffer[MAX_LED_COUNT];
static uint8_t priority_buffer[MAX_LED_COUNT];

// Profile registry helper functions for O(1) lookup
static inline void profile_registry_set(uint16_t profile_id) {
  if (profile_id >= MAX_PROFILE_SCAN_LIMIT)
    return;
  profile_registry[profile_id / 8] |= (1 << (profile_id % 8));
}

static inline void profile_registry_clear(uint16_t profile_id) {
  if (profile_id >= MAX_PROFILE_SCAN_LIMIT)
    return;
  profile_registry[profile_id / 8] &= ~(1 << (profile_id % 8));
}

static inline bool profile_registry_exists(uint16_t profile_id) {
  if (profile_id >= MAX_PROFILE_SCAN_LIMIT)
    return false;
  return (profile_registry[profile_id / 8] & (1 << (profile_id % 8))) != 0;
}

static void profile_registry_rebuild(void) {
  memset(profile_registry, 0, PROFILE_REGISTRY_SIZE);

  // OPTIMIZATION: Use SPIFFS directory listing instead of 100x fopen()
  DIR *dir = opendir("/spiffs/profiles");
  if (dir == NULL) {
    ESP_LOGW(TAG_CONFIG, "Profile directory not found, creating...");
    mkdir("/spiffs/profiles", 0755);
    profile_registry_initialized = true;
    return;
  }

  struct dirent *entry;
  int count = 0;
  while ((entry = readdir(dir)) != NULL) {
    // Parse profile ID from filename (e.g., "42.bin" -> 42)
    if (entry->d_type == DT_REG) { // Regular file only
      int profile_id = -1;
      if (sscanf(entry->d_name, "%d.bin", &profile_id) == 1) {
        if (profile_id >= 0 && profile_id < MAX_PROFILE_SCAN_LIMIT) {
          profile_registry_set(profile_id);
          count++;
        }
      }
    }
  }
  closedir(dir);

  profile_registry_initialized = true;
  ESP_LOGI(TAG_CONFIG, "Profile registry built (%d profiles found)", count);
}

static void load_wheel_control_settings(void) {
  uint8_t enabled_u8        = settings_get_bool("wheel_control_enabled", 0);
  uint8_t speed_kph         = settings_get_u8("wheel_control_speed_limit", wheel_control_speed_limit);

  wheel_control_enabled     = enabled_u8 != 0;
  wheel_control_speed_limit = speed_kph;
  // Clamp
  if (wheel_control_speed_limit > 100) {
    wheel_control_speed_limit = 100;
  }
}

bool config_manager_init(void) {
  // Initialize the active profile
  memset(&active_profile, 0, sizeof(active_profile));
  active_profile_loaded          = false;

  // Reduce stack usage: use a dynamic buffer for temporary profiles
  config_profile_t *temp_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (temp_profile == NULL) {
    ESP_LOGE(TAG_CONFIG, "Temporary profile allocation error");
    return false;
  }

  // Build profile registry for O(1) existence checks
  profile_registry_rebuild();

  // Load steering wheel control settings (opt-in)
  load_wheel_control_settings();

  // Load the active profile ID from SPIFFS
  int32_t saved_active_id = settings_get_i32("active_profile_id", -1);
  if (saved_active_id >= 0) {
    active_profile_id = saved_active_id;
  }

  // Try to load the active profile
  bool profile_exists = false;
  if (active_profile_id >= 0) {
    profile_exists = config_manager_load_profile(active_profile_id, &active_profile);
    if (profile_exists) {
      active_profile_loaded = true;
      ESP_LOGI(TAG_CONFIG, "Active profile %d loaded: %s", active_profile_id, active_profile.name);
      led_effects_set_config(&active_profile.default_effect);
      effect_override_active = false;
    }
  }

  // If no profile exists, create a default profile
  if (!profile_exists) {
    ESP_LOGI(TAG_CONFIG, "No profile found, creating default profile + Off profile");
    config_manager_create_default_profile(&active_profile, "Default");
    config_manager_save_profile(1, &active_profile);
    active_profile_id     = 1;
    active_profile_loaded = true;

    config_manager_create_off_profile(temp_profile, "Eteint");
    config_manager_save_profile(0, temp_profile);

    // Save the active ID
    settings_set_i32("active_profile_id", active_profile_id);

    led_effects_set_config(&active_profile.default_effect);
    effect_override_active = false;
  }

  // Ensure an "Eteint" profile exists (ID 0 reserved)
  if (!config_manager_load_profile(0, temp_profile)) {
    config_manager_create_off_profile(temp_profile, "Eteint");
    config_manager_save_profile(0, temp_profile);
  }

  free(temp_profile);

  return true;
}

// This function is no longer needed - load only the active profile on demand

// ============================================================================
// Binary save/load of profiles (binary format with CRC32)
// ============================================================================

bool config_manager_save_profile(uint16_t profile_id, const config_profile_t *profile) {
  if (profile == NULL) {
    return false;
  }

  // Allocate on the heap to avoid stack overflow (~2.5KB)
  profile_file_t *file_data = (profile_file_t *)malloc(sizeof(profile_file_t));
  if (file_data == NULL) {
    ESP_LOGE(TAG_CONFIG, "Memory allocation error while saving profile %d", profile_id);
    return false;
  }

  // Copy data
  memcpy(&file_data->data, profile, sizeof(config_profile_t));

  // Fill the header
  file_data->magic     = PROFILE_FILE_MAGIC;
  file_data->version   = PROFILE_FILE_VERSION;
  file_data->data_size = sizeof(config_profile_t);

  // Compute checksum over profile data
  file_data->checksum  = calculate_checksum(&file_data->data, sizeof(config_profile_t));

  // Save to SPIFFS (binary)
  char filepath[64];
  snprintf(filepath, sizeof(filepath), "/spiffs/profiles/profile_%d.bin", profile_id);

  esp_err_t err = spiffs_save_blob(filepath, file_data, sizeof(profile_file_t));

  if (err != ESP_OK) {
    ESP_LOGE(TAG_CONFIG, "SPIFFS save error for profile %d: %s", profile_id, esp_err_to_name(err));
    free(file_data);
    return false;
  }

  // Update the active profile in RAM if it is the one we just saved
  if (profile_id == active_profile_id && active_profile_loaded) {
    memcpy(&active_profile, &file_data->data, sizeof(config_profile_t));
  }

  ESP_LOGI(TAG_CONFIG, "Profile %d saved (binary, %d bytes): %s", profile_id, sizeof(profile_file_t), profile->name);
  free(file_data);

  // Update registry
  profile_registry_set(profile_id);

  return true;
}

bool config_manager_load_profile(uint16_t profile_id, config_profile_t *profile) {
  if (profile == NULL) {
    return false;
  }

  // Load from SPIFFS (binary)
  char filepath[64];
  snprintf(filepath, sizeof(filepath), "/spiffs/profiles/profile_%d.bin", profile_id);

  // Check if the file exists
  if (!spiffs_file_exists(filepath)) {
    return false;
  }

  // Allocate on the heap to avoid stack overflow (~2.5KB)
  profile_file_t *file_data = (profile_file_t *)malloc(sizeof(profile_file_t));
  if (file_data == NULL) {
    ESP_LOGE(TAG_CONFIG, "Memory allocation error while loading profile %d", profile_id);
    return false;
  }

  size_t buffer_size = sizeof(profile_file_t);
  esp_err_t err      = spiffs_load_blob(filepath, file_data, &buffer_size);

  if (err != ESP_OK) {
    ESP_LOGE(TAG_CONFIG, "SPIFFS load error for profile %d: %s", profile_id, esp_err_to_name(err));
    free(file_data);
    return false;
  }

  if (buffer_size < (sizeof(profile_header_t) + sizeof(uint32_t))) {
    ESP_LOGE(TAG_CONFIG, "Invalid file size for profile %d: %d bytes (too small)", profile_id, buffer_size);
    free(file_data);
    return false;
  }

  profile_header_t header;
  memcpy(&header, file_data, sizeof(profile_header_t));
  const size_t expected_size = sizeof(profile_header_t) + header.data_size + sizeof(uint32_t);

  if (buffer_size < expected_size) {
    ESP_LOGE(TAG_CONFIG, "Invalid file size for profile %d: %d bytes (expected >= %d)", profile_id, buffer_size, expected_size);
    free(file_data);
    return false;
  }

  // Check the magic number
  if (header.magic != PROFILE_FILE_MAGIC) {
    ESP_LOGE(TAG_CONFIG, "Invalid magic number for profile %d: 0x%08X", profile_id, header.magic);
    free(file_data);
    return false;
  }

  if (header.version > PROFILE_FILE_VERSION) {
    ESP_LOGE(TAG_CONFIG, "Unsupported profile version %d (current: v%d)", header.version, PROFILE_FILE_VERSION);
    free(file_data);
    return false;
  }
  if (header.version < PROFILE_FILE_MIN_VERSION) {
    ESP_LOGW(TAG_CONFIG, "Profile version %d is older than min supported %d, attempting migration", header.version, PROFILE_FILE_MIN_VERSION);
  }
  if (header.version != PROFILE_FILE_VERSION) {
    ESP_LOGW(TAG_CONFIG, "Different version for profile %d: v%d (current: v%d)", profile_id, header.version, PROFILE_FILE_VERSION);
  }

  if (header.data_size == 0 || header.data_size > sizeof(config_profile_t)) {
    ESP_LOGE(TAG_CONFIG, "Invalid profile data size for profile %d: %d", profile_id, header.data_size);
    free(file_data);
    return false;
  }

  const uint8_t *data_ptr  = (const uint8_t *)file_data + sizeof(profile_header_t);
  uint32_t stored_checksum = 0;
  memcpy(&stored_checksum, data_ptr + header.data_size, sizeof(uint32_t));

  uint32_t calculated_checksum = calculate_checksum(data_ptr, header.data_size);
  if (calculated_checksum != stored_checksum) {
    ESP_LOGE(TAG_CONFIG, "Invalid checksum for profile %d: 0x%08X (expected 0x%08X)", profile_id, calculated_checksum, stored_checksum);
    free(file_data);
    return false;
  }

  // Copy the data (accept older profile sizes)
  memset(profile, 0, sizeof(config_profile_t));
  memcpy(profile, data_ptr, header.data_size);
  migrate_profile_if_needed(profile, header.version);

  if (header.version != PROFILE_FILE_VERSION) {
    ESP_LOGI(TAG_CONFIG, "Migrating profile %d from v%d to v%d", profile_id, header.version, PROFILE_FILE_VERSION);
    config_manager_save_profile(profile_id, profile);
  }

  ESP_LOGD(TAG_CONFIG, "Profile %d loaded (binary, %d bytes): %s", profile_id, buffer_size, profile->name);
  free(file_data);
  return true;
}

bool config_manager_delete_profile(uint16_t profile_id) {
  // Verify that the profile exists
  char filepath[64];
  snprintf(filepath, sizeof(filepath), "/spiffs/profiles/profile_%d.bin", profile_id);

  if (!spiffs_file_exists(filepath)) {
    ESP_LOGW(TAG_CONFIG, "Profile %d does not exist", profile_id);
    return false;
  }

  // Ensure the profile is not used in events from other profiles
  config_profile_t *temp_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (temp_profile == NULL) {
    ESP_LOGE(TAG_CONFIG, "Memory allocation error");
    return false;
  }

  // Use registry to scan only existing profiles (O(1) existence check)
  for (int p = 0; p < MAX_PROFILE_SCAN_LIMIT; p++) {
    if (p == profile_id)
      continue;

    // Skip non-existent profiles using registry
    if (!profile_registry_exists(p))
      continue;

    if (config_manager_load_profile(p, temp_profile)) {
      // Check whether this profile references the profile to delete
      for (int e = 0; e < CAN_EVENT_MAX; e++) {
        if (temp_profile->event_effects[e].action_type == EVENT_ACTION_SWITCH_PROFILE && temp_profile->event_effects[e].profile_id == profile_id) {
          ESP_LOGW(TAG_CONFIG, "Cannot delete profile %d: used by event %d in profile %d", profile_id, e, p);
          free(temp_profile);
          return false;
        }
      }
    }
  }
  free(temp_profile);

  // Delete the SPIFFS file
  esp_err_t err = spiffs_delete_file(filepath);
  if (err != ESP_OK) {
    ESP_LOGE(TAG_CONFIG, "Error deleting profile file %d", profile_id);
    return false;
  }

  // If the active profile was deleted, look for another profile
  if (active_profile_id == profile_id) {
    active_profile_id                = -1;
    active_profile_loaded            = false;

    // Use registry to find next available profile
    config_profile_t *search_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
    if (search_profile != NULL) {
      for (int i = 0; i < MAX_PROFILE_SCAN_LIMIT; i++) {
        // Skip non-existent profiles using registry
        if (!profile_registry_exists(i))
          continue;

        if (config_manager_load_profile(i, search_profile)) {
          memcpy(&active_profile, search_profile, sizeof(config_profile_t));
          active_profile_id     = i;
          active_profile_loaded = true;
          settings_set_i32("active_profile_id", i);
          ESP_LOGI(TAG_CONFIG, "Active profile deleted, activating profile %d", i);
          break;
        }
      }
      free(search_profile);
    }

    if (active_profile_id == -1) {
      settings_set_i32("active_profile_id", -1);
      memset(&active_profile, 0, sizeof(active_profile));
      ESP_LOGI(TAG_CONFIG, "No profile available");
    }
  }

  ESP_LOGI(TAG_CONFIG, "Profile %d deleted successfully from SPIFFS", profile_id);

  // Update registry
  profile_registry_clear(profile_id);

  return true;
}

bool config_manager_rename_profile(uint16_t profile_id, const char *new_name) {
  if (new_name == NULL) {
    return false;
  }

  size_t name_len = strnlen(new_name, PROFILE_NAME_MAX_LEN);
  if (name_len == 0) {
    return false;
  }

  // Allocate dynamically to avoid stack overflow
  config_profile_t *profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (profile == NULL) {
    ESP_LOGE(TAG_CONFIG, "Memory allocation error for rename");
    return false;
  }

  if (!config_manager_load_profile(profile_id, profile)) {
    ESP_LOGW(TAG_CONFIG, "Profile %d not found for rename", profile_id);
    free(profile);
    return false;
  }

  bool was_active = profile->active;

  memset(profile->name, 0, PROFILE_NAME_MAX_LEN);
  strncpy(profile->name, new_name, PROFILE_NAME_MAX_LEN - 1);

  bool success = config_manager_save_profile(profile_id, profile);
  free(profile);

  if (success && was_active) {
    // Reapply to preserve the active state and effect
    config_manager_activate_profile(profile_id);
  }

  return success;
}

bool config_manager_activate_profile(uint16_t profile_id) {
  // Load the new profile from SPIFFS
  if (!config_manager_load_profile(profile_id, &active_profile)) {
    ESP_LOGE(TAG_CONFIG, "Profile %d does not exist", profile_id);
    return false;
  }

  // Update the active ID
  active_profile_id     = profile_id;
  active_profile_loaded = true;

  // Save the active ID in SPIFFS
  settings_set_i32("active_profile_id", profile_id);

  // Stop all active events before applying the new effect
  config_manager_stop_all_events();

  // Apply the default effect
  led_effects_set_config(&active_profile.default_effect);
  effect_override_active = false;

  ESP_LOGI(TAG_CONFIG, "Profile %d activated: %s", profile_id, active_profile.name);
  return true;
}

bool config_manager_get_active_profile(config_profile_t *profile) {
  if (!active_profile_loaded || profile == NULL) {
    return false;
  }

  memcpy(profile, &active_profile, sizeof(config_profile_t));
  return true;
}

int config_manager_get_active_profile_id(void) {
  return active_profile_id;
}

bool config_manager_cycle_active_profile(int direction) {
  if (!active_profile_loaded || (direction != 1 && direction != -1)) {
    return false;
  }

  int start_id = active_profile_id < 0 ? 0 : active_profile_id;
  for (int step = 1; step < MAX_PROFILE_SCAN_LIMIT; step++) {
    int candidate = start_id + direction * step;
    if (candidate < 0 || candidate >= MAX_PROFILE_SCAN_LIMIT) {
      break;
    }

    // Check whether the profile exists (without fully loading it)
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/spiffs/profiles/profile_%d.bin", candidate);
    if (spiffs_file_exists(filepath)) {
      return config_manager_activate_profile((uint16_t)candidate);
    }
  }
  return false;
}

bool config_manager_get_dynamic_brightness(bool *enabled, uint8_t *rate) {
  if (!active_profile_loaded || !enabled || !rate) {
    return false;
  }

  *enabled = active_profile.dynamic_brightness_enabled;
  *rate    = active_profile.dynamic_brightness_rate;
  return true;
}

bool config_manager_is_dynamic_brightness_excluded(can_event_type_t event) {
  if (!active_profile_loaded) {
    return false;
  }
  if (event <= CAN_EVENT_NONE || event >= CAN_EVENT_MAX) {
    return false;
  }
  return (active_profile.dynamic_brightness_exclude_mask & (1ULL << event)) != 0;
}

bool config_manager_get_wheel_control_enabled(void) {
  return wheel_control_enabled;
}

bool config_manager_set_wheel_control_enabled(bool enabled) {
  wheel_control_enabled = enabled;
  esp_err_t err         = settings_set_bool("wheel_control_enabled", enabled);
  return err == ESP_OK;
}

int config_manager_get_wheel_control_speed_limit(void) {
  return (int)wheel_control_speed_limit;
}

bool config_manager_set_wheel_control_speed_limit(int speed_kph) {
  if (speed_kph < 0) {
    speed_kph = 0;
  }
  if (speed_kph > 100) {
    speed_kph = 100;
  }
  wheel_control_speed_limit = (uint8_t)speed_kph;
  esp_err_t err             = settings_set_u8("wheel_control_speed_limit", wheel_control_speed_limit);
  return err == ESP_OK;
}

int config_manager_list_profiles(config_profile_t *profile_list, int max_profiles) {
  if (profile_list == NULL || max_profiles <= 0) {
    return 0;
  }

  // Rebuild registry if not initialized
  if (!profile_registry_initialized) {
    profile_registry_rebuild();
  }

  int count = 0;

  // Use registry for O(1) existence check instead of O(n) file reads
  for (int i = 0; i < MAX_PROFILE_SCAN_LIMIT && count < max_profiles; i++) {
    if (profile_registry_exists(i)) {
      if (config_manager_load_profile(i, &profile_list[count])) {
        count++;
      }
    }
  }

  return count;
}

// Embedded JSON file - default.json
extern const uint8_t default_json_start[] asm("_binary_default_json_start");
extern const uint8_t default_json_end[] asm("_binary_default_json_end");

void config_manager_create_default_profile(config_profile_t *profile, const char *name) {
  bool success        = false;

  // 1. Load the embedded JSON file (default.json)
  size_t json_size    = default_json_end - default_json_start;

  // Allocate buffer for JSON (with null terminator)
  char *embedded_json = (char *)malloc(json_size + 1);
  if (embedded_json != NULL) {
    memcpy(embedded_json, default_json_start, json_size);
    embedded_json[json_size] = '\0'; // Null terminator

    ESP_LOGI(TAG_CONFIG, "Loading embedded default.json (%zu bytes)", json_size);
    success = config_manager_import_profile_from_json(embedded_json, profile);
    free(embedded_json);
  } else {
    ESP_LOGE(TAG_CONFIG, "Failed to allocate memory for embedded JSON");
  }

  // 2. If it fails, minimal fallback
  if (!success) {
    ESP_LOGE(TAG_CONFIG, "Failed to import embedded preset, using minimal fallback");
    memset(profile, 0, sizeof(config_profile_t));
    strncpy(profile->name, name, PROFILE_NAME_MAX_LEN - 1);
    profile->default_effect.effect     = EFFECT_SOLID;
    profile->default_effect.brightness = 20;
    profile->default_effect.speed      = 1;
    profile->default_effect.color1     = 0xFFFFFF;
    profile->active                    = false;
    return;
  }

  // Optional: Override name if provided
  if (name != NULL && strlen(name) > 0) {
    strncpy(profile->name, name, PROFILE_NAME_MAX_LEN - 1);
  }

  // Note: Missing events are already filled by the import function
  // with disabled default values (see config_manager_import_profile_from_json)

  // Timestamp parameters
  profile->active = false;
}

void config_manager_create_off_profile(config_profile_t *profile, const char *name) {
  memset(profile, 0, sizeof(config_profile_t));
  strncpy(profile->name, name ? name : "Eteint", PROFILE_NAME_MAX_LEN - 1);

  profile->default_effect.effect         = EFFECT_OFF;
  profile->default_effect.brightness     = 0;
  profile->default_effect.speed          = 1;
  profile->default_effect.color1         = 0;
  profile->default_effect.segment_start  = 0;
  profile->default_effect.segment_length = 0;
  profile->default_effect.audio_reactive = false;
  profile->default_effect.reverse        = false;
  profile->default_effect.sync_mode      = SYNC_OFF;

  for (int i = 0; i < CAN_EVENT_MAX; i++) {
    profile->event_effects[i].event                        = (can_event_type_t)i;
    profile->event_effects[i].action_type                  = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[i].enabled                      = false;
    profile->event_effects[i].priority                     = 0;
    profile->event_effects[i].duration_ms                  = 0;
    profile->event_effects[i].profile_id                   = -1;
    profile->event_effects[i].effect_config.effect         = EFFECT_OFF;
    profile->event_effects[i].effect_config.brightness     = 0;
    profile->event_effects[i].effect_config.speed          = 1;
    profile->event_effects[i].effect_config.color1         = 0;
    profile->event_effects[i].effect_config.color2         = 0;
    profile->event_effects[i].effect_config.color3         = 0;
    profile->event_effects[i].effect_config.segment_start  = 0;
    profile->event_effects[i].effect_config.segment_length = 0;
    profile->event_effects[i].effect_config.audio_reactive = false;
    profile->event_effects[i].effect_config.reverse        = false;
    profile->event_effects[i].effect_config.sync_mode      = SYNC_OFF;
  }

  profile->dynamic_brightness_enabled      = false;
  profile->dynamic_brightness_rate         = 0;
  profile->dynamic_brightness_exclude_mask = 0;
  profile->active                          = false;
}

bool config_manager_set_event_effect(uint16_t profile_id, can_event_type_t event, const effect_config_t *effect_config, uint16_t duration_ms, uint8_t priority) {
  if (event >= CAN_EVENT_MAX || effect_config == NULL) {
    return false;
  }

  // If this is the active profile, modify directly
  if (profile_id == active_profile_id && active_profile_loaded) {
    active_profile.event_effects[event].event = event;
    memcpy(&active_profile.event_effects[event].effect_config, effect_config, sizeof(effect_config_t));
    active_profile.event_effects[event].duration_ms = duration_ms;
    active_profile.event_effects[event].priority    = priority;
    active_profile.event_effects[event].enabled     = true;
    return true;
  }

  // Otherwise, load the profile, modify it, and save it
  config_profile_t *temp_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (temp_profile == NULL) {
    ESP_LOGE(TAG_CONFIG, "Memory allocation error");
    return false;
  }

  if (!config_manager_load_profile(profile_id, temp_profile)) {
    free(temp_profile);
    return false;
  }

  temp_profile->event_effects[event].event = event;
  memcpy(&temp_profile->event_effects[event].effect_config, effect_config, sizeof(effect_config_t));
  temp_profile->event_effects[event].duration_ms = duration_ms;
  temp_profile->event_effects[event].priority    = priority;
  temp_profile->event_effects[event].enabled     = true;

  bool success                                   = config_manager_save_profile(profile_id, temp_profile);
  free(temp_profile);

  return success;
}

bool config_manager_set_event_enabled(uint16_t profile_id, can_event_type_t event, bool enabled) {
  if (event >= CAN_EVENT_MAX) {
    return false;
  }

  // If this is the active profile, modify directly
  if (profile_id == active_profile_id && active_profile_loaded) {
    active_profile.event_effects[event].enabled = enabled;
    return true;
  }

  // Otherwise, load the profile, modify it, and save it
  config_profile_t *temp_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (temp_profile == NULL) {
    ESP_LOGE(TAG_CONFIG, "Memory allocation error");
    return false;
  }

  if (!config_manager_load_profile(profile_id, temp_profile)) {
    free(temp_profile);
    return false;
  }

  temp_profile->event_effects[event].enabled = enabled;

  bool success                               = config_manager_save_profile(profile_id, temp_profile);
  free(temp_profile);

  return success;
}

bool config_manager_process_can_event(can_event_type_t event) {
  if (!active_profile_loaded || event > CAN_EVENT_MAX) {
    return false;
  }

  can_event_effect_t *event_effect = &active_profile.event_effects[event];
  effect_config_t effect_to_apply;
  uint16_t duration_ms = 0;
  uint8_t priority     = 0;

  // Handle profile changes if configured (independent from the enabled flag)
  if (event_effect->action_type != EVENT_ACTION_APPLY_EFFECT) {
    // EVENT_ACTION_SWITCH_PROFILE
    if (event_effect->profile_id >= 0) {
      ESP_LOGI(TAG_CONFIG, "Event %d: Switching to profile %d", event, event_effect->profile_id);
      config_manager_activate_profile(event_effect->profile_id);

      return false;
    }
  }

  // If the effect is configured and enabled, use it
  if (event_effect->enabled) {
    memcpy(&effect_to_apply, &event_effect->effect_config, sizeof(effect_config_t));
    duration_ms            = event_effect->duration_ms;
    priority               = event_effect->priority;
    // For CAN events, only use color1 for all colors
    effect_to_apply.color2 = effect_to_apply.color1;
    effect_to_apply.color3 = effect_to_apply.color1;
    // if(effect_to_apply.segment_length == 0) {
    //   effect_to_apply.segment_length = total_leds;
    // }

    // Find a free slot for the event
    int slot               = -1;
    int existing_slot      = -1;
    int free_slot          = -1;

    // Check if the event is already active or find a free slot
    for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
      if (active_events[i].active && active_events[i].event == event) {
        existing_slot = i;
        break;
      }
      if (!active_events[i].active && free_slot == -1) {
        free_slot = i;
      }
    }

    // If the event already exists, update it
    slot = (existing_slot >= 0) ? existing_slot : free_slot;

    if (slot < 0) {
      // No free slot, check if a lower priority event can be overwritten
      // lower priority BUT only if it is the same zone or if the new
      // event is FULL
      int lowest_priority_slot = -1;
      uint8_t lowest_priority  = 255;

      for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
        if (active_events[i].active && active_events[i].priority < priority && active_events[i].priority < lowest_priority) {
          // New event has higher priority, we can overwrite
          lowest_priority      = active_events[i].priority;
          lowest_priority_slot = i;
        }
      }

      if (lowest_priority_slot >= 0) {
        slot = lowest_priority_slot;
        ESP_LOGI(TAG_CONFIG, "Overwriting event priority %d with priority %d", lowest_priority, priority);
      } else {
        ESP_LOGW(TAG_CONFIG, "Event '%s' ignored (no available slot)", config_manager_enum_to_id(event));
        return false;
      }
    }

    // Save the active event
    active_events[slot].event         = event;
    active_events[slot].effect_config = effect_to_apply;
    active_events[slot].duration_ms   = duration_ms;
    active_events[slot].priority      = priority;
    active_events[slot].active        = true;

    if (existing_slot >= 0) {
      // Keep animation phase; only reset timer for finite-duration events
      if (duration_ms > 0) {
        active_events[slot].start_time = xTaskGetTickCount();
      }
    } else {
      active_events[slot].start_time = xTaskGetTickCount();
    }

    // DO NOT apply immediately - let config_manager_update() handle it
    // according to per-zone priority to avoid visual glitches

    if (duration_ms > 0) {
      ESP_LOGI(TAG_CONFIG, "Effect '%s' enabled for %dms (priority %d)", config_manager_enum_to_id(event), duration_ms, priority);
    } else {
      ESP_LOGI(TAG_CONFIG, "Effect '%s' enabled (permanent, priority %d)", config_manager_enum_to_id(event), priority);
    }

    return true;
  } else {
    // ESP_LOGW(TAG_CONFIG, "Default effect ignored for '%s'",
    //         config_manager_enum_to_id(event));
  }

  return false;
}

void config_manager_stop_event(can_event_type_t event) {
  // Verify that the event is valid
  if (event >= CAN_EVENT_MAX) {
    return;
  }

  // Disable all slots that match this event
  for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
    if (active_events[i].active && active_events[i].event == event) {
      ESP_LOGI(TAG_CONFIG, "Stopping event '%s'", config_manager_enum_to_id(event));
      active_events[i].active = false;
    }
  }
}

void config_manager_stop_all_events(void) {
  for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
    if (active_events[i].active) {
      ESP_LOGI(TAG_CONFIG, "Stopping event '%s' globally", config_manager_enum_to_id(active_events[i].event));
      active_events[i].active = false;
    }
  }
}

void config_manager_update(void) {
  if (led_effects_is_ota_display_active()) {
    effect_override_active = false;
    return;
  }

  uint32_t now        = xTaskGetTickCount();
  uint16_t total_leds = led_effects_get_led_count();
  bool any_active     = false;

  if (total_leds == 0) {
    total_leds = NUM_LEDS;
  }
  if (total_leds == 0 || total_leds > MAX_LED_COUNT) {
    total_leds = MAX_LED_COUNT;
  }

  // Check and expire active events
  for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
    if (!active_events[i].active)
      continue;

    // Check if the event has expired (if duration > 0)
    if (active_events[i].duration_ms > 0) {
      uint32_t elapsed = now - active_events[i].start_time;
      if (elapsed >= pdMS_TO_TICKS(active_events[i].duration_ms)) {
        ESP_LOGI(TAG_CONFIG, "Event '%s' completed", config_manager_enum_to_id(active_events[i].event));
        active_events[i].active = false;
        continue;
      }
    }

    any_active = true;
  }

  // Optimization: if no active event, let the default effect render
  if (!any_active) {
    effect_override_active = false;
    return;
  }

  // Initialize buffers
  memset(priority_buffer, 0, total_leds * sizeof(uint8_t));
  memset(composed_buffer, 0, total_leds * sizeof(led_rgb_t));

  uint32_t frame_counter = led_effects_get_frame_counter();
  bool needs_fft         = false;

  // Render the default effect as a base layer
  if (active_profile_loaded) {
    effect_config_t base    = active_profile.default_effect;

    // Use the segment configured in the profile (or full strip if none)
    uint16_t default_start  = base.segment_start;
    uint16_t default_length = base.segment_length;

    // Normalize segment
    led_effects_normalize_segment(&default_start, &default_length, total_leds);

    // Modulate by accel_pedal_pos if enabled
    if (base.accel_pedal_pos_enabled) {
      default_length = led_effects_apply_accel_modulation(default_length, led_effects_get_accel_pedal_pos(), base.accel_pedal_offset);
    }

    memset(temp_buffer, 0, total_leds * sizeof(led_rgb_t));
    led_effects_set_event_context(CAN_EVENT_NONE);
    led_effects_render_to_buffer(&base, default_start, default_length, frame_counter, temp_buffer);

    for (uint16_t idx = 0; idx < total_leds; idx++) {
      composed_buffer[idx] = temp_buffer[idx];
    }

    needs_fft |= led_effects_requires_fft(active_profile.default_effect.effect);
  }

  // Render events on top of the base layer (per-LED priority)
  for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
    if (!active_events[i].active)
      continue;

    uint16_t start  = active_events[i].effect_config.segment_start;
    uint16_t length = active_events[i].effect_config.segment_length;

    // Normalize length == 0 -> full strip
    if (length == 0) {
      length = total_leds;
    }

    if (start >= total_leds) {
      continue;
    }
    if ((uint32_t)start + length > total_leds) {
      length = total_leds - start;
    }

    memset(temp_buffer, 0, total_leds * sizeof(led_rgb_t));

    effect_config_t event_config = active_events[i].effect_config;
    event_config.segment_start   = 0;
    event_config.segment_length  = length;

    led_effects_set_event_context(active_events[i].event);
    led_effects_render_to_buffer(&event_config, 0, length, frame_counter, temp_buffer);

    // Apply per-LED priority overlay (segment-local buffer)
    for (uint16_t j = 0; j < length; j++) {
      uint16_t idx = start + j;
      if (idx >= total_leds) {
        break;
      }
      if (active_events[i].priority >= priority_buffer[idx]) {
        composed_buffer[idx] = temp_buffer[j];
        priority_buffer[idx] = active_events[i].priority;
      }
    }

    if (led_effects_requires_fft(active_events[i].effect_config.effect)) {
      needs_fft = true;
    }
  }

  audio_input_set_fft_enabled(needs_fft);

  led_effects_set_event_context(CAN_EVENT_NONE);
  led_effects_show_buffer(composed_buffer);

  effect_override_active = any_active;
}

bool config_manager_has_active_events(void) {
  return effect_override_active;
}

// Mapping table enum -> alphanumeric ID
const char *config_manager_enum_to_id(can_event_type_t event) {
  switch (event) {
  case CAN_EVENT_NONE:
    return EVENT_ID_NONE;
  case CAN_EVENT_TURN_LEFT:
    return EVENT_ID_TURN_LEFT;
  case CAN_EVENT_TURN_RIGHT:
    return EVENT_ID_TURN_RIGHT;
  case CAN_EVENT_TURN_HAZARD:
    return EVENT_ID_TURN_HAZARD;
  case CAN_EVENT_CHARGING:
    return EVENT_ID_CHARGING;
  case CAN_EVENT_CHARGE_COMPLETE:
    return EVENT_ID_CHARGE_COMPLETE;
  case CAN_EVENT_CHARGING_STARTED:
    return EVENT_ID_CHARGING_STARTED;
  case CAN_EVENT_CHARGING_STOPPED:
    return EVENT_ID_CHARGING_STOPPED;
  case CAN_EVENT_CHARGING_CABLE_CONNECTED:
    return EVENT_ID_CHARGING_CABLE_CONNECTED;
  case CAN_EVENT_CHARGING_CABLE_DISCONNECTED:
    return EVENT_ID_CHARGING_CABLE_DISCONNECTED;
  case CAN_EVENT_CHARGING_PORT_OPENED:
    return EVENT_ID_CHARGING_PORT_OPENED;
  case CAN_EVENT_DOOR_OPEN_LEFT:
    return EVENT_ID_DOOR_OPEN_LEFT;
  case CAN_EVENT_DOOR_CLOSE_LEFT:
    return EVENT_ID_DOOR_CLOSE_LEFT;
  case CAN_EVENT_DOOR_OPEN_RIGHT:
    return EVENT_ID_DOOR_OPEN_RIGHT;
  case CAN_EVENT_DOOR_CLOSE_RIGHT:
    return EVENT_ID_DOOR_CLOSE_RIGHT;
  case CAN_EVENT_LOCKED:
    return EVENT_ID_LOCKED;
  case CAN_EVENT_UNLOCKED:
    return EVENT_ID_UNLOCKED;
  case CAN_EVENT_BRAKE_ON:
    return EVENT_ID_BRAKE_ON;
  case CAN_EVENT_BLINDSPOT_LEFT:
    return EVENT_ID_BLINDSPOT_LEFT;
  case CAN_EVENT_BLINDSPOT_RIGHT:
    return EVENT_ID_BLINDSPOT_RIGHT;
  case CAN_EVENT_BLINDSPOT_LEFT_ALERT:
    return EVENT_ID_BLINDSPOT_LEFT_ALERT;
  case CAN_EVENT_BLINDSPOT_RIGHT_ALERT:
    return EVENT_ID_BLINDSPOT_RIGHT_ALERT;
  case CAN_EVENT_SIDE_COLLISION_LEFT:
    return EVENT_ID_SIDE_COLLISION_LEFT;
  case CAN_EVENT_SIDE_COLLISION_RIGHT:
    return EVENT_ID_SIDE_COLLISION_RIGHT;
  case CAN_EVENT_FORWARD_COLLISION:
    return EVENT_ID_FORWARD_COLLISION;
  case CAN_EVENT_LANE_DEPARTURE_LEFT_LV1:
    return EVENT_ID_LANE_DEPARTURE_LEFT_LV1;
  case CAN_EVENT_LANE_DEPARTURE_LEFT_LV2:
    return EVENT_ID_LANE_DEPARTURE_LEFT_LV2;
  case CAN_EVENT_LANE_DEPARTURE_RIGHT_LV1:
    return EVENT_ID_LANE_DEPARTURE_RIGHT_LV1;
  case CAN_EVENT_LANE_DEPARTURE_RIGHT_LV2:
    return EVENT_ID_LANE_DEPARTURE_RIGHT_LV2;
  case CAN_EVENT_SPEED_THRESHOLD:
    return EVENT_ID_SPEED_THRESHOLD;
  case CAN_EVENT_AUTOPILOT_ENGAGED:
    return EVENT_ID_AUTOPILOT_ENGAGED;
  case CAN_EVENT_AUTOPILOT_DISENGAGED:
    return EVENT_ID_AUTOPILOT_DISENGAGED;
  case CAN_EVENT_AUTOPILOT_ALERT_LV1:
    return EVENT_ID_AUTOPILOT_ALERT_LV1;
  case CAN_EVENT_AUTOPILOT_ALERT_LV2:
    return EVENT_ID_AUTOPILOT_ALERT_LV2;
  case CAN_EVENT_GEAR_DRIVE:
    return EVENT_ID_GEAR_DRIVE;
  case CAN_EVENT_GEAR_REVERSE:
    return EVENT_ID_GEAR_REVERSE;
  case CAN_EVENT_GEAR_PARK:
    return EVENT_ID_GEAR_PARK;
  case CAN_EVENT_SENTRY_MODE_ON:
    return EVENT_ID_SENTRY_MODE_ON;
  case CAN_EVENT_SENTRY_MODE_OFF:
    return EVENT_ID_SENTRY_MODE_OFF;
  case CAN_EVENT_SENTRY_ALERT:
    return EVENT_ID_SENTRY_ALERT;
  default:
    return EVENT_ID_NONE;
  }
}

// Mapping table alphanumeric ID -> enum
can_event_type_t config_manager_id_to_enum(const char *id) {
  if (id == NULL)
    return CAN_EVENT_NONE;

  if (strcmp(id, EVENT_ID_NONE) == 0)
    return CAN_EVENT_NONE;
  if (strcmp(id, EVENT_ID_TURN_LEFT) == 0)
    return CAN_EVENT_TURN_LEFT;
  if (strcmp(id, EVENT_ID_TURN_RIGHT) == 0)
    return CAN_EVENT_TURN_RIGHT;
  if (strcmp(id, EVENT_ID_TURN_HAZARD) == 0)
    return CAN_EVENT_TURN_HAZARD;
  if (strcmp(id, EVENT_ID_CHARGING) == 0)
    return CAN_EVENT_CHARGING;
  if (strcmp(id, EVENT_ID_CHARGE_COMPLETE) == 0)
    return CAN_EVENT_CHARGE_COMPLETE;
  if (strcmp(id, EVENT_ID_CHARGING_STARTED) == 0)
    return CAN_EVENT_CHARGING_STARTED;
  if (strcmp(id, EVENT_ID_CHARGING_STOPPED) == 0)
    return CAN_EVENT_CHARGING_STOPPED;
  if (strcmp(id, EVENT_ID_CHARGING_CABLE_CONNECTED) == 0)
    return CAN_EVENT_CHARGING_CABLE_CONNECTED;
  if (strcmp(id, EVENT_ID_CHARGING_CABLE_DISCONNECTED) == 0)
    return CAN_EVENT_CHARGING_CABLE_DISCONNECTED;
  if (strcmp(id, EVENT_ID_CHARGING_PORT_OPENED) == 0)
    return CAN_EVENT_CHARGING_PORT_OPENED;
  if (strcmp(id, EVENT_ID_DOOR_OPEN_LEFT) == 0)
    return CAN_EVENT_DOOR_OPEN_LEFT;
  if (strcmp(id, EVENT_ID_DOOR_CLOSE_LEFT) == 0)
    return CAN_EVENT_DOOR_CLOSE_LEFT;
  if (strcmp(id, EVENT_ID_DOOR_OPEN_RIGHT) == 0)
    return CAN_EVENT_DOOR_OPEN_RIGHT;
  if (strcmp(id, EVENT_ID_DOOR_CLOSE_RIGHT) == 0)
    return CAN_EVENT_DOOR_CLOSE_RIGHT;
  if (strcmp(id, EVENT_ID_LOCKED) == 0)
    return CAN_EVENT_LOCKED;
  if (strcmp(id, EVENT_ID_UNLOCKED) == 0)
    return CAN_EVENT_UNLOCKED;
  if (strcmp(id, EVENT_ID_BRAKE_ON) == 0)
    return CAN_EVENT_BRAKE_ON;
  if (strcmp(id, EVENT_ID_BLINDSPOT_LEFT) == 0)
    return CAN_EVENT_BLINDSPOT_LEFT;
  if (strcmp(id, EVENT_ID_BLINDSPOT_RIGHT) == 0)
    return CAN_EVENT_BLINDSPOT_RIGHT;
  if (strcmp(id, EVENT_ID_BLINDSPOT_LEFT_ALERT) == 0)
    return CAN_EVENT_BLINDSPOT_LEFT_ALERT;
  if (strcmp(id, EVENT_ID_BLINDSPOT_RIGHT_ALERT) == 0)
    return CAN_EVENT_BLINDSPOT_RIGHT_ALERT;
  if (strcmp(id, EVENT_ID_SIDE_COLLISION_LEFT) == 0)
    return CAN_EVENT_SIDE_COLLISION_LEFT;
  if (strcmp(id, EVENT_ID_SIDE_COLLISION_RIGHT) == 0)
    return CAN_EVENT_SIDE_COLLISION_RIGHT;
  if (strcmp(id, EVENT_ID_FORWARD_COLLISION) == 0)
    return CAN_EVENT_FORWARD_COLLISION;
  if (strcmp(id, EVENT_ID_LANE_DEPARTURE_LEFT_LV1) == 0)
    return CAN_EVENT_LANE_DEPARTURE_LEFT_LV1;
  if (strcmp(id, EVENT_ID_LANE_DEPARTURE_LEFT_LV2) == 0)
    return CAN_EVENT_LANE_DEPARTURE_LEFT_LV2;
  if (strcmp(id, EVENT_ID_LANE_DEPARTURE_RIGHT_LV1) == 0)
    return CAN_EVENT_LANE_DEPARTURE_RIGHT_LV1;
  if (strcmp(id, EVENT_ID_LANE_DEPARTURE_RIGHT_LV2) == 0)
    return CAN_EVENT_LANE_DEPARTURE_RIGHT_LV2;
  if (strcmp(id, EVENT_ID_SPEED_THRESHOLD) == 0)
    return CAN_EVENT_SPEED_THRESHOLD;
  if (strcmp(id, EVENT_ID_AUTOPILOT_ENGAGED) == 0)
    return CAN_EVENT_AUTOPILOT_ENGAGED;
  if (strcmp(id, EVENT_ID_AUTOPILOT_DISENGAGED) == 0)
    return CAN_EVENT_AUTOPILOT_DISENGAGED;
  if (strcmp(id, EVENT_ID_AUTOPILOT_ALERT_LV1) == 0)
    return CAN_EVENT_AUTOPILOT_ALERT_LV1;
  if (strcmp(id, EVENT_ID_AUTOPILOT_ALERT_LV2) == 0)
    return CAN_EVENT_AUTOPILOT_ALERT_LV2;
  if (strcmp(id, EVENT_ID_GEAR_DRIVE) == 0)
    return CAN_EVENT_GEAR_DRIVE;
  if (strcmp(id, EVENT_ID_GEAR_REVERSE) == 0)
    return CAN_EVENT_GEAR_REVERSE;
  if (strcmp(id, EVENT_ID_GEAR_PARK) == 0)
    return CAN_EVENT_GEAR_PARK;
  if (strcmp(id, EVENT_ID_SENTRY_MODE_ON) == 0)
    return CAN_EVENT_SENTRY_MODE_ON;
  if (strcmp(id, EVENT_ID_SENTRY_MODE_OFF) == 0)
    return CAN_EVENT_SENTRY_MODE_OFF;
  if (strcmp(id, EVENT_ID_SENTRY_ALERT) == 0)
    return CAN_EVENT_SENTRY_ALERT;

  ESP_LOGW(TAG_CONFIG, "Unknown event ID: %s", id);
  return CAN_EVENT_NONE;
}

// Check if an event can trigger a profile change
bool config_manager_event_can_switch_profile(can_event_type_t event) {
  switch (event) {
  case CAN_EVENT_DOOR_OPEN_LEFT:
  case CAN_EVENT_DOOR_CLOSE_LEFT:
  case CAN_EVENT_DOOR_OPEN_RIGHT:
  case CAN_EVENT_DOOR_CLOSE_RIGHT:
  case CAN_EVENT_CHARGING:
  case CAN_EVENT_CHARGE_COMPLETE:
  case CAN_EVENT_SPEED_THRESHOLD:
  case CAN_EVENT_LOCKED:
  case CAN_EVENT_UNLOCKED:
  case CAN_EVENT_AUTOPILOT_ENGAGED:
  case CAN_EVENT_AUTOPILOT_DISENGAGED:
  case CAN_EVENT_AUTOPILOT_ALERT_LV1:
  case CAN_EVENT_AUTOPILOT_ALERT_LV2:
  case CAN_EVENT_FORWARD_COLLISION:
  case CAN_EVENT_GEAR_DRIVE:
  case CAN_EVENT_GEAR_REVERSE:
  case CAN_EVENT_GEAR_PARK:
  case CAN_EVENT_SENTRY_MODE_ON:
  case CAN_EVENT_SENTRY_MODE_OFF:
    return true;
  default:
    return false;
  }
}

// Full factory reset
bool config_manager_factory_reset(void) {
  ESP_LOGI(TAG_CONFIG, "Factory reset: Erasing NVS and SPIFFS...");

  // Completely erase NVS (WiFi credentials, etc.)
  esp_err_t err = nvs_flash_erase();
  if (err != ESP_OK) {
    ESP_LOGE(TAG_CONFIG, "Failed to erase NVS: %s", esp_err_to_name(err));
    return false;
  }

  // Reset the NVS
  err = nvs_flash_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG_CONFIG, "Failed to reinit NVS: %s", esp_err_to_name(err));
    return false;
  }

  // Fully format the SPIFFS (erase everything)
  err = spiffs_format();
  if (err != ESP_OK) {
    ESP_LOGE(TAG_CONFIG, "Failed to format SPIFFS: %s", esp_err_to_name(err));
    return false;
  }

  // Reset the settings manager (will create the settings.json file)
  err = settings_manager_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG_CONFIG, "Failed to reinit settings manager");
    return false;
  }

  wheel_control_enabled     = false;
  wheel_control_speed_limit = 5;

  // Reset the active profile in RAM
  memset(&active_profile, 0, sizeof(active_profile));
  active_profile_id                 = -1;
  active_profile_loaded             = false;

  // Create base profiles without using the stack (avoid httpd overflow)
  config_profile_t *default_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  config_profile_t *off_profile     = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (off_profile) {
    config_manager_create_off_profile(off_profile, "Eteint");
    config_manager_save_profile(0, off_profile);
  }
  if (default_profile) {
    config_manager_create_default_profile(default_profile, "Default");
    config_manager_save_profile(1, default_profile);
    memcpy(&active_profile, default_profile, sizeof(config_profile_t));
    active_profile_id     = 1;
    active_profile_loaded = true;
  }
  config_manager_activate_profile(1);
  if (default_profile) {
    free(default_profile);
  }
  if (off_profile) {
    free(off_profile);
  }

  ESP_LOGI(TAG_CONFIG, "Factory reset complete. Default profile created.");
  return true;
}

// Conversion 0-255 to percentage 1-100
static uint8_t value_to_percent(uint8_t value) {
  if (value == 0) {
    return 1;
  }
  uint8_t percent = (value * 100 + 127) / 255; // Round to nearest
  return (percent < 1) ? 1 : (percent > 100) ? 100 : percent;
}

// Conversion percentage 1-100 to 0-255
static uint8_t percent_to_value(uint8_t percent) {
  if (percent < 1)
    percent = 1;
  if (percent > 100)
    percent = 100;
  return (percent * 255 + 50) / 100; // Round to nearest
}

// Internal function to export a profile to JSON (directly from the structure)
static bool export_profile_to_json(const config_profile_t *profile, uint16_t profile_id, char *json_buffer, size_t buffer_size) {
  if (profile == NULL || json_buffer == NULL) {
    return false;
  }

  cJSON *root = cJSON_CreateObject();

  // Metadata
  cJSON_AddStringToObject(root, "name", profile->name);
  cJSON_AddNumberToObject(root, "version", 1);

  // Default effect - convert to percentage for export
  cJSON *default_effect = cJSON_CreateObject();
  cJSON_AddStringToObject(default_effect, "effect_id", led_effects_enum_to_id(profile->default_effect.effect));
  cJSON_AddNumberToObject(default_effect, "brightness", value_to_percent(profile->default_effect.brightness));
  cJSON_AddNumberToObject(default_effect, "speed", value_to_percent(profile->default_effect.speed));
  cJSON_AddNumberToObject(default_effect, "color1", profile->default_effect.color1);
  cJSON_AddNumberToObject(default_effect, "color2", profile->default_effect.color2);
  cJSON_AddNumberToObject(default_effect, "color3", profile->default_effect.color3);
  cJSON_AddBoolToObject(default_effect, "reverse", profile->default_effect.reverse);
  cJSON_AddBoolToObject(default_effect, "audio_reactive", profile->default_effect.audio_reactive);
  cJSON_AddNumberToObject(default_effect, "segment_start", profile->default_effect.segment_start);
  cJSON_AddNumberToObject(default_effect, "segment_length", profile->default_effect.segment_length);
  cJSON_AddBoolToObject(default_effect, "accel_pedal_pos_enabled", profile->default_effect.accel_pedal_pos_enabled);
  cJSON_AddNumberToObject(default_effect, "accel_pedal_offset", profile->default_effect.accel_pedal_offset);
  cJSON_AddItemToObject(root, "default_effect", default_effect);

  // Dynamic brightness parameters
  cJSON_AddBoolToObject(root, "dynamic_brightness_enabled", profile->dynamic_brightness_enabled);
  cJSON_AddNumberToObject(root, "dynamic_brightness_rate", profile->dynamic_brightness_rate);
  cJSON *dyn_excluded = cJSON_CreateArray();
  for (int i = CAN_EVENT_NONE + 1; i < CAN_EVENT_MAX; i++) {
    if (profile->dynamic_brightness_exclude_mask & (1ULL << i)) {
      cJSON_AddItemToArray(dyn_excluded, cJSON_CreateString(config_manager_enum_to_id((can_event_type_t)i)));
    }
  }
  cJSON_AddItemToObject(root, "dynamic_brightness_excluded_events", dyn_excluded);

  // CAN events
  cJSON *events = cJSON_CreateArray();
  for (int i = 0; i < CAN_EVENT_MAX; i++) {
    if (profile->event_effects[i].enabled) {
      cJSON *event         = cJSON_CreateObject();
      // Use the alphanumeric ID instead of the enum for compatibility
      const char *event_id = config_manager_enum_to_id(profile->event_effects[i].event);
      cJSON_AddStringToObject(event, "event_id", event_id);
      cJSON_AddBoolToObject(event, "enabled", profile->event_effects[i].enabled);
      cJSON_AddNumberToObject(event, "priority", profile->event_effects[i].priority);
      cJSON_AddNumberToObject(event, "duration_ms", profile->event_effects[i].duration_ms);

      cJSON *event_effect = cJSON_CreateObject();
      cJSON_AddStringToObject(event_effect, "effect_id", led_effects_enum_to_id(profile->event_effects[i].effect_config.effect));
      cJSON_AddNumberToObject(event_effect, "brightness", value_to_percent(profile->event_effects[i].effect_config.brightness));
      cJSON_AddNumberToObject(event_effect, "speed", value_to_percent(profile->event_effects[i].effect_config.speed));
      cJSON_AddNumberToObject(event_effect, "color1", profile->event_effects[i].effect_config.color1);
      cJSON_AddNumberToObject(event_effect, "color2", profile->event_effects[i].effect_config.color2);
      cJSON_AddNumberToObject(event_effect, "color3", profile->event_effects[i].effect_config.color3);
      cJSON_AddBoolToObject(event_effect, "reverse", profile->event_effects[i].effect_config.reverse);
      cJSON_AddNumberToObject(event_effect, "segment_start", profile->event_effects[i].effect_config.segment_start);
      cJSON_AddNumberToObject(event_effect, "segment_length", profile->event_effects[i].effect_config.segment_length);
      cJSON_AddItemToObject(event, "effect_config", event_effect);

      cJSON_AddItemToArray(events, event);
    }
  }
  cJSON_AddItemToObject(root, "event_effects", events);

  // Convert to JSON string
  char *json_str = cJSON_PrintUnformatted(root);
  bool success   = false;

  if (json_str) {
    size_t len = strlen(json_str);
    if (len < buffer_size) {
      strcpy(json_buffer, json_str);
      ESP_LOGD(TAG_CONFIG, "Profile %d exported successfully (%d bytes)", profile_id, len);
      success = true;
    } else {
      ESP_LOGE(TAG_CONFIG, "Buffer too small to export profile %d: %d bytes needed, %d available", profile_id, len + 1, buffer_size);
    }
    free(json_str);
  } else {
    size_t free_heap = esp_get_free_heap_size();
    ESP_LOGE(TAG_CONFIG, "cJSON_PrintUnformatted error for profile %d - Heap after failure: %d bytes", profile_id, free_heap);
  }

  cJSON_Delete(root);
  return success;
}

// Public function to export a profile (loads from SPIFFS if needed)
bool config_manager_export_profile(uint16_t profile_id, char *json_buffer, size_t buffer_size) {
  if (json_buffer == NULL) {
    return false;
  }

  // Load the profile (or use the active profile if it matches)
  config_profile_t *profile      = NULL;
  config_profile_t *temp_profile = NULL;

  if (profile_id == active_profile_id && active_profile_loaded) {
    profile = &active_profile;
  } else {
    temp_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
    if (temp_profile == NULL) {
      ESP_LOGE(TAG_CONFIG, "Memory allocation error");
      return false;
    }

    if (!config_manager_load_profile(profile_id, temp_profile)) {
      ESP_LOGW(TAG_CONFIG, "Profile %d does not exist, cannot export", profile_id);
      free(temp_profile);
      return false;
    }

    profile = temp_profile;
  }

  bool success = export_profile_to_json(profile, profile_id, json_buffer, buffer_size);

  if (temp_profile != NULL) {
    free(temp_profile);
  }

  return success;
}

bool config_manager_import_profile_from_json(const char *json_string, config_profile_t *profile) {
  if (json_string == NULL || profile == NULL) {
    return false;
  }

  size_t json_len         = strlen(json_string);
  size_t free_heap_before = esp_get_free_heap_size();
  ESP_LOGI(TAG_CONFIG, "Parsing JSON: size=%d bytes, free heap=%d bytes", json_len, free_heap_before);

  // Estimate memory needed: roughly 3x JSON size for cJSON tree
  size_t estimated_needed = json_len * 3;
  if (free_heap_before < estimated_needed) {
    ESP_LOGW(TAG_CONFIG, "Low memory: free=%d, estimated needed=%d bytes", free_heap_before, estimated_needed);
  }

  cJSON *root = cJSON_Parse(json_string);
  if (!root) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != NULL) {
      // Calculate position of error in JSON
      size_t error_pos = error_ptr - json_string;
      ESP_LOGE(TAG_CONFIG, "JSON parsing error at position %d (heap before: %d bytes)", error_pos, free_heap_before);
      // Show a snippet of JSON around the error
      if (error_pos >= 20) {
        char snippet[41];
        strncpy(snippet, error_ptr - 20, 40);
        snippet[40] = '\0';
        ESP_LOGE(TAG_CONFIG, "Error near: ...%s", snippet);
      }
    } else {
      ESP_LOGE(TAG_CONFIG, "JSON parsing error (likely out of memory, free heap: %d bytes)", free_heap_before);
    }
    return false;
  }

  size_t free_heap_after = esp_get_free_heap_size();
  ESP_LOGI(TAG_CONFIG, "JSON parsed successfully, heap used=%d bytes", free_heap_before - free_heap_after);

  memset(profile, 0, sizeof(config_profile_t));

  // Metadata
  const cJSON *name = cJSON_GetObjectItem(root, "name");
  if (name && cJSON_IsString(name)) {
    strncpy(profile->name, name->valuestring, PROFILE_NAME_MAX_LEN - 1);
  } else {
    ESP_LOGE(TAG_CONFIG, "Missing or invalid 'name' field");
    cJSON_Delete(root);
    return false;
  }
  profile->active             = false;

  // Default effect
  const cJSON *default_effect = cJSON_GetObjectItem(root, "default_effect");
  if (default_effect && cJSON_IsObject(default_effect)) {
    cJSON *item;
    if ((item = cJSON_GetObjectItem(default_effect, "effect_id")) && cJSON_IsString(item)) {
      profile->default_effect.effect = led_effects_id_to_enum(item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(default_effect, "brightness")) && cJSON_IsNumber(item))
      profile->default_effect.brightness = percent_to_value(item->valueint);
    if ((item = cJSON_GetObjectItem(default_effect, "speed")) && cJSON_IsNumber(item))
      profile->default_effect.speed = percent_to_value(item->valueint);
    if ((item = cJSON_GetObjectItem(default_effect, "color1")) && cJSON_IsNumber(item))
      profile->default_effect.color1 = item->valueint;
    if ((item = cJSON_GetObjectItem(default_effect, "color2")) && cJSON_IsNumber(item))
      profile->default_effect.color2 = item->valueint;
    if ((item = cJSON_GetObjectItem(default_effect, "color3")) && cJSON_IsNumber(item))
      profile->default_effect.color3 = item->valueint;
    if ((item = cJSON_GetObjectItem(default_effect, "reverse")) && cJSON_IsBool(item))
      profile->default_effect.reverse = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(default_effect, "audio_reactive")) && cJSON_IsBool(item))
      profile->default_effect.audio_reactive = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(default_effect, "segment_start")) && cJSON_IsNumber(item))
      profile->default_effect.segment_start = item->valueint;
    if ((item = cJSON_GetObjectItem(default_effect, "segment_length")) && cJSON_IsNumber(item))
      profile->default_effect.segment_length = item->valueint;
    if ((item = cJSON_GetObjectItem(default_effect, "accel_pedal_pos_enabled")) && cJSON_IsBool(item))
      profile->default_effect.accel_pedal_pos_enabled = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(default_effect, "accel_pedal_offset")) && cJSON_IsNumber(item))
      profile->default_effect.accel_pedal_offset = item->valueint;
  }

  // Dynamic brightness parameters
  const cJSON *dyn_bright_enabled = cJSON_GetObjectItem(root, "dynamic_brightness_enabled");
  if (dyn_bright_enabled && cJSON_IsBool(dyn_bright_enabled)) {
    profile->dynamic_brightness_enabled = cJSON_IsTrue(dyn_bright_enabled);
  } else {
    profile->dynamic_brightness_enabled = false; // Default
  }

  const cJSON *dyn_bright_rate = cJSON_GetObjectItem(root, "dynamic_brightness_rate");
  if (dyn_bright_rate && cJSON_IsNumber(dyn_bright_rate)) {
    profile->dynamic_brightness_rate = dyn_bright_rate->valueint;
  } else {
    profile->dynamic_brightness_rate = 50; // Default
  }

  profile->dynamic_brightness_exclude_mask = 0;
  const cJSON *dyn_bright_excluded         = cJSON_GetObjectItem(root, "dynamic_brightness_excluded_events");
  if (dyn_bright_excluded && cJSON_IsArray(dyn_bright_excluded)) {
    const cJSON *excluded = NULL;
    cJSON_ArrayForEach(excluded, dyn_bright_excluded) {
      if (excluded && cJSON_IsString(excluded)) {
        can_event_type_t evt = config_manager_id_to_enum(excluded->valuestring);
        if (evt > CAN_EVENT_NONE && evt < CAN_EVENT_MAX) {
          profile->dynamic_brightness_exclude_mask |= (1ULL << evt);
        }
      }
    }
  }

  // CAN events
  cJSON *events = cJSON_GetObjectItem(root, "event_effects");
  if (events && cJSON_IsArray(events)) {
    const cJSON *event = NULL;
    cJSON_ArrayForEach(event, events) {
      // New format: use event_id (alphanumeric string)
      const cJSON *event_id_obj = cJSON_GetObjectItem(event, "event_id");
      int evt                   = -1;

      if (event_id_obj && cJSON_IsString(event_id_obj)) {
        // New format with alphanumeric ID
        evt = config_manager_id_to_enum(event_id_obj->valuestring);
      } else {
        // Old format with enum number (backward compatibility)
        const cJSON *event_type = cJSON_GetObjectItem(event, "event");
        if (event_type && cJSON_IsNumber(event_type)) {
          evt = event_type->valueint;
        }
      }

      if (evt < 0 || evt >= CAN_EVENT_MAX)
        continue;

      const cJSON *enabled = cJSON_GetObjectItem(event, "enabled");
      if (enabled && cJSON_IsBool(enabled)) {
        profile->event_effects[evt].enabled = cJSON_IsTrue(enabled);
      }

      const cJSON *priority = cJSON_GetObjectItem(event, "priority");
      if (priority && cJSON_IsNumber(priority)) {
        profile->event_effects[evt].priority = priority->valueint;
      }

      const cJSON *duration = cJSON_GetObjectItem(event, "duration_ms");
      if (duration && cJSON_IsNumber(duration)) {
        profile->event_effects[evt].duration_ms = duration->valueint;
      }

      profile->event_effects[evt].event = evt;

      const cJSON *effect_config        = cJSON_GetObjectItem(event, "effect_config");
      if (effect_config && cJSON_IsObject(effect_config)) {
        cJSON *item;
        if ((item = cJSON_GetObjectItem(effect_config, "effect_id")) && cJSON_IsString(item)) {
          profile->event_effects[evt].effect_config.effect = led_effects_id_to_enum(item->valuestring);
        }
        if ((item = cJSON_GetObjectItem(effect_config, "brightness")) && cJSON_IsNumber(item))
          profile->event_effects[evt].effect_config.brightness = percent_to_value(item->valueint);
        if ((item = cJSON_GetObjectItem(effect_config, "speed")) && cJSON_IsNumber(item))
          profile->event_effects[evt].effect_config.speed = percent_to_value(item->valueint);
        if ((item = cJSON_GetObjectItem(effect_config, "color1")) && cJSON_IsNumber(item))
          profile->event_effects[evt].effect_config.color1 = item->valueint;
        if ((item = cJSON_GetObjectItem(effect_config, "color2")) && cJSON_IsNumber(item))
          profile->event_effects[evt].effect_config.color2 = item->valueint;
        if ((item = cJSON_GetObjectItem(effect_config, "color3")) && cJSON_IsNumber(item))
          profile->event_effects[evt].effect_config.color3 = item->valueint;
        if ((item = cJSON_GetObjectItem(effect_config, "reverse")) && cJSON_IsBool(item))
          profile->event_effects[evt].effect_config.reverse = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(effect_config, "segment_start")) && cJSON_IsNumber(item))
          profile->event_effects[evt].effect_config.segment_start = item->valueint;
        if ((item = cJSON_GetObjectItem(effect_config, "segment_length")) && cJSON_IsNumber(item))
          profile->event_effects[evt].effect_config.segment_length = item->valueint;
      }
    }
  }

  // Fill any missing events with default disabled values
  // Allows managing new events added after the preset was created
  for (int evt = CAN_EVENT_NONE; evt < CAN_EVENT_MAX; evt++) {
    // If the event was not initialized (effect_config.effect == 0 and enabled == false)
    // AND it is not NONE, complete it with default values
    if (evt != CAN_EVENT_NONE && profile->event_effects[evt].event == 0 && !profile->event_effects[evt].enabled) {
      profile->event_effects[evt].event                = evt;
      profile->event_effects[evt].enabled              = false;
      profile->event_effects[evt].effect_config.effect = EFFECT_OFF;
      profile->event_effects[evt].priority             = 1;
      profile->event_effects[evt].duration_ms          = 0;
      profile->event_effects[evt].action_type          = EVENT_ACTION_APPLY_EFFECT;
      profile->event_effects[evt].profile_id           = -1;
    }
  }

  cJSON_Delete(root);
  ESP_LOGD(TAG_CONFIG, "Profile parsed from JSON: %s", profile->name);
  return true;
}

// Import depuis JSON avec sauvegarde binaire
bool config_manager_import_profile_direct(uint16_t profile_id, const char *json_string) {
  if (json_string == NULL) {
    return false;
  }

  size_t json_len  = strlen(json_string);
  size_t free_heap = esp_get_free_heap_size();
  ESP_LOGI(TAG_CONFIG, "Direct profile import %d: JSON size=%d bytes, heap free=%d bytes", profile_id, json_len, free_heap);

  // Allocate on the heap to avoid stack overflow (~2.2KB)
  config_profile_t *temp_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (temp_profile == NULL) {
    ESP_LOGE(TAG_CONFIG, "Memory allocation error while importing profile %d", profile_id);
    return false;
  }

  // Parse JSON into structure
  if (!config_manager_import_profile_from_json(json_string, temp_profile)) {
    free(temp_profile);
    return false;
  }

  // Save in binary (uses the standard function that handles CRC32, etc.)
  bool success = config_manager_save_profile(profile_id, temp_profile);

  if (success) {
    ESP_LOGI(TAG_CONFIG, "Profile %d imported and saved in binary: %s", profile_id, temp_profile->name);
  } else {
    ESP_LOGE(TAG_CONFIG, "Profile save error for %d", profile_id);
  }

  free(temp_profile);
  return success;
}

bool config_manager_get_effect_for_event(can_event_type_t event, can_event_effect_t *event_effect) {
  if (!active_profile_loaded || event >= CAN_EVENT_MAX || event_effect == NULL) {
    return false;
  }

  memcpy(event_effect, &active_profile.event_effects[event], sizeof(can_event_effect_t));
  return true;
}

uint16_t config_manager_get_led_count(void) {
  return settings_get_u16("led_count", NUM_LEDS);
}

bool config_manager_set_led_count(uint16_t led_count) {
  // Validation
  if (led_count < 1 || led_count > 200) {
    ESP_LOGE(TAG_CONFIG, "Invalid LED count: %d (1-200)", led_count);
    return false;
  }

  esp_err_t err = settings_set_u16("led_count", led_count);
  if (err == ESP_OK) {
    ESP_LOGI(TAG_CONFIG, "LED configuration saved: %d LEDs", led_count);
    return true;
  }
  return false;
}

void config_manager_reapply_default_effect(void) {
  if (!active_profile_loaded) {
    ESP_LOGW(TAG_CONFIG, "No active profile, cannot reapply effect");
    return;
  }

  // Reapply the default effect from the active profile
  led_effects_set_config(&active_profile.default_effect);
  ESP_LOGI(TAG_CONFIG, "Default effect re-applied (audio_reactive=%d)", active_profile.default_effect.audio_reactive);
}

bool config_manager_can_create_profile(void) {
  size_t spiffs_total = 0, spiffs_used = 0;
  esp_err_t err = spiffs_get_stats(&spiffs_total, &spiffs_used);

  if (err != ESP_OK) {
    ESP_LOGW(TAG_CONFIG, "Unable to retrieve SPIFFS stats");
    return false;
  }

  // A binary profile is ~2.5 KB + 8KB SPIFFS overhead
  // Keep a safety margin: ensure at least 12 KB free
  const size_t BYTES_PER_PROFILE = 12 * 1024; // 12 KB per profile (2.5KB data + 8KB overhead + margin)
  size_t spiffs_free             = spiffs_total - spiffs_used;

  bool can_create                = spiffs_free >= BYTES_PER_PROFILE;

  ESP_LOGI(TAG_CONFIG,
           "SPIFFS check: free=%zu bytes, needed=%zu bytes, can_create=%d (usage=%zu%%)",
           spiffs_free,
           BYTES_PER_PROFILE,
           can_create,
           (spiffs_total > 0) ? (spiffs_used * 100 / spiffs_total) : 0);

  return can_create;
}
