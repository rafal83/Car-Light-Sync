/**
 * @file config_manager.c
 * @brief Gestionnaire de profils et événements CAN
 *
 * Gère:
 * - Profils d'effets LED (4 profils max) avec sauvegarde NVS
 * - Événements CAN avec priorités et segments
 * - Système multi-pass de rendu: réservation → effet par défaut → événements
 * - Import/export JSON des profils
 * - Activation/désactivation d'événements
 */

#include "config_manager.h"

#include "audio_input.h"
#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_effects.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_manager.h"

#include <string.h>
#include <time.h>

// Nouveau: on ne garde que le profil actif en RAM (~17KB au lieu de ~170KB)
static config_profile_t active_profile;
static int active_profile_id             = -1;
static bool active_profile_loaded        = false;

// Commande par boutons volant (opt-in)
static bool wheel_control_enabled        = false;
static uint8_t wheel_control_speed_limit = 5; // km/h

// Système d'événements multiples
#define MAX_ACTIVE_EVENTS 5
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

// Buffers pour le rendu composé des effets LED
static led_rgb_t composed_buffer[MAX_LED_COUNT];
static led_rgb_t temp_buffer[MAX_LED_COUNT];
static uint8_t priority_buffer[MAX_LED_COUNT];

static void load_wheel_control_settings(void) {
  uint8_t enabled_u8        = nvs_manager_get_u8(NVS_NAMESPACE_SETTINGS, "wheel_ctl", 0);
  uint8_t speed_kph         = nvs_manager_get_u8(NVS_NAMESPACE_SETTINGS, "wheel_spd", wheel_control_speed_limit);

  wheel_control_enabled     = enabled_u8 != 0;
  wheel_control_speed_limit = speed_kph;
  // Clamp
  if (wheel_control_speed_limit > 100) {
    wheel_control_speed_limit = 100;
  }
}

bool config_manager_init(void) {
  // Initialiser le profil actif
  memset(&active_profile, 0, sizeof(active_profile));
  active_profile_loaded = false;

  // Réduire la consommation de stack : utiliser un buffer dynamique pour les profils temporaires
  config_profile_t *temp_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (temp_profile == NULL) {
    ESP_LOGE(TAG_CONFIG, "Erreur alloc profil temp");
    return false;
  }

  // Charger les réglages de contrôle volant (opt-in)
  load_wheel_control_settings();

  // Charger l'ID du profil actif depuis NVS
  int32_t saved_active_id = nvs_manager_get_i32(NVS_NAMESPACE_PROFILES, "active_id", -1);
  if (saved_active_id >= 0) {
    active_profile_id = saved_active_id;
  }

  // Essayer de charger le profil actif
  bool profile_exists = false;
  if (active_profile_id >= 0) {
    profile_exists = config_manager_load_profile(active_profile_id, &active_profile);
    if (profile_exists) {
      active_profile_loaded = true;
      ESP_LOGI(TAG_CONFIG, "Profil actif %d chargé: %s", active_profile_id, active_profile.name);
      led_effects_set_config(&active_profile.default_effect);
      effect_override_active = false;
    }
  }

  // Si aucun profil n'existe, créer un profil par défaut
  if (!profile_exists) {
    ESP_LOGI(TAG_CONFIG, "Aucun profil trouvé, création du profil par défaut + profil Eteint");
    config_manager_create_default_profile(&active_profile, "Default");
    config_manager_save_profile(1, &active_profile);
    active_profile_id     = 1;
    active_profile_loaded = true;

    config_manager_create_off_profile(temp_profile, "Eteint");
    config_manager_save_profile(0, temp_profile);

    // Sauvegarder l'ID actif
    nvs_manager_set_i32(NVS_NAMESPACE_PROFILES, "active_id", active_profile_id);

    led_effects_set_config(&active_profile.default_effect);
    effect_override_active = false;
  }

  // S'assurer qu'un profil "Eteint" existe (ID 0 réservé)
  if (!config_manager_load_profile(0, temp_profile)) {
    config_manager_create_off_profile(temp_profile, "Eteint");
    config_manager_save_profile(0, temp_profile);
  }

  free(temp_profile);

  return true;
}

// Cette fonction n'est plus nécessaire - on charge uniquement le profil actif à la demande

bool config_manager_save_profile(uint16_t profile_id, const config_profile_t *profile) {
  if (profile == NULL) {
    return false;
  }

  char key[16];
  snprintf(key, sizeof(key), "profile_%d", profile_id);

  // Allouer dynamiquement pour éviter le stack overflow
  config_profile_t *profile_copy = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (profile_copy == NULL) {
    ESP_LOGE(TAG_CONFIG, "Erreur allocation mémoire");
    return false;
  }

  // Mettre à jour le timestamp
  memcpy(profile_copy, profile, sizeof(config_profile_t));
  profile_copy->modified_timestamp = (uint32_t)time(NULL);

  ESP_LOGI(TAG_CONFIG, "Saving profile %d: size=%d bytes (NVS limit=1984)", profile_id, sizeof(config_profile_t));

  esp_err_t err = nvs_manager_set_blob(NVS_NAMESPACE_PROFILES, key, profile_copy, sizeof(config_profile_t));
  if (err != ESP_OK) {
    ESP_LOGE(TAG_CONFIG, "Erreur sauvegarde profil: %s (size=%d bytes)", esp_err_to_name(err), sizeof(config_profile_t));
    free(profile_copy);
    return false;
  }

  // Mettre à jour le profil actif en RAM si c'est celui qu'on vient de sauvegarder
  if (profile_id == active_profile_id) {
    memcpy(&active_profile, profile_copy, sizeof(config_profile_t));
    active_profile_loaded = true;
  }

  free(profile_copy);

  ESP_LOGI(TAG_CONFIG, "Profil %d sauvegardé: %s", profile_id, profile->name);
  return true;
}

