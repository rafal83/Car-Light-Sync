/**
 * @file settings_manager.c
 * @brief Centralized system configuration manager (SPIFFS/JSON)
 *
 * Replaces NVS for all system parameters:
 * - Active profile ID
 * - LED hardware config
 * - Wheel control settings
 * - CAN servers autostart
 * - ESP-NOW role/type
 */

#include "settings_manager.h"

#include "cJSON.h"
#include "esp_log.h"
#include "spiffs_storage.h"

#include <string.h>

// RAM cache for parameters
static system_settings_t s_settings;
static bool s_settings_loaded = false;

// Batch mode: defer saves until explicit commit
static bool s_batch_mode = false;
static bool s_batch_dirty = false;

// Default values
static const system_settings_t DEFAULT_SETTINGS = {
    .active_profile_id = -1,
    .led_count = 122, // NUM_LEDS by default
    .wheel_control_enabled = false,
    .wheel_control_speed_limit = 5,
    .gvret_autostart = false,
    .canserver_autostart = false,
    .espnow_role = 0,
    .espnow_type = 0,
};

esp_err_t settings_manager_init(void) {
  // Load parameters from SPIFFS
  esp_err_t err = settings_manager_load(&s_settings);
  if (err != ESP_OK) {
    ESP_LOGW(TAG_SETTINGS, "No parameters file, using default values");
    memcpy(&s_settings, &DEFAULT_SETTINGS, sizeof(system_settings_t));

    // Save default values
    err = settings_manager_save(&s_settings);
    if (err != ESP_OK) {
      ESP_LOGE(TAG_SETTINGS, "Error saving default parameters (err=%s)", esp_err_to_name(err));
      ESP_LOGW(TAG_SETTINGS, "Continuing with in-memory defaults; settings won't persist until SPIFFS has space");
    }
  }

  s_settings_loaded = true;
  ESP_LOGI(TAG_SETTINGS, "System parameters initialized");
  return ESP_OK;
}