bool config_manager_load_profile(uint16_t profile_id, config_profile_t *profile) {
  if (profile == NULL) {
    return false;
  }

  char key[16];
  snprintf(key, sizeof(key), "profile_%d", profile_id);

  // Vérifier d'abord la taille du blob
  size_t required_size = 0;
  esp_err_t err        = nvs_manager_get_blob(NVS_NAMESPACE_PROFILES, key, NULL, &required_size);
  if (err != ESP_OK) {
    return false;
  }

  // Vérifier que la taille correspond à la structure actuelle
  if (required_size != sizeof(config_profile_t)) {
    ESP_LOGW(TAG_CONFIG, "Profile %d a une taille incompatible (%d vs %d) - ignoré", profile_id, required_size, sizeof(config_profile_t));
    return false;
  }

  if (sizeof(config_profile_t) > 1984) {
    ESP_LOGE(TAG_CONFIG, "CRITICAL: config_profile_t size (%d bytes) exceeds NVS blob limit (1984 bytes)!", sizeof(config_profile_t));
  }

  // Charger le profil
  required_size = sizeof(config_profile_t);
  err           = nvs_manager_get_blob(NVS_NAMESPACE_PROFILES, key, profile, &required_size);

  return (err == ESP_OK);
}

bool config_manager_delete_profile(uint16_t profile_id) {
  // Vérifier que le profil existe
  char key[16];
  snprintf(key, sizeof(key), "profile_%d", profile_id);
  size_t required_size = 0;
  esp_err_t err        = nvs_manager_get_blob(NVS_NAMESPACE_PROFILES, key, NULL, &required_size);
  if (err != ESP_OK) {
    ESP_LOGW(TAG_CONFIG, "Profil %d n'existe pas", profile_id);
    return false;
  }

  // Vérifier que le profil n'est pas utilisé dans des événements d'autres profils
  // Scanner dynamiquement tous les profils existants
  config_profile_t *temp_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (temp_profile == NULL) {
    ESP_LOGE(TAG_CONFIG, "Erreur allocation mémoire");
    return false;
  }

  // Scanner tous les IDs possibles
  for (int p = 0; p < MAX_PROFILE_SCAN_LIMIT; p++) {
    if (p == profile_id)
      continue; // Ignorer le profil qu'on veut supprimer

    snprintf(key, sizeof(key), "profile_%d", p);
    required_size = sizeof(config_profile_t);
    err           = nvs_manager_get_blob(NVS_NAMESPACE_PROFILES, key, temp_profile, &required_size);

    if (err == ESP_OK) {
      // Vérifier si ce profil référence le profil à supprimer
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

  // Supprimer le profil de la NVS
  snprintf(key, sizeof(key), "profile_%d", profile_id);
  err = nvs_manager_erase_key(NVS_NAMESPACE_PROFILES, key);

  if (err != ESP_OK) {
    return false;
  }

  // Compresser les IDs : décaler tous les profils suivants
  config_profile_t *shift_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (shift_profile == NULL) {
    ESP_LOGE(TAG_CONFIG, "Erreur allocation mémoire pour compression");
    return false;
  }

  bool needs_compression = false;
  // Scanner dynamiquement les profils suivants
  for (int i = profile_id + 1; i < MAX_PROFILE_SCAN_LIMIT; i++) {
    snprintf(key, sizeof(key), "profile_%d", i);
    required_size = sizeof(config_profile_t);
    if (nvs_manager_get_blob(NVS_NAMESPACE_PROFILES, key, NULL, &required_size) == ESP_OK) {
      needs_compression = true;
      break;
    }
  }

  if (needs_compression) {
    ESP_LOGI(TAG_CONFIG, "Compression des IDs de profils après suppression du profil %d", profile_id);

    // Scanner et décaler les profils
    for (int i = profile_id; i < MAX_PROFILE_SCAN_LIMIT - 1; i++) {
      char next_key[16];
      snprintf(next_key, sizeof(next_key), "profile_%d", i + 1);

      required_size      = sizeof(config_profile_t);
      esp_err_t read_err = nvs_manager_get_blob(NVS_NAMESPACE_PROFILES, next_key, shift_profile, &required_size);

      if (read_err == ESP_OK) {
        // Sauvegarder au nouvel emplacement
        char current_key[16];
        snprintf(current_key, sizeof(current_key), "profile_%d", i);
        nvs_manager_set_blob(NVS_NAMESPACE_PROFILES, current_key, shift_profile, sizeof(config_profile_t));

        // Effacer l'ancien emplacement
        nvs_manager_erase_key(NVS_NAMESPACE_PROFILES, next_key);

        ESP_LOGI(TAG_CONFIG, "Profil %d déplacé vers ID %d", i + 1, i);
      } else {
        break;
      }
    }

    // Mettre à jour l'ID du profil actif
    if (active_profile_id > profile_id) {
      active_profile_id--;
      memcpy(&active_profile, shift_profile, sizeof(config_profile_t));
      nvs_manager_set_i32(NVS_NAMESPACE_PROFILES, "active_id", active_profile_id);
      ESP_LOGI(TAG_CONFIG, "ID du profil actif mis à jour: %d", active_profile_id);
    } else if (active_profile_id == profile_id) {
      // Le profil actif a été supprimé, chercher le premier profil disponible
      active_profile_id     = -1;
      active_profile_loaded = false;

      // Scanner dynamiquement tous les profils
      for (int i = 0; i < MAX_PROFILE_SCAN_LIMIT; i++) {
        snprintf(key, sizeof(key), "profile_%d", i);
        required_size = sizeof(config_profile_t);
        if (nvs_manager_get_blob(NVS_NAMESPACE_PROFILES, key, &active_profile, &required_size) == ESP_OK) {
          active_profile_id     = i;
          active_profile_loaded = true;
          nvs_manager_set_i32(NVS_NAMESPACE_PROFILES, "active_id", i);
          ESP_LOGI(TAG_CONFIG, "Profil actif supprimé, activation du profil %d", i);
          break;
        }
      }

      if (active_profile_id == -1) {
        nvs_manager_erase_key(NVS_NAMESPACE_PROFILES, "active_id");
        memset(&active_profile, 0, sizeof(active_profile));
        ESP_LOGI(TAG_CONFIG, "Aucun profil disponible");
      }
    }
  } else {
    // Pas de compression nécessaire
    if (active_profile_id == profile_id) {
      // Chercher le premier profil disponible
      active_profile_id     = -1;
      active_profile_loaded = false;

      for (int i = 0; i < MAX_PROFILE_SCAN_LIMIT; i++) {
        snprintf(key, sizeof(key), "profile_%d", i);
        required_size = sizeof(config_profile_t);
        if (nvs_manager_get_blob(NVS_NAMESPACE_PROFILES, key, &active_profile, &required_size) == ESP_OK) {
          active_profile_id     = i;
          active_profile_loaded = true;
          nvs_manager_set_i32(NVS_NAMESPACE_PROFILES, "active_id", i);
          ESP_LOGI(TAG_CONFIG, "Activation automatique du profil %d", i);
          break;
        }
      }

      if (active_profile_id == -1) {
        nvs_manager_erase_key(NVS_NAMESPACE_PROFILES, "active_id");
        memset(&active_profile, 0, sizeof(active_profile));
        ESP_LOGI(TAG_CONFIG, "Aucun profil disponible");
      }
    }
  }

  free(shift_profile);
  ESP_LOGI(TAG_CONFIG, "Profil %d supprimé avec succès", profile_id);
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

  // Allouer dynamiquement pour éviter le stack overflow
  config_profile_t *profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (profile == NULL) {
    ESP_LOGE(TAG_CONFIG, "Erreur allocation mémoire pour renommage");
    return false;
  }

  if (!config_manager_load_profile(profile_id, profile)) {
    ESP_LOGW(TAG_CONFIG, "Profil %d introuvable pour renommage", profile_id);
    free(profile);
    return false;
  }

  bool was_active = profile->active;

  memset(profile->name, 0, PROFILE_NAME_MAX_LEN);
  strncpy(profile->name, new_name, PROFILE_NAME_MAX_LEN - 1);

  bool success = config_manager_save_profile(profile_id, profile);
  free(profile);

  if (success && was_active) {
    // Réappliquer pour conserver l'état actif et l'effet
    config_manager_activate_profile(profile_id);
  }

  return success;
}

bool config_manager_activate_profile(uint16_t profile_id) {
  // Charger le nouveau profil depuis NVS
  if (!config_manager_load_profile(profile_id, &active_profile)) {
    ESP_LOGE(TAG_CONFIG, "Profil %d inexistant", profile_id);
    return false;
  }

  // Mettre à jour l'ID actif
  active_profile_id     = profile_id;
  active_profile_loaded = true;

  // Sauvegarder l'ID actif dans NVS
  nvs_manager_set_i32(NVS_NAMESPACE_PROFILES, "active_id", profile_id);

  // Arrêter tous les événements actifs avant d'appliquer le nouvel effet
  config_manager_stop_all_events();

  // Appliquer l'effet par défaut
  led_effects_set_config(&active_profile.default_effect);
  effect_override_active = false;

  ESP_LOGI(TAG_CONFIG, "Profil %d activé: %s", profile_id, active_profile.name);
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
    config_profile_t temp;
    if (config_manager_load_profile((uint16_t)candidate, &temp)) {
      return config_manager_activate_profile((uint16_t)candidate);
    }
  }
  return false;
}

bool config_manager_get_wheel_control_enabled(void) {
  return wheel_control_enabled;
}

bool config_manager_set_wheel_control_enabled(bool enabled) {
  wheel_control_enabled = enabled;
  esp_err_t err         = nvs_manager_set_bool(NVS_NAMESPACE_SETTINGS, "wheel_ctl", enabled);
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
  esp_err_t err             = nvs_manager_set_u8(NVS_NAMESPACE_SETTINGS, "wheel_spd", wheel_control_speed_limit);
  return err == ESP_OK;
}

int config_manager_list_profiles(config_profile_t *profile_list, int max_profiles) {
  if (profile_list == NULL || max_profiles <= 0) {
    return 0;
  }

  int count = 0;
  char key[16];

  // Scanner dynamiquement tous les profils
  for (int i = 0; i < MAX_PROFILE_SCAN_LIMIT && count < max_profiles; i++) {
    snprintf(key, sizeof(key), "profile_%d", i);

    size_t required_size = sizeof(config_profile_t);
    esp_err_t err        = nvs_manager_get_blob(NVS_NAMESPACE_PROFILES, key, &profile_list[count], &required_size);
    if (err == ESP_OK && required_size == sizeof(config_profile_t)) {
      count++;
    }
  }
  return count;
}

// Fichier JSON embarqué - default.json
extern const uint8_t default_json_start[] asm("_binary_default_json_start");
extern const uint8_t default_json_end[] asm("_binary_default_json_end");

void config_manager_create_default_profile(config_profile_t *profile, const char *name) {
  bool success        = false;

  // 1. Charger le fichier JSON embarqué (default.json)
  size_t json_size    = default_json_end - default_json_start;

  // Allouer un buffer pour le JSON (avec null terminator)
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

  // 2. Si échec, fallback minimal
  if (!success) {
    ESP_LOGE(TAG_CONFIG, "Failed to import embedded preset, using minimal fallback");
    memset(profile, 0, sizeof(config_profile_t));
    strncpy(profile->name, name, PROFILE_NAME_MAX_LEN - 1);
    profile->default_effect.effect     = EFFECT_SOLID;
    profile->default_effect.brightness = 20;
    profile->default_effect.speed      = 1;
    profile->default_effect.color1     = 0xFFFFFF;
    profile->active                    = false;
    profile->created_timestamp         = (uint32_t)time(NULL);
    profile->modified_timestamp        = profile->created_timestamp;
    return;
  }

  // Optionnel: Override le nom si fourni
  if (name != NULL && strlen(name) > 0) {
    strncpy(profile->name, name, PROFILE_NAME_MAX_LEN - 1);
  }

  // Note: Les événements manquants sont déjà complétés par la fonction d'import
  // avec des valeurs disabled par défaut (voir config_manager_import_profile_from_json)

  // Paramètres de timestamps
  profile->active             = false;
  profile->created_timestamp  = (uint32_t)time(NULL);
  profile->modified_timestamp = profile->created_timestamp;
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

  profile->dynamic_brightness_enabled = false;
  profile->dynamic_brightness_rate    = 0;
  profile->active                     = false;
  profile->created_timestamp          = (uint32_t)time(NULL);
  profile->modified_timestamp         = profile->created_timestamp;
}

bool config_manager_set_event_effect(uint16_t profile_id, can_event_type_t event, const effect_config_t *effect_config, uint16_t duration_ms, uint8_t priority) {
  if (event >= CAN_EVENT_MAX || effect_config == NULL) {
    return false;
  }

  // Si c'est le profil actif, modifier directement
  if (profile_id == active_profile_id && active_profile_loaded) {
    active_profile.event_effects[event].event = event;
    memcpy(&active_profile.event_effects[event].effect_config, effect_config, sizeof(effect_config_t));
    active_profile.event_effects[event].duration_ms = duration_ms;
    active_profile.event_effects[event].priority    = priority;
    active_profile.event_effects[event].enabled     = true;
    return true;
  }

  // Sinon, charger le profil, le modifier et le sauvegarder
  config_profile_t *temp_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (temp_profile == NULL) {
    ESP_LOGE(TAG_CONFIG, "Erreur allocation mémoire");
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

  // Si c'est le profil actif, modifier directement
  if (profile_id == active_profile_id && active_profile_loaded) {
    active_profile.event_effects[event].enabled = enabled;
    return true;
  }

  // Sinon, charger le profil, le modifier et le sauvegarder
  config_profile_t *temp_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (temp_profile == NULL) {
    ESP_LOGE(TAG_CONFIG, "Erreur allocation mémoire");
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

  // Gérer le changement de profil si configuré (indépendant du flag enabled)
  if (event_effect->action_type != EVENT_ACTION_APPLY_EFFECT) {
    // EVENT_ACTION_SWITCH_PROFILE
    if (event_effect->profile_id >= 0) {
      ESP_LOGI(TAG_CONFIG, "Event %d: Switching to profile %d", event, event_effect->profile_id);
      config_manager_activate_profile(event_effect->profile_id);

      return false;
    }
  }

  // Si l'effet est configure et active, l'utiliser
  if (event_effect->enabled) {
    memcpy(&effect_to_apply, &event_effect->effect_config, sizeof(effect_config_t));
    duration_ms = event_effect->duration_ms;
    priority    = event_effect->priority;
    // if(effect_to_apply.segment_length == 0) {
    //   effect_to_apply.segment_length = total_leds;
    // }

    ESP_LOGI(TAG_CONFIG,
             "Event %s: segment_start=%d, segment_length=%d, reverse=%d",
             config_manager_enum_to_id(event),
             effect_to_apply.segment_start,
             effect_to_apply.segment_length,
             effect_to_apply.reverse);

    // Trouver un slot libre pour l'événement
    int free_slot     = -1;
    int existing_slot = -1;

    // Chercher si l'événement est déjà actif ou trouver un slot libre
    for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
      if (active_events[i].active && active_events[i].event == event) {
        existing_slot = i;
        break;
      }
      if (!active_events[i].active && free_slot == -1) {
        free_slot = i;
      }
    }

    // Si l'événement existe déjà, le mettre à jour
    int slot = (existing_slot >= 0) ? existing_slot : free_slot;

    if (slot < 0) {
      // Pas de slot libre, vérifier si on peut écraser un événement de priorité
      // inférieure MAIS seulement si c'est la même zone ou si le nouvel
      // événement est FULL
      int lowest_priority_slot = -1;
      uint8_t lowest_priority  = 255;

      for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
        if (active_events[i].active && active_events[i].priority < priority && active_events[i].priority < lowest_priority) {
          // Nouvel événement a priorité supérieure, on peut écraser
          lowest_priority      = active_events[i].priority;
          lowest_priority_slot = i;
        }
      }

      if (lowest_priority_slot >= 0) {
        slot = lowest_priority_slot;
        ESP_LOGI(TAG_CONFIG, "Écrasement événement priorité %d par priorité %d", lowest_priority, priority);
      } else {
        ESP_LOGW(TAG_CONFIG, "Événement '%s' ignoré (pas de slot disponible)", config_manager_enum_to_id(event));
        return false;
      }
    }

    // Enregistrer l'événement actif
    active_events[slot].event         = event;
    active_events[slot].effect_config = effect_to_apply;
    active_events[slot].start_time    = xTaskGetTickCount();
    active_events[slot].duration_ms   = duration_ms;
    active_events[slot].priority      = priority;
    active_events[slot].active        = true;

    // NE PAS appliquer immédiatement - laisser config_manager_update() le faire
    // selon la logique de priorité par zone pour éviter le glitch visuel

    if (duration_ms > 0) {
      ESP_LOGI(TAG_CONFIG, "Effet '%s' activé pour %dms (priorité %d)", config_manager_enum_to_id(event), duration_ms, priority);
    } else {
      ESP_LOGI(TAG_CONFIG, "Effet '%s' activé (permanent, priorité %d)", config_manager_enum_to_id(event), priority);
    }

    return true;
  } else {
    // ESP_LOGW(TAG_CONFIG, "Effet par défaut ignoré pour '%s'",
    //         config_manager_enum_to_id(event));
  }

  return false;
}

void config_manager_stop_event(can_event_type_t event) {
  // Vérifier que l'événement est valide
  if (event >= CAN_EVENT_MAX) {
    return;
  }

  // Désactiver tous les slots qui correspondent à cet événement
  for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
    if (active_events[i].active && active_events[i].event == event) {
      ESP_LOGI(TAG_CONFIG, "Arrêt de l'événement '%s'", config_manager_enum_to_id(event));
      active_events[i].active = false;
    }
  }
}

void config_manager_stop_all_events(void) {
  for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
    if (active_events[i].active) {
      ESP_LOGI(TAG_CONFIG, "Arret global de l'evenement '%s'", config_manager_enum_to_id(active_events[i].event));
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

  // Vérifier et expirer les événements actifs
  for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
    if (!active_events[i].active)
      continue;

    // Vérifier si l'événement est expiré (si durée > 0)
    if (active_events[i].duration_ms > 0) {
      uint32_t elapsed = now - active_events[i].start_time;
      if (elapsed >= pdMS_TO_TICKS(active_events[i].duration_ms)) {
        ESP_LOGI(TAG_CONFIG, "Événements '%s' terminé", config_manager_enum_to_id(active_events[i].event));
        active_events[i].active = false;
        continue;
      }
    }

    any_active = true;
  }

  // Optimisation: si aucun événement actif, laisser l'effet par défaut s'afficher
  if (!any_active) {
    effect_override_active = false;
    return;
  }

  // Initialiser les buffers
  memset(priority_buffer, 0, total_leds * sizeof(uint8_t));
  memset(composed_buffer, 0, total_leds * sizeof(led_rgb_t));

  uint32_t frame_counter = led_effects_get_frame_counter();
  bool needs_fft         = false;

  // Première passe : marquer les segments réservés par les événements actifs
  int reserved_count     = 0;
  for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
    if (!active_events[i].active)
      continue;

    uint16_t start  = active_events[i].effect_config.segment_start;
    uint16_t length = active_events[i].effect_config.segment_length;

    // Normaliser length == 0 → full strip
    if (length == 0) {
      length = total_leds;
    }

    // Vérifier validité du segment
    if (start >= total_leds) {
      ESP_LOGW(TAG_CONFIG, "Event skipped: start >= total_leds (start=%d, total=%d)", start, total_leds);
      continue;
    }
    if ((uint32_t)start + length > total_leds) {
      ESP_LOGW(TAG_CONFIG, "Event length trimmed: %d -> %d", length, total_leds - start);
      length = total_leds - start;
    }

    // Vérifier si tout le segment peut être réservé (priorité par segment)
    bool can_reserve_segment = true;
    for (uint16_t idx = start; idx < (uint16_t)(start + length); idx++) {
      if (active_events[i].priority < priority_buffer[idx]) {
        can_reserve_segment = false;
        break;
      }
    }

    // Réserver tout le segment en marquant les priorités
    if (can_reserve_segment) {
      for (uint16_t idx = start; idx < (uint16_t)(start + length); idx++) {
        priority_buffer[idx] = active_events[i].priority;
      }
      reserved_count++;
    }
  }

  // Deuxième passe : rendre l'effet par défaut uniquement sur les zones non réservées
  if (active_profile_loaded) {
    effect_config_t base    = active_profile.default_effect;

    // Utiliser le segment configuré dans le profil (ou toute la strip si non configuré)
    uint16_t default_start  = base.segment_start;
    uint16_t default_length = base.segment_length;

    // Normaliser le segment
    led_effects_normalize_segment(&default_start, &default_length, total_leds);

    // Modulation par accel_pedal_pos si activé
    if (base.accel_pedal_pos_enabled) {
      default_length = led_effects_apply_accel_modulation(default_length, led_effects_get_accel_pedal_pos(), base.accel_pedal_offset);
    }

    memset(temp_buffer, 0, total_leds * sizeof(led_rgb_t));
    led_effects_render_to_buffer(&base, default_start, default_length, frame_counter, temp_buffer);

    // Copier uniquement les LEDs non réservées (priority == 0)
    for (uint16_t idx = 0; idx < total_leds; idx++) {
      if (priority_buffer[idx] == 0) {
        composed_buffer[idx] = temp_buffer[idx];
      }
    }

    needs_fft |= led_effects_requires_fft(active_profile.default_effect.effect);
  }

  // Troisième passe : rendre les événements sur leurs segments réservés
  for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
    if (!active_events[i].active)
      continue;

    uint16_t start  = active_events[i].effect_config.segment_start;
    uint16_t length = active_events[i].effect_config.segment_length;

    // Normaliser length == 0 → full strip
    if (length == 0) {
      length = total_leds;
    }

    // Vérifier si ce segment a bien été réservé pour cet événement
    bool segment_reserved = true;
    for (uint16_t idx = start; idx < (uint16_t)(start + length); idx++) {
      if (priority_buffer[idx] != active_events[i].priority) {
        segment_reserved = false;
        break;
      }
    }

    // Rendre l'effet sur le segment réservé
    if (segment_reserved) {
      memset(temp_buffer, 0, total_leds * sizeof(led_rgb_t));

      led_effects_render_to_buffer(&active_events[i].effect_config, start, length, frame_counter, temp_buffer);

      // Copier tout le segment
      for (uint16_t idx = start; idx < (uint16_t)(start + length); idx++) {
        composed_buffer[idx] = temp_buffer[idx];
      }
    }

    if (led_effects_requires_fft(active_events[i].effect_config.effect)) {
      needs_fft = true;
    }
  }

  audio_input_set_fft_enabled(needs_fft);

  led_effects_show_buffer(composed_buffer);

  effect_override_active = any_active;
}