esp_err_t settings_manager_load(system_settings_t *settings) {
  if (settings == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Load JSON from SPIFFS
  char *json_buffer = (char *)malloc(2048);
  if (json_buffer == NULL) {
    ESP_LOGE(TAG_SETTINGS, "Buffer allocation error");
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = spiffs_load_json(SETTINGS_FILE_PATH, json_buffer, 2048);
  if (err != ESP_OK) {
    free(json_buffer);
    return err;
  }

  // Parse JSON
  cJSON *root = cJSON_Parse(json_buffer);
  free(json_buffer);

  if (root == NULL) {
    ESP_LOGE(TAG_SETTINGS, "JSON parsing error");
    return ESP_FAIL;
  }

  // Load values (with fallback to defaults)
  cJSON *item;

  item = cJSON_GetObjectItem(root, "active_profile_id");
  settings->active_profile_id = item ? item->valueint : DEFAULT_SETTINGS.active_profile_id;

  item = cJSON_GetObjectItem(root, "led_count");
  settings->led_count = item ? (uint16_t)item->valueint : DEFAULT_SETTINGS.led_count;

  item = cJSON_GetObjectItem(root, "wheel_control_enabled");
  settings->wheel_control_enabled = item ? cJSON_IsTrue(item) : DEFAULT_SETTINGS.wheel_control_enabled;

  item = cJSON_GetObjectItem(root, "wheel_control_speed_limit");
  settings->wheel_control_speed_limit = item ? (uint8_t)item->valueint : DEFAULT_SETTINGS.wheel_control_speed_limit;

  item = cJSON_GetObjectItem(root, "gvret_autostart");
  settings->gvret_autostart = item ? cJSON_IsTrue(item) : DEFAULT_SETTINGS.gvret_autostart;

  item = cJSON_GetObjectItem(root, "canserver_autostart");
  settings->canserver_autostart = item ? cJSON_IsTrue(item) : DEFAULT_SETTINGS.canserver_autostart;

  item = cJSON_GetObjectItem(root, "espnow_role");
  settings->espnow_role = item ? (uint8_t)item->valueint : DEFAULT_SETTINGS.espnow_role;

  item = cJSON_GetObjectItem(root, "espnow_type");
  settings->espnow_type = item ? (uint8_t)item->valueint : DEFAULT_SETTINGS.espnow_type;

  cJSON_Delete(root);

  ESP_LOGI(TAG_SETTINGS, "Parameters loaded: active_profile=%ld, led_count=%u",
           settings->active_profile_id, settings->led_count);
  return ESP_OK;
}

esp_err_t settings_manager_save(const system_settings_t *settings) {
  if (settings == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Create the JSON
  cJSON *root = cJSON_CreateObject();

  cJSON_AddNumberToObject(root, "active_profile_id", settings->active_profile_id);
  cJSON_AddNumberToObject(root, "led_count", settings->led_count);
  cJSON_AddBoolToObject(root, "wheel_control_enabled", settings->wheel_control_enabled);
  cJSON_AddNumberToObject(root, "wheel_control_speed_limit", settings->wheel_control_speed_limit);
  cJSON_AddBoolToObject(root, "gvret_autostart", settings->gvret_autostart);
  cJSON_AddBoolToObject(root, "canserver_autostart", settings->canserver_autostart);
  cJSON_AddNumberToObject(root, "espnow_role", settings->espnow_role);
  cJSON_AddNumberToObject(root, "espnow_type", settings->espnow_type);

  // Convert to string
  char *json_str = cJSON_PrintUnformatted(root);
  esp_err_t err = ESP_FAIL;

  if (json_str) {
    err = spiffs_save_json(SETTINGS_FILE_PATH, json_str);
    free(json_str);
  }

  cJSON_Delete(root);

  if (err == ESP_OK) {
    // Update the cache
    memcpy(&s_settings, settings, sizeof(system_settings_t));
    s_settings_loaded = true;
    ESP_LOGD(TAG_SETTINGS, "Parameters saved");
  } else {
    ESP_LOGE(TAG_SETTINGS, "Error saving parameters");
  }

  return err;
}

// Utility functions for individual get/set
int32_t settings_get_i32(const char *key, int32_t default_value) {
  if (!s_settings_loaded) {
    return default_value;
  }

  if (strcmp(key, "active_profile_id") == 0) {
    return s_settings.active_profile_id;
  }

  return default_value;
}

esp_err_t settings_set_i32(const char *key, int32_t value) {
  if (!s_settings_loaded) {
    return ESP_ERR_INVALID_STATE;
  }

  if (strcmp(key, "active_profile_id") == 0) {
    s_settings.active_profile_id = value;

    // In batch mode, defer save
    if (s_batch_mode) {
      s_batch_dirty = true;
      return ESP_OK;
    }

    return settings_manager_save(&s_settings);
  }

  return ESP_ERR_NOT_FOUND;
}

uint8_t settings_get_u8(const char *key, uint8_t default_value) {
  if (!s_settings_loaded) {
    return default_value;
  }

  if (strcmp(key, "wheel_control_speed_limit") == 0) {
    return s_settings.wheel_control_speed_limit;
  } else if (strcmp(key, "espnow_role") == 0) {
    return s_settings.espnow_role;
  } else if (strcmp(key, "espnow_type") == 0) {
    return s_settings.espnow_type;
  }

  return default_value;
}

esp_err_t settings_set_u8(const char *key, uint8_t value) {
  if (!s_settings_loaded) {
    return ESP_ERR_INVALID_STATE;
  }

  if (strcmp(key, "wheel_control_speed_limit") == 0) {
    s_settings.wheel_control_speed_limit = value;
  } else if (strcmp(key, "espnow_role") == 0) {
    s_settings.espnow_role = value;
  } else if (strcmp(key, "espnow_type") == 0) {
    s_settings.espnow_type = value;
  } else {
    return ESP_ERR_NOT_FOUND;
  }

  // In batch mode, defer save
  if (s_batch_mode) {
    s_batch_dirty = true;
    return ESP_OK;
  }

  return settings_manager_save(&s_settings);
}

uint16_t settings_get_u16(const char *key, uint16_t default_value) {
  if (!s_settings_loaded) {
    return default_value;
  }

  if (strcmp(key, "led_count") == 0) {
    return s_settings.led_count;
  }

  return default_value;
}

esp_err_t settings_set_u16(const char *key, uint16_t value) {
  if (!s_settings_loaded) {
    return ESP_ERR_INVALID_STATE;
  }

  if (strcmp(key, "led_count") == 0) {
    s_settings.led_count = value;

    // In batch mode, defer save
    if (s_batch_mode) {
      s_batch_dirty = true;
      return ESP_OK;
    }

    return settings_manager_save(&s_settings);
  }

  return ESP_ERR_NOT_FOUND;
}

bool settings_get_bool(const char *key, bool default_value) {
  if (!s_settings_loaded) {
    return default_value;
  }

  if (strcmp(key, "wheel_control_enabled") == 0) {
    return s_settings.wheel_control_enabled;
  } else if (strcmp(key, "gvret_autostart") == 0) {
    return s_settings.gvret_autostart;
  } else if (strcmp(key, "canserver_autostart") == 0) {
    return s_settings.canserver_autostart;
  }

  return default_value;
}

esp_err_t settings_set_bool(const char *key, bool value) {
  if (!s_settings_loaded) {
    return ESP_ERR_INVALID_STATE;
  }

  if (strcmp(key, "wheel_control_enabled") == 0) {
    s_settings.wheel_control_enabled = value;
  } else if (strcmp(key, "gvret_autostart") == 0) {
    s_settings.gvret_autostart = value;
  } else if (strcmp(key, "canserver_autostart") == 0) {
    s_settings.canserver_autostart = value;
  } else {
    return ESP_ERR_NOT_FOUND;
  }

  // In batch mode, defer save
  if (s_batch_mode) {
    s_batch_dirty = true;
    return ESP_OK;
  }

  return settings_manager_save(&s_settings);
}

esp_err_t settings_manager_clear(void) {
  // Restore default values
  memcpy(&s_settings, &DEFAULT_SETTINGS, sizeof(system_settings_t));

  esp_err_t err = settings_manager_save(&s_settings);
  if (err == ESP_OK) {
    ESP_LOGI(TAG_SETTINGS, "Parameters reset to default values");
  }

  return err;
}

void settings_begin_batch(void) {
  s_batch_mode = true;
  s_batch_dirty = false;
  ESP_LOGD(TAG_SETTINGS, "Batch mode started (saves deferred)");
}

esp_err_t settings_commit_batch(void) {
  if (!s_batch_mode) {
    ESP_LOGW(TAG_SETTINGS, "Commit called but not in batch mode");
    return ESP_ERR_INVALID_STATE;
  }

  s_batch_mode = false;

  if (!s_batch_dirty) {
    ESP_LOGD(TAG_SETTINGS, "No changes to commit");
    return ESP_OK;
  }

  s_batch_dirty = false;
  esp_err_t err = settings_manager_save(&s_settings);

  if (err == ESP_OK) {
    ESP_LOGI(TAG_SETTINGS, "Batch committed to SPIFFS");
  } else {
    ESP_LOGE(TAG_SETTINGS, "Batch commit failed (err=%s)", esp_err_to_name(err));
  }

  return err;
}

bool settings_is_batch_mode(void) {
  return s_batch_mode;
}