bool config_manager_has_active_events(void) {
  return effect_override_active;
}

// Table de correspondance enum -> ID alphanumérique
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
  case CAN_EVENT_DOOR_OPEN:
    return EVENT_ID_DOOR_OPEN;
  case CAN_EVENT_DOOR_CLOSE:
    return EVENT_ID_DOOR_CLOSE;
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
  case CAN_EVENT_AUTOPILOT_ALERT_LV3:
    return EVENT_ID_AUTOPILOT_ALERT_LV3;
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

// Table de correspondance ID alphanumérique -> enum
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
  if (strcmp(id, EVENT_ID_DOOR_OPEN) == 0)
    return CAN_EVENT_DOOR_OPEN;
  if (strcmp(id, EVENT_ID_DOOR_CLOSE) == 0)
    return CAN_EVENT_DOOR_CLOSE;
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
  if (strcmp(id, EVENT_ID_AUTOPILOT_ALERT_LV3) == 0)
    return CAN_EVENT_AUTOPILOT_ALERT_LV3;
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

  ESP_LOGW(TAG_CONFIG, "ID d'événement inconnu: %s", id);
  return CAN_EVENT_NONE;
}

// Vérifie si un événement peut déclencher un changement de profil
bool config_manager_event_can_switch_profile(can_event_type_t event) {
  switch (event) {
  case CAN_EVENT_DOOR_OPEN:
  case CAN_EVENT_DOOR_CLOSE:
  case CAN_EVENT_CHARGING:
  case CAN_EVENT_CHARGE_COMPLETE:
  case CAN_EVENT_SPEED_THRESHOLD:
  case CAN_EVENT_LOCKED:
  case CAN_EVENT_UNLOCKED:
  case CAN_EVENT_AUTOPILOT_ENGAGED:
  case CAN_EVENT_AUTOPILOT_DISENGAGED:
  case CAN_EVENT_AUTOPILOT_ALERT_LV1:
  case CAN_EVENT_AUTOPILOT_ALERT_LV2:
  case CAN_EVENT_AUTOPILOT_ALERT_LV3:
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

// Réinitialisation usine complète
bool config_manager_factory_reset(void) {
  ESP_LOGI(TAG_CONFIG, "Factory reset: Erasing all profiles and settings...");

  esp_err_t err = nvs_manager_erase_namespace(NVS_NAMESPACE_PROFILES);
  if (err != ESP_OK) {
    ESP_LOGE(TAG_CONFIG, "Failed to erase profiles namespace");
    return false;
  }

  // Effacer la configuration matérielle LED
  nvs_manager_erase_namespace(NVS_NAMESPACE_LED_HW);
  wheel_control_enabled     = false;
  wheel_control_speed_limit = 5;

  // Effacer les réglages globaux (wheel control, etc.)
  nvs_manager_erase_namespace(NVS_NAMESPACE_SETTINGS);

  nvs_manager_erase_namespace(NVS_NAMESPACE_AUDIO);
  nvs_manager_erase_namespace(NVS_NAMESPACE_CAN_SERVERS);

  // Réinitialiser le profil actif en RAM
  memset(&active_profile, 0, sizeof(active_profile));
  active_profile_id                 = -1;
  active_profile_loaded             = false;

  // Créer les profils de base sans utiliser la stack (éviter overflow httpd)
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

// Conversion 0-255 vers pourcentage 1-100
static uint8_t value_to_percent(uint8_t value) {
  if (value == 0) {
    return 1;
  }
  uint8_t percent = (value * 100 + 127) / 255; // Arrondi au plus proche
  return (percent < 1) ? 1 : (percent > 100) ? 100 : percent;
}

// Conversion pourcentage 1-100 vers 0-255
static uint8_t percent_to_value(uint8_t percent) {
  if (percent < 1)
    percent = 1;
  if (percent > 100)
    percent = 100;
  return (percent * 255 + 50) / 100; // Arrondi au plus proche
}

bool config_manager_export_profile(uint16_t profile_id, char *json_buffer, size_t buffer_size) {
  if (json_buffer == NULL) {
    return false;
  }

  // Charger le profil (ou utiliser le profil actif si c'est celui-là)
  config_profile_t *profile      = NULL;
  config_profile_t *temp_profile = NULL;

  if (profile_id == active_profile_id && active_profile_loaded) {
    profile = &active_profile;
  } else {
    temp_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
    if (temp_profile == NULL) {
      ESP_LOGE(TAG_CONFIG, "Erreur allocation mémoire");
      return false;
    }

    if (!config_manager_load_profile(profile_id, temp_profile)) {
      ESP_LOGW(TAG_CONFIG, "Profil %d inexistant, impossible d'exporter", profile_id);
      free(temp_profile);
      return false;
    }

    profile = temp_profile;
  }

  cJSON *root = cJSON_CreateObject();

  // Métadonnées
  cJSON_AddStringToObject(root, "name", profile->name);
  cJSON_AddNumberToObject(root, "version", 1);
  cJSON_AddNumberToObject(root, "created_timestamp", profile->created_timestamp);
  cJSON_AddNumberToObject(root, "modified_timestamp", profile->modified_timestamp);

  // Effet par défaut - Convertir en pourcentage pour l'export
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

  // Paramètres de luminosité dynamique
  cJSON_AddBoolToObject(root, "dynamic_brightness_enabled", profile->dynamic_brightness_enabled);
  cJSON_AddNumberToObject(root, "dynamic_brightness_rate", profile->dynamic_brightness_rate);

  // Événements CAN
  cJSON *events = cJSON_CreateArray();
  for (int i = 0; i < CAN_EVENT_MAX; i++) {
    if (profile->event_effects[i].enabled) {
      cJSON *event         = cJSON_CreateObject();
      // Utiliser l'ID alphanumérique au lieu de l'enum pour compatibilité
      const char *event_id = config_manager_enum_to_id(profile->event_effects[i].event);
      cJSON_AddStringToObject(event, "event_id", event_id);
      cJSON_AddBoolToObject(event, "enabled", profile->event_effects[i].enabled);
      cJSON_AddNumberToObject(event, "priority", value_to_percent(profile->event_effects[i].priority));
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

  // Convertir en chaîne JSON
  char *json_str = cJSON_PrintUnformatted(root);
  bool success   = false;

  if (json_str) {
    size_t len = strlen(json_str);
    if (len < buffer_size) {
      strcpy(json_buffer, json_str);
      ESP_LOGI(TAG_CONFIG, "Profil %d exporté avec succès (%d octets)", profile_id, len);
      success = true;
    } else {
      ESP_LOGE(TAG_CONFIG, "Buffer trop petit: %d nécessaires, %d disponibles", len + 1, buffer_size);
    }
    free(json_str);
  }

  cJSON_Delete(root);

  // Libérer le profil temporaire si on l'a alloué
  if (temp_profile != NULL) {
    free(temp_profile);
  }

  return success;
}

bool config_manager_import_profile_from_json(const char *json_string, config_profile_t *profile) {
  if (json_string == NULL || profile == NULL) {
    return false;
  }

  cJSON *root = cJSON_Parse(json_string);
  if (!root) {
    ESP_LOGE(TAG_CONFIG, "Erreur parsing JSON");
    return false;
  }

  memset(profile, 0, sizeof(config_profile_t));

  // Métadonnées
  const cJSON *name = cJSON_GetObjectItem(root, "name");
  if (name && cJSON_IsString(name)) {
    strncpy(profile->name, name->valuestring, PROFILE_NAME_MAX_LEN - 1);
  } else {
    ESP_LOGE(TAG_CONFIG, "Champ 'name' manquant ou invalide");
    cJSON_Delete(root);
    return false;
  }

  const cJSON *created = cJSON_GetObjectItem(root, "created_timestamp");
  if (created && cJSON_IsNumber(created)) {
    profile->created_timestamp = created->valueint;
  }

  const cJSON *modified = cJSON_GetObjectItem(root, "modified_timestamp");
  if (modified && cJSON_IsNumber(modified)) {
    profile->modified_timestamp = modified->valueint;
  }

  if (profile->created_timestamp == 0) {
    profile->created_timestamp = (uint32_t)time(NULL);
  }
  if (profile->modified_timestamp == 0) {
    profile->modified_timestamp = profile->created_timestamp;
  }
  profile->active             = false;

  // Effet par défaut
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

  // Paramètres de luminosité dynamique
  const cJSON *dyn_bright_enabled = cJSON_GetObjectItem(root, "dynamic_brightness_enabled");
  if (dyn_bright_enabled && cJSON_IsBool(dyn_bright_enabled)) {
    profile->dynamic_brightness_enabled = cJSON_IsTrue(dyn_bright_enabled);
  } else {
    profile->dynamic_brightness_enabled = false; // Défaut
  }

  const cJSON *dyn_bright_rate = cJSON_GetObjectItem(root, "dynamic_brightness_rate");
  if (dyn_bright_rate && cJSON_IsNumber(dyn_bright_rate)) {
    profile->dynamic_brightness_rate = dyn_bright_rate->valueint;
  } else {
    profile->dynamic_brightness_rate = 50; // Défaut
  }

  // Événements CAN
  cJSON *events = cJSON_GetObjectItem(root, "event_effects");
  if (events && cJSON_IsArray(events)) {
    const cJSON *event = NULL;
    cJSON_ArrayForEach(event, events) {
      // Nouveau format : utiliser event_id (string alphanumérique)
      const cJSON *event_id_obj = cJSON_GetObjectItem(event, "event_id");
      int evt                   = -1;

      if (event_id_obj && cJSON_IsString(event_id_obj)) {
        // Format nouveau avec ID alphanumérique
        evt = config_manager_id_to_enum(event_id_obj->valuestring);
      } else {
        // Format ancien avec numéro d'enum (rétrocompatibilité)
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
        profile->event_effects[evt].priority = percent_to_value(priority->valueint);
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

  // Compléter tous les événements manquants avec des valeurs par défaut (désactivés)
  // Cela permet de gérer les nouveaux événements ajoutés après la création du preset
  for (int evt = CAN_EVENT_NONE; evt < CAN_EVENT_MAX; evt++) {
    // Si l'événement n'a pas été initialisé (effect_config.effect == 0 et enabled == false)
    // ET que ce n'est pas NONE, on le complète avec des valeurs par défaut
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
  ESP_LOGI(TAG_CONFIG, "Profil importé avec succès: %s", profile->name);
  return true;
}

bool config_manager_import_profile(uint16_t profile_id, const char *json_string) {
  if (json_string == NULL) {
    return false;
  }

  // Allouer dynamiquement pour éviter stack overflow
  config_profile_t *imported_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (imported_profile == NULL) {
    ESP_LOGE(TAG_CONFIG, "Erreur allocation mémoire");
    return false;
  }

  // Utiliser la fonction générique pour parser le JSON
  if (!config_manager_import_profile_from_json(json_string, imported_profile)) {
    free(imported_profile);
    return false;
  }

  // Sauvegarder le profil importé
  bool success = config_manager_save_profile(profile_id, imported_profile);
  if (success) {
    ESP_LOGI(TAG_CONFIG, "Profil %d importé avec succès: %s", profile_id, imported_profile->name);
  } else {
    ESP_LOGE(TAG_CONFIG, "Erreur lors de la sauvegarde du profil %d", profile_id);
  }

  free(imported_profile);
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
  return nvs_manager_get_u16(NVS_NAMESPACE_LED_HW, "led_count", NUM_LEDS);
}

bool config_manager_set_led_count(uint16_t led_count) {
  // Validation
  if (led_count < 1 || led_count > 200) {
    ESP_LOGE(TAG_CONFIG, "Nombre de LEDs invalide: %d (1-200)", led_count);
    return false;
  }

  esp_err_t err = nvs_manager_set_u16(NVS_NAMESPACE_LED_HW, "led_count", led_count);
  if (err == ESP_OK) {
    ESP_LOGI(TAG_CONFIG, "Configuration LED sauvegardée: %d LEDs", led_count);
    return true;
  }
  return false;
}

void config_manager_reapply_default_effect(void) {
  if (!active_profile_loaded) {
    ESP_LOGW(TAG_CONFIG, "Aucun profil actif, impossible de réappliquer l'effet");
    return;
  }

  // Réappliquer l'effet par défaut du profil actif
  led_effects_set_config(&active_profile.default_effect);
  ESP_LOGI(TAG_CONFIG, "Effet par défaut réappliqué (audio_reactive=%d)", active_profile.default_effect.audio_reactive);
}

bool config_manager_can_create_profile(void) {
  nvs_stats_t nvs_stats;
  esp_err_t err = nvs_get_stats(NULL, &nvs_stats);

  if (err != ESP_OK) {
    ESP_LOGW(TAG_CONFIG, "Impossible de récupérer les stats NVS");
    return false;
  }

  // Un profil nécessite environ 1 entry (clé) + entries pour le blob
  // Taille d'un profil : sizeof(config_profile_t) ≈ 2KB
  // NVS entry = 32 bytes, donc un profil ≈ 2KB/32 ≈ 64 entries
  // On garde une marge de sécurité : on vérifie qu'il reste au moins 100 entries libres
  const size_t ENTRIES_PER_PROFILE = 100;
  const size_t MIN_FREE_ENTRIES    = ENTRIES_PER_PROFILE;

  bool can_create                  = nvs_stats.free_entries >= MIN_FREE_ENTRIES;

  ESP_LOGI(TAG_CONFIG,
           "NVS check: free=%zu, needed=%zu, can_create=%d (usage=%zu%%)",
           nvs_stats.free_entries,
           MIN_FREE_ENTRIES,
           can_create,
           (nvs_stats.total_entries > 0) ? (nvs_stats.used_entries * 100 / nvs_stats.total_entries) : 0);

  return can_create;
}
