#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config_manager.h"
#include "led_effects.h"
#include "config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include <string.h>
#include <time.h>

static const char *TAG = "ConfigMgr";

static config_profile_t profiles[MAX_PROFILES];
static int active_profile_id = -1;

// Système d'événements multiples
#define MAX_ACTIVE_EVENTS 4
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

// Noms des événements CAN
static const char* event_names[] = {
    "None",
    "Turn Left",
    "Turn Right",
    "Turn Hazard",
    "Charging",
    "Charge Complete",
    "Charging Started",
    "Charging Stopped",
    "Charging Cable Connected",
    "Charging Cable Disconnected",
    "Charging Port Opened",
    "Door Open",
    "Door Close",
    "Locked",
    "Unlocked",
    "Brake On",
    "Brake Off",
    "Blindspot Left",
    "Blindspot Right",
    "Blindspot Warning",
    "Night Mode On",
    "Night Mode Off",
    "Speed Threshold",
    "Autopilot Engaged",
    "Autopilot Disengaged",
    "Gear Drive",
    "Gear Reverse",
    "Gear Park",
    "Sentry Mode On",
    "Sentry Mode Off",
    "Sentry Alert"
};

bool config_manager_init(void) {
    // Initialiser les profils
    memset(profiles, 0, sizeof(profiles));

    // Charger les profils depuis NVS
    int loaded = config_manager_load_profiles();
    ESP_LOGI(TAG, "%d profil(s) chargé(s)", loaded);

    // Si aucun profil, créer un profil par défaut
    if (loaded == 0) {
        // Allouer dynamiquement pour éviter le stack overflow
        config_profile_t* default_profile = (config_profile_t*)malloc(sizeof(config_profile_t));
        if (default_profile == NULL) {
            ESP_LOGE(TAG, "Erreur allocation mémoire pour profil par défaut");
            return false;
        }
        config_manager_create_default_profile(default_profile, "Default");
        config_manager_save_profile(0, default_profile);
        free(default_profile);
        config_manager_activate_profile(0);
        ESP_LOGI(TAG, "Profil par défaut créé");
    } else {
        // Appliquer le profil actif chargé depuis NVS
        if (active_profile_id >= 0 && active_profile_id < MAX_PROFILES) {
            ESP_LOGI(TAG, "Application du profil actif %d: %s", active_profile_id, profiles[active_profile_id].name);
            led_effects_set_config(&profiles[active_profile_id].default_effect);
            // Ne pas activer le mode nuit au démarrage, il sera activé par l'événement CAN si auto_night_mode est activé
            led_effects_set_night_mode(false, profiles[active_profile_id].night_brightness);
            ESP_LOGI(TAG, "Auto night mode: %s (will respond to CAN events)",
                     profiles[active_profile_id].auto_night_mode ? "ENABLED" : "DISABLED");
            effect_override_active = false;
        } else {
            ESP_LOGW(TAG, "Aucun profil actif trouvé, activation du profil 0");
            config_manager_activate_profile(0);
        }
    }

    return true;
}

int config_manager_load_profiles(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("profiles", NVS_READONLY, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Impossible d'ouvrir NVS profiles");
        return 0;
    }

    int count = 0;
    char key[16];

    for (int i = 0; i < MAX_PROFILES; i++) {
        snprintf(key, sizeof(key), "profile_%d", i);
        size_t required_size = 0;

        // Vérifier d'abord la taille du blob
        err = nvs_get_blob(nvs_handle, key, NULL, &required_size);
        if (err == ESP_OK) {
            // Vérifier que la taille correspond à la structure actuelle
            if (required_size != sizeof(config_profile_t)) {
                ESP_LOGW(TAG, "Profile %d a une taille incompatible (%d vs %d) - ignoré",
                         i, required_size, sizeof(config_profile_t));
                continue;
            }

            // Charger le profil
            required_size = sizeof(config_profile_t);
            err = nvs_get_blob(nvs_handle, key, &profiles[i], &required_size);
            if (err == ESP_OK) {
                count++;
                if (profiles[i].active) {
                    active_profile_id = i;
                }
            }
        }
    }

    // Charger l'ID du profil actif
    int32_t active_id;
    err = nvs_get_i32(nvs_handle, "active_id", &active_id);
    if (err == ESP_OK) {
        active_profile_id = active_id;
    }

    nvs_close(nvs_handle);

    // Si aucun profil compatible n'a été chargé, avertir l'utilisateur
    if (count == 0) {
        ESP_LOGW(TAG, "Aucun profil compatible trouvé - un factory reset peut être nécessaire");
    }

    return count;
}

bool config_manager_save_profile(uint8_t profile_id, const config_profile_t* profile) {
    if (profile_id >= MAX_PROFILES || profile == NULL) {
        return false;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("profiles", NVS_READWRITE, &nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erreur ouverture NVS: %s", esp_err_to_name(err));
        return false;
    }
    
    char key[16];
    snprintf(key, sizeof(key), "profile_%d", profile_id);

    // Allouer dynamiquement pour éviter le stack overflow
    config_profile_t* profile_copy = (config_profile_t*)malloc(sizeof(config_profile_t));
    if (profile_copy == NULL) {
        ESP_LOGE(TAG, "Erreur allocation mémoire");
        nvs_close(nvs_handle);
        return false;
    }

    // Mettre à jour le timestamp
    memcpy(profile_copy, profile, sizeof(config_profile_t));
    profile_copy->modified_timestamp = (uint32_t)time(NULL);

    err = nvs_set_blob(nvs_handle, key, profile_copy, sizeof(config_profile_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erreur sauvegarde profil: %s", esp_err_to_name(err));
        free(profile_copy);
        nvs_close(nvs_handle);
        return false;
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    // Mettre à jour la copie en mémoire
    memcpy(&profiles[profile_id], profile_copy, sizeof(config_profile_t));
    free(profile_copy);
    
    ESP_LOGI(TAG, "Profil %d sauvegardé: %s", profile_id, profile->name);
    return true;
}

bool config_manager_load_profile(uint8_t profile_id, config_profile_t* profile) {
    if (profile_id >= MAX_PROFILES || profile == NULL) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("profiles", NVS_READONLY, &nvs_handle);

    if (err != ESP_OK) {
        return false;
    }

    char key[16];
    snprintf(key, sizeof(key), "profile_%d", profile_id);

    // Vérifier d'abord la taille du blob
    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, key, NULL, &required_size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    // Vérifier que la taille correspond à la structure actuelle
    if (required_size != sizeof(config_profile_t)) {
        ESP_LOGW(TAG, "Profile %d a une taille incompatible (%d vs %d) - ignoré",
                 profile_id, required_size, sizeof(config_profile_t));
        nvs_close(nvs_handle);
        return false;
    }

    // Charger le profil
    required_size = sizeof(config_profile_t);
    err = nvs_get_blob(nvs_handle, key, profile, &required_size);
    nvs_close(nvs_handle);

    return (err == ESP_OK);
}

bool config_manager_delete_profile(uint8_t profile_id) {
    if (profile_id >= MAX_PROFILES) {
        return false;
    }

    // Vérifier que le profil n'est pas utilisé dans un événement
    for (int p = 0; p < MAX_PROFILES; p++) {
        if (profiles[p].name[0] == '\0') continue; // Profil vide

        for (int e = 0; e < CAN_EVENT_MAX; e++) {
            if ((profiles[p].event_effects[e].profile_id == profile_id) && (profile_id != p)) {
                ESP_LOGW(TAG, "Cannot delete profile %d: used by event %d in profile %d",
                         profile_id, e, p);
                return false;
            }
        }
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("profiles", NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK) {
        return false;
    }

    // Supprimer le profil
    char key[16];
    snprintf(key, sizeof(key), "profile_%d", profile_id);
    err = nvs_erase_key(nvs_handle, key);

    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    // Marquer comme supprimé dans le tableau RAM
    memset(&profiles[profile_id], 0, sizeof(config_profile_t));

    // Compresser les IDs : décaler tous les profils suivants
    bool needs_compression = false;
    for (int i = profile_id + 1; i < MAX_PROFILES; i++) {
        if (profiles[i].name[0] != '\0') {
            needs_compression = true;
            break;
        }
    }

    if (needs_compression) {
        ESP_LOGI(TAG, "Compression des IDs de profils après suppression du profil %d", profile_id);

        // Décaler tous les profils dans NVS et RAM
        for (int i = profile_id; i < MAX_PROFILES - 1; i++) {
            // Charger le profil suivant
            char next_key[16];
            snprintf(next_key, sizeof(next_key), "profile_%d", i + 1);

            size_t required_size = sizeof(config_profile_t);
            esp_err_t read_err = nvs_get_blob(nvs_handle, next_key, &profiles[i], &required_size);

            if (read_err == ESP_OK) {
                // Sauvegarder au nouvel emplacement (i au lieu de i+1)
                char current_key[16];
                snprintf(current_key, sizeof(current_key), "profile_%d", i);
                nvs_set_blob(nvs_handle, current_key, &profiles[i], sizeof(config_profile_t));

                // Effacer l'ancien emplacement
                nvs_erase_key(nvs_handle, next_key);

                ESP_LOGI(TAG, "Profil %d déplacé vers ID %d", i + 1, i);
            } else {
                // Plus de profils à décaler
                memset(&profiles[i], 0, sizeof(config_profile_t));
                break;
            }
        }

        // Mettre à jour l'ID du profil actif
        if (active_profile_id > profile_id) {
            active_profile_id--;
            nvs_set_i32(nvs_handle, "active_id", active_profile_id);
            ESP_LOGI(TAG, "ID du profil actif mis à jour: %d", active_profile_id);
        } else if (active_profile_id == profile_id) {
            // Le profil actif a été supprimé, activer le profil 0 s'il existe
            if (profiles[0].name[0] != '\0') {
                active_profile_id = 0;
                nvs_set_i32(nvs_handle, "active_id", 0);
                ESP_LOGI(TAG, "Profil actif supprimé, activation du profil 0");
            } else {
                active_profile_id = -1;
                nvs_erase_key(nvs_handle, "active_id");
                ESP_LOGI(TAG, "Profil actif supprimé, aucun profil disponible");
            }
        }
    } else {
        // Pas de compression nécessaire
        if (active_profile_id == profile_id) {
            // Chercher le premier profil disponible
            active_profile_id = -1;
            for (int i = 0; i < MAX_PROFILES; i++) {
                if (profiles[i].name[0] != '\0') {
                    active_profile_id = i;
                    nvs_set_i32(nvs_handle, "active_id", i);
                    ESP_LOGI(TAG, "Activation automatique du profil %d", i);
                    break;
                }
            }

            if (active_profile_id == -1) {
                nvs_erase_key(nvs_handle, "active_id");
                ESP_LOGI(TAG, "Aucun profil disponible");
            }
        }
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Profil %d supprimé avec succès", profile_id);
    return true;
}

bool config_manager_activate_profile(uint8_t profile_id) {
    if (profile_id >= MAX_PROFILES) {
        return false;
    }
    
    // Vérifier que le profil existe
    if (profiles[profile_id].name[0] == '\0') {
        // Profil vide, essayer de charger depuis NVS
        if (!config_manager_load_profile(profile_id, &profiles[profile_id])) {
            ESP_LOGE(TAG, "Profil %d inexistant", profile_id);
            return false;
        }
    }
    
    // Désactiver l'ancien profil
    if (active_profile_id >= 0 && active_profile_id < MAX_PROFILES) {
        profiles[active_profile_id].active = false;
    }
    
    // Activer le nouveau profil
    active_profile_id = profile_id;
    profiles[profile_id].active = true;

    // Sauvegarder l'ID actif
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("profiles", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_i32(nvs_handle, "active_id", profile_id);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    // Appliquer l'effet par défaut
    led_effects_set_config(&profiles[profile_id].default_effect);
    // Ne pas activer le mode nuit lors du changement de profil, il sera activé par l'événement CAN
    led_effects_set_night_mode(false, profiles[profile_id].night_brightness);
    effect_override_active = false;

    ESP_LOGI(TAG, "Profil %d activé: %s (auto night mode: %s, brightness: %d)",
             profile_id, profiles[profile_id].name,
             profiles[profile_id].auto_night_mode ? "ENABLED" : "DISABLED",
             profiles[profile_id].night_brightness);
    return true;
}

bool config_manager_get_active_profile(config_profile_t* profile) {
    if (active_profile_id < 0 || active_profile_id >= MAX_PROFILES || profile == NULL) {
        return false;
    }
    
    memcpy(profile, &profiles[active_profile_id], sizeof(config_profile_t));
    return true;
}

int config_manager_get_active_profile_id(void) {
    return active_profile_id;
}

int config_manager_list_profiles(config_profile_t* profile_list, int max_profiles) {
    int count = 0;
    
    for (int i = 0; i < MAX_PROFILES && count < max_profiles; i++) {
        if (profiles[i].name[0] != '\0') {
            memcpy(&profile_list[count], &profiles[i], sizeof(config_profile_t));
            count++;
        }
    }
    
    return count;
}

void config_manager_create_default_profile(config_profile_t* profile, const char* name) {
    memset(profile, 0, sizeof(config_profile_t));
    
    strncpy(profile->name, name, PROFILE_NAME_MAX_LEN - 1);
    
    // Effet par défaut: rainbow
    profile->default_effect.effect = EFFECT_SCAN;
    profile->default_effect.brightness = 128;
    profile->default_effect.speed = 50;
    profile->default_effect.color1 = 0xFF0000;
    
    // Effet mode nuit: breathing doux
    profile->night_mode_effect.effect = EFFECT_BREATHING;
    profile->night_mode_effect.brightness = 30;
    profile->night_mode_effect.speed = 20;
    profile->night_mode_effect.color1 = 0x0000FF;
    
    // Configurer les effets pour événements
    // Clignotant gauche
    profile->event_effects[CAN_EVENT_TURN_LEFT].event = CAN_EVENT_TURN_LEFT;
    profile->event_effects[CAN_EVENT_TURN_LEFT].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_TURN_LEFT].profile_id = -1;
    profile->event_effects[CAN_EVENT_TURN_LEFT].effect_config.effect = EFFECT_TURN_SIGNAL;
    profile->event_effects[CAN_EVENT_TURN_LEFT].effect_config.brightness = 200;
    profile->event_effects[CAN_EVENT_TURN_LEFT].effect_config.speed = 200;
    profile->event_effects[CAN_EVENT_TURN_LEFT].effect_config.color1 = 0xFF8000;
    profile->event_effects[CAN_EVENT_TURN_LEFT].effect_config.reverse = true; // Animation depuis centre vers gauche
    profile->event_effects[CAN_EVENT_TURN_LEFT].duration_ms = 0;
    profile->event_effects[CAN_EVENT_TURN_LEFT].priority = 200;
    profile->event_effects[CAN_EVENT_TURN_LEFT].enabled = true;

    // Clignotant droite (identique)
    memcpy(&profile->event_effects[CAN_EVENT_TURN_RIGHT],
           &profile->event_effects[CAN_EVENT_TURN_LEFT],
           sizeof(can_event_effect_t));
    profile->event_effects[CAN_EVENT_TURN_RIGHT].event = CAN_EVENT_TURN_RIGHT;
    profile->event_effects[CAN_EVENT_TURN_RIGHT].effect_config.reverse = false; // Animation depuis centre vers droite

    // Charge
    profile->event_effects[CAN_EVENT_CHARGING].event = CAN_EVENT_CHARGING;
    profile->event_effects[CAN_EVENT_CHARGING].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_CHARGING].profile_id = -1;
    profile->event_effects[CAN_EVENT_CHARGING].effect_config.effect = EFFECT_CHARGE_STATUS;
    profile->event_effects[CAN_EVENT_CHARGING].effect_config.brightness = 150;
    profile->event_effects[CAN_EVENT_CHARGING].effect_config.speed = 50;
    profile->event_effects[CAN_EVENT_CHARGING].effect_config.color1 = 0x00FF00;
    profile->event_effects[CAN_EVENT_CHARGING].duration_ms = 0;
    profile->event_effects[CAN_EVENT_CHARGING].priority = 150;
    profile->event_effects[CAN_EVENT_CHARGING].enabled = true;
    
    // Blindspot
    profile->event_effects[CAN_EVENT_BLINDSPOT_LEFT].event = CAN_EVENT_BLINDSPOT_LEFT;
    profile->event_effects[CAN_EVENT_BLINDSPOT_LEFT].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_BLINDSPOT_LEFT].profile_id = -1;
    profile->event_effects[CAN_EVENT_BLINDSPOT_LEFT].effect_config.effect = EFFECT_BLINDSPOT_FLASH;
    profile->event_effects[CAN_EVENT_BLINDSPOT_LEFT].effect_config.brightness = 255;
    profile->event_effects[CAN_EVENT_BLINDSPOT_LEFT].effect_config.speed = 255;
    profile->event_effects[CAN_EVENT_BLINDSPOT_LEFT].effect_config.color1 = 0xFF0000;
    profile->event_effects[CAN_EVENT_BLINDSPOT_LEFT].effect_config.reverse = true; // Animation depuis centre vers gauche
    profile->event_effects[CAN_EVENT_BLINDSPOT_LEFT].duration_ms = 0;
    profile->event_effects[CAN_EVENT_BLINDSPOT_LEFT].priority = 250;
    profile->event_effects[CAN_EVENT_BLINDSPOT_LEFT].enabled = true;

    // Blindspot droite (identique)
    memcpy(&profile->event_effects[CAN_EVENT_BLINDSPOT_RIGHT],
           &profile->event_effects[CAN_EVENT_BLINDSPOT_LEFT],
           sizeof(can_event_effect_t));
    profile->event_effects[CAN_EVENT_BLINDSPOT_RIGHT].event = CAN_EVENT_BLINDSPOT_RIGHT;
    profile->event_effects[CAN_EVENT_BLINDSPOT_RIGHT].effect_config.reverse = false; // Animation depuis centre vers droite

    // Blind spot warning
    profile->event_effects[CAN_EVENT_BLINDSPOT_WARNING].event = CAN_EVENT_BLINDSPOT_WARNING;
    profile->event_effects[CAN_EVENT_BLINDSPOT_WARNING].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_BLINDSPOT_WARNING].profile_id = -1;
    profile->event_effects[CAN_EVENT_BLINDSPOT_WARNING].effect_config.effect = EFFECT_HAZARD;
    profile->event_effects[CAN_EVENT_BLINDSPOT_WARNING].effect_config.brightness = 255;
    profile->event_effects[CAN_EVENT_BLINDSPOT_WARNING].effect_config.speed = 100;
    profile->event_effects[CAN_EVENT_BLINDSPOT_WARNING].effect_config.color1 = 0xFF0000;
    profile->event_effects[CAN_EVENT_BLINDSPOT_WARNING].duration_ms = 0;
    profile->event_effects[CAN_EVENT_BLINDSPOT_WARNING].priority = 220;
    profile->event_effects[CAN_EVENT_BLINDSPOT_WARNING].enabled = false;

    memcpy(&profile->event_effects[CAN_EVENT_FORWARD_COLISSION],
           &profile->event_effects[CAN_EVENT_BLINDSPOT_WARNING],
           sizeof(can_event_effect_t));
    profile->event_effects[CAN_EVENT_FORWARD_COLISSION].event = CAN_EVENT_FORWARD_COLISSION;

    // Hazard/Warning (clignotants de détresse)
    profile->event_effects[CAN_EVENT_TURN_HAZARD].event = CAN_EVENT_TURN_HAZARD;
    profile->event_effects[CAN_EVENT_TURN_HAZARD].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_TURN_HAZARD].profile_id = -1;
    profile->event_effects[CAN_EVENT_TURN_HAZARD].effect_config.effect = EFFECT_HAZARD;
    profile->event_effects[CAN_EVENT_TURN_HAZARD].effect_config.brightness = 255;
    profile->event_effects[CAN_EVENT_TURN_HAZARD].effect_config.speed = 100;
    profile->event_effects[CAN_EVENT_TURN_HAZARD].effect_config.color1 = 0xFF8000;
    profile->event_effects[CAN_EVENT_TURN_HAZARD].duration_ms = 0;
    profile->event_effects[CAN_EVENT_TURN_HAZARD].priority = 220;
    profile->event_effects[CAN_EVENT_TURN_HAZARD].enabled = true;

    // Charge complète
    profile->event_effects[CAN_EVENT_CHARGE_COMPLETE].event = CAN_EVENT_CHARGE_COMPLETE;
    profile->event_effects[CAN_EVENT_CHARGE_COMPLETE].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_CHARGE_COMPLETE].profile_id = -1;
    profile->event_effects[CAN_EVENT_CHARGE_COMPLETE].effect_config.effect = EFFECT_BREATHING;
    profile->event_effects[CAN_EVENT_CHARGE_COMPLETE].effect_config.brightness = 200;
    profile->event_effects[CAN_EVENT_CHARGE_COMPLETE].effect_config.speed = 30;
    profile->event_effects[CAN_EVENT_CHARGE_COMPLETE].effect_config.color1 = 0x00FF00;
    profile->event_effects[CAN_EVENT_CHARGE_COMPLETE].duration_ms = 0;
    profile->event_effects[CAN_EVENT_CHARGE_COMPLETE].priority = 140;
    profile->event_effects[CAN_EVENT_CHARGE_COMPLETE].enabled = true;

    // Câble connecté
    memcpy(&profile->event_effects[CAN_EVENT_CHARGING_STARTED],
           &profile->event_effects[CAN_EVENT_CHARGE_COMPLETE],
           sizeof(can_event_effect_t));    
    profile->event_effects[CAN_EVENT_CHARGING_STARTED].event = CAN_EVENT_CHARGING_STARTED;
    profile->event_effects[CAN_EVENT_CHARGING_STARTED].enabled = false;
    profile->event_effects[CAN_EVENT_CHARGING_STARTED].duration_ms = 500;

    memcpy(&profile->event_effects[CAN_EVENT_CHARGING_STOPPED],
           &profile->event_effects[CAN_EVENT_CHARGING_STARTED],
           sizeof(can_event_effect_t));    
    profile->event_effects[CAN_EVENT_CHARGING_STOPPED].event = CAN_EVENT_CHARGING_STOPPED;

    memcpy(&profile->event_effects[CAN_EVENT_CHARGING_CABLE_CONNECTED],
           &profile->event_effects[CAN_EVENT_CHARGING_STARTED],
           sizeof(can_event_effect_t));    
    profile->event_effects[CAN_EVENT_CHARGING_CABLE_CONNECTED].event = CAN_EVENT_CHARGING_CABLE_CONNECTED;

    memcpy(&profile->event_effects[CAN_EVENT_CHARGING_CABLE_DISCONNECTED],
           &profile->event_effects[CAN_EVENT_CHARGING_STARTED],
           sizeof(can_event_effect_t));    
    profile->event_effects[CAN_EVENT_CHARGING_CABLE_DISCONNECTED].event = CAN_EVENT_CHARGING_CABLE_DISCONNECTED;
    memcpy(&profile->event_effects[CAN_EVENT_CHARGING_PORT_OPENED],
           &profile->event_effects[CAN_EVENT_CHARGING_STARTED],
           sizeof(can_event_effect_t));    
    profile->event_effects[CAN_EVENT_CHARGING_PORT_OPENED].event = CAN_EVENT_CHARGING_PORT_OPENED;
    
    // Porte ouverte
    profile->event_effects[CAN_EVENT_DOOR_OPEN].event = CAN_EVENT_DOOR_OPEN;
    profile->event_effects[CAN_EVENT_DOOR_OPEN].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_DOOR_OPEN].profile_id = -1;
    profile->event_effects[CAN_EVENT_DOOR_OPEN].effect_config.effect = EFFECT_BREATHING;
    profile->event_effects[CAN_EVENT_DOOR_OPEN].effect_config.brightness = 180;
    profile->event_effects[CAN_EVENT_DOOR_OPEN].effect_config.speed = 80;
    profile->event_effects[CAN_EVENT_DOOR_OPEN].effect_config.color1 = 0xFFFFFF;
    profile->event_effects[CAN_EVENT_DOOR_OPEN].duration_ms = 5000; // 5 secondes
    profile->event_effects[CAN_EVENT_DOOR_OPEN].priority = 100;
    profile->event_effects[CAN_EVENT_DOOR_OPEN].enabled = true;

    // Porte fermée
    profile->event_effects[CAN_EVENT_DOOR_CLOSE].event = CAN_EVENT_DOOR_CLOSE;
    profile->event_effects[CAN_EVENT_DOOR_CLOSE].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_DOOR_CLOSE].profile_id = -1;
    profile->event_effects[CAN_EVENT_DOOR_CLOSE].effect_config.effect = EFFECT_BREATHING;
    profile->event_effects[CAN_EVENT_DOOR_CLOSE].effect_config.brightness = 100;
    profile->event_effects[CAN_EVENT_DOOR_CLOSE].effect_config.speed = 120;
    profile->event_effects[CAN_EVENT_DOOR_CLOSE].effect_config.color1 = 0x0000FF;
    profile->event_effects[CAN_EVENT_DOOR_CLOSE].duration_ms = 2000; // 2 secondes
    profile->event_effects[CAN_EVENT_DOOR_CLOSE].priority = 90;
    profile->event_effects[CAN_EVENT_DOOR_CLOSE].enabled = true;

    // Verrouillé
    profile->event_effects[CAN_EVENT_LOCKED].event = CAN_EVENT_LOCKED;
    profile->event_effects[CAN_EVENT_LOCKED].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_LOCKED].profile_id = -1;
    profile->event_effects[CAN_EVENT_LOCKED].effect_config.effect = EFFECT_STROBE;
    profile->event_effects[CAN_EVENT_LOCKED].effect_config.brightness = 200;
    profile->event_effects[CAN_EVENT_LOCKED].effect_config.speed = 150;
    profile->event_effects[CAN_EVENT_LOCKED].effect_config.color1 = 0xFF0000;
    profile->event_effects[CAN_EVENT_LOCKED].duration_ms = 1000; // 1 seconde
    profile->event_effects[CAN_EVENT_LOCKED].priority = 110;
    profile->event_effects[CAN_EVENT_LOCKED].enabled = true;

    // Déverrouillé
    profile->event_effects[CAN_EVENT_UNLOCKED].event = CAN_EVENT_UNLOCKED;
    profile->event_effects[CAN_EVENT_UNLOCKED].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_UNLOCKED].profile_id = -1;
    profile->event_effects[CAN_EVENT_UNLOCKED].effect_config.effect = EFFECT_BREATHING;
    profile->event_effects[CAN_EVENT_UNLOCKED].effect_config.brightness = 200;
    profile->event_effects[CAN_EVENT_UNLOCKED].effect_config.speed = 100;
    profile->event_effects[CAN_EVENT_UNLOCKED].effect_config.color1 = 0x00FF00;
    profile->event_effects[CAN_EVENT_UNLOCKED].duration_ms = 1500; // 1.5 secondes
    profile->event_effects[CAN_EVENT_UNLOCKED].priority = 110;
    profile->event_effects[CAN_EVENT_UNLOCKED].enabled = true;

    // Frein activé
    profile->event_effects[CAN_EVENT_BRAKE_ON].event = CAN_EVENT_BRAKE_ON;
    profile->event_effects[CAN_EVENT_BRAKE_ON].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_BRAKE_ON].profile_id = -1;
    profile->event_effects[CAN_EVENT_BRAKE_ON].effect_config.effect = EFFECT_BRAKE_LIGHT;
    profile->event_effects[CAN_EVENT_BRAKE_ON].effect_config.brightness = 255;
    profile->event_effects[CAN_EVENT_BRAKE_ON].effect_config.speed = 100;
    profile->event_effects[CAN_EVENT_BRAKE_ON].effect_config.color1 = 0xFF0000;
    profile->event_effects[CAN_EVENT_BRAKE_ON].duration_ms = 0;
    profile->event_effects[CAN_EVENT_BRAKE_ON].priority = 180;
    profile->event_effects[CAN_EVENT_BRAKE_ON].enabled = true;

    // Frein relâché
    profile->event_effects[CAN_EVENT_BRAKE_OFF].event = CAN_EVENT_BRAKE_OFF;
    profile->event_effects[CAN_EVENT_BRAKE_OFF].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_BRAKE_OFF].profile_id = -1;
    profile->event_effects[CAN_EVENT_BRAKE_OFF].effect_config.effect = EFFECT_FADE;
    profile->event_effects[CAN_EVENT_BRAKE_OFF].effect_config.brightness = 100;
    profile->event_effects[CAN_EVENT_BRAKE_OFF].effect_config.speed = 150;
    profile->event_effects[CAN_EVENT_BRAKE_OFF].effect_config.color1 = 0xFF0000;
    profile->event_effects[CAN_EVENT_BRAKE_OFF].duration_ms = 500; // 0.5 seconde
    profile->event_effects[CAN_EVENT_BRAKE_OFF].priority = 170;
    profile->event_effects[CAN_EVENT_BRAKE_OFF].enabled = false; // Désactivé par défaut

    // Mode nuit activé - DÉSACTIVÉ par défaut (le mode nuit est géré globalement, pas comme un effet)
    profile->event_effects[CAN_EVENT_NIGHT_MODE_ON].event = CAN_EVENT_NIGHT_MODE_ON;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_ON].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_ON].profile_id = -1;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_ON].effect_config.effect = EFFECT_OFF;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_ON].effect_config.brightness = 0;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_ON].effect_config.speed = 0;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_ON].effect_config.color1 = 0x000000;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_ON].duration_ms = 0;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_ON].priority = 0;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_ON].enabled = false; // Toujours désactivé

    // Mode nuit désactivé - DÉSACTIVÉ par défaut (le mode nuit est géré globalement, pas comme un effet)
    profile->event_effects[CAN_EVENT_NIGHT_MODE_OFF].event = CAN_EVENT_NIGHT_MODE_OFF;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_OFF].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_OFF].profile_id = -1;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_OFF].effect_config.effect = EFFECT_OFF;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_OFF].effect_config.brightness = 0;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_OFF].effect_config.speed = 0;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_OFF].effect_config.color1 = 0x000000;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_OFF].duration_ms = 0;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_OFF].priority = 0;
    profile->event_effects[CAN_EVENT_NIGHT_MODE_OFF].enabled = false; // Toujours désactivé

    // Seuil de vitesse
    profile->event_effects[CAN_EVENT_SPEED_THRESHOLD].event = CAN_EVENT_SPEED_THRESHOLD;
    profile->event_effects[CAN_EVENT_SPEED_THRESHOLD].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_SPEED_THRESHOLD].profile_id = -1;
    profile->event_effects[CAN_EVENT_SPEED_THRESHOLD].effect_config.effect = EFFECT_RUNNING_LIGHTS;
    profile->event_effects[CAN_EVENT_SPEED_THRESHOLD].effect_config.brightness = 200;
    profile->event_effects[CAN_EVENT_SPEED_THRESHOLD].effect_config.speed = 120;
    profile->event_effects[CAN_EVENT_SPEED_THRESHOLD].effect_config.color1 = 0x00FFFF;
    profile->event_effects[CAN_EVENT_SPEED_THRESHOLD].duration_ms = 0;
    profile->event_effects[CAN_EVENT_SPEED_THRESHOLD].priority = 60;
    profile->event_effects[CAN_EVENT_SPEED_THRESHOLD].enabled = false; // Désactivé par défaut

    memcpy(&profile->event_effects[CAN_EVENT_AUTOPILOT_ENGAGED],
           &profile->event_effects[CAN_EVENT_SPEED_THRESHOLD],
           sizeof(can_event_effect_t));
    profile->event_effects[CAN_EVENT_AUTOPILOT_ENGAGED].event = CAN_EVENT_AUTOPILOT_ENGAGED;
    profile->event_effects[CAN_EVENT_AUTOPILOT_ENGAGED].duration_ms = 500;

    memcpy(&profile->event_effects[CAN_EVENT_AUTOPILOT_DISENGAGED],
           &profile->event_effects[CAN_EVENT_AUTOPILOT_ENGAGED],
           sizeof(can_event_effect_t));   
    profile->event_effects[CAN_EVENT_AUTOPILOT_DISENGAGED].event = CAN_EVENT_AUTOPILOT_DISENGAGED;

    memcpy(&profile->event_effects[CAN_EVENT_AUTOPILOT_ABORTING],
           &profile->event_effects[CAN_EVENT_AUTOPILOT_ENGAGED],
           sizeof(can_event_effect_t));   
    profile->event_effects[CAN_EVENT_AUTOPILOT_ABORTING].event = CAN_EVENT_AUTOPILOT_ABORTING;

    memcpy(&profile->event_effects[CAN_EVENT_GEAR_DRIVE],
           &profile->event_effects[CAN_EVENT_AUTOPILOT_ENGAGED],
           sizeof(can_event_effect_t));   
    profile->event_effects[CAN_EVENT_GEAR_DRIVE].event = CAN_EVENT_GEAR_DRIVE;

    memcpy(&profile->event_effects[CAN_EVENT_GEAR_REVERSE],
           &profile->event_effects[CAN_EVENT_AUTOPILOT_ENGAGED],
           sizeof(can_event_effect_t));   
    profile->event_effects[CAN_EVENT_GEAR_REVERSE].event = CAN_EVENT_GEAR_REVERSE;

    memcpy(&profile->event_effects[CAN_EVENT_GEAR_PARK],
           &profile->event_effects[CAN_EVENT_AUTOPILOT_ENGAGED],
           sizeof(can_event_effect_t));   
    profile->event_effects[CAN_EVENT_GEAR_PARK].event = CAN_EVENT_GEAR_PARK;

    // Mode Sentry armé/désarmé
    profile->event_effects[CAN_EVENT_SENTRY_MODE_ON].event = CAN_EVENT_SENTRY_MODE_ON;
    profile->event_effects[CAN_EVENT_SENTRY_MODE_ON].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_SENTRY_MODE_ON].profile_id = -1;
    profile->event_effects[CAN_EVENT_SENTRY_MODE_ON].effect_config.effect = EFFECT_BREATHING;
    profile->event_effects[CAN_EVENT_SENTRY_MODE_ON].effect_config.brightness = 180;
    profile->event_effects[CAN_EVENT_SENTRY_MODE_ON].effect_config.speed = 40;
    profile->event_effects[CAN_EVENT_SENTRY_MODE_ON].effect_config.color1 = 0xFF0000;
    profile->event_effects[CAN_EVENT_SENTRY_MODE_ON].duration_ms = 0;
    profile->event_effects[CAN_EVENT_SENTRY_MODE_ON].priority = 160;
    profile->event_effects[CAN_EVENT_SENTRY_MODE_ON].enabled = false;

    memcpy(&profile->event_effects[CAN_EVENT_SENTRY_MODE_OFF],
           &profile->event_effects[CAN_EVENT_SENTRY_MODE_ON],
           sizeof(can_event_effect_t));
    profile->event_effects[CAN_EVENT_SENTRY_MODE_OFF].event = CAN_EVENT_SENTRY_MODE_OFF;
    profile->event_effects[CAN_EVENT_SENTRY_MODE_OFF].effect_config.color1 = 0x0040FF;

    // Alerte Sentry
    profile->event_effects[CAN_EVENT_SENTRY_ALERT].event = CAN_EVENT_SENTRY_ALERT;
    profile->event_effects[CAN_EVENT_SENTRY_ALERT].action_type = EVENT_ACTION_APPLY_EFFECT;
    profile->event_effects[CAN_EVENT_SENTRY_ALERT].profile_id = -1;
    profile->event_effects[CAN_EVENT_SENTRY_ALERT].effect_config.effect = EFFECT_STROBE;
    profile->event_effects[CAN_EVENT_SENTRY_ALERT].effect_config.brightness = 255;
    profile->event_effects[CAN_EVENT_SENTRY_ALERT].effect_config.speed = 220;
    profile->event_effects[CAN_EVENT_SENTRY_ALERT].effect_config.color1 = 0xFF2020;
    profile->event_effects[CAN_EVENT_SENTRY_ALERT].duration_ms = 3000;
    profile->event_effects[CAN_EVENT_SENTRY_ALERT].priority = 240;
    profile->event_effects[CAN_EVENT_SENTRY_ALERT].enabled = true;

    // Paramètres généraux
    profile->auto_night_mode = false; // Désactivé par défaut, l'utilisateur peut l'activer manuellement
    profile->night_brightness = 30;
    profile->speed_threshold = 50;  // 50 km/h
    
    profile->active = false;
    profile->created_timestamp = (uint32_t)time(NULL);
    profile->modified_timestamp = profile->created_timestamp;
}

bool config_manager_set_event_effect(uint8_t profile_id, 
                                     can_event_type_t event,
                                     const effect_config_t* effect_config,
                                     uint16_t duration_ms,
                                     uint8_t priority) {
    if (profile_id >= MAX_PROFILES || event >= CAN_EVENT_MAX || effect_config == NULL) {
        return false;
    }
    
    profiles[profile_id].event_effects[event].event = event;
    memcpy(&profiles[profile_id].event_effects[event].effect_config, 
           effect_config, sizeof(effect_config_t));
    profiles[profile_id].event_effects[event].duration_ms = duration_ms;
    profiles[profile_id].event_effects[event].priority = priority;
    profiles[profile_id].event_effects[event].enabled = true;

    return true;
}

bool config_manager_set_event_enabled(uint8_t profile_id, can_event_type_t event, bool enabled) {
    if (profile_id >= MAX_PROFILES || event >= CAN_EVENT_MAX) {
        return false;
    }

    profiles[profile_id].event_effects[event].enabled = enabled;
    return true;
}

bool config_manager_process_can_event(can_event_type_t event) {
    if (active_profile_id < 0 || event > CAN_EVENT_MAX) {
        return false;
    }

    can_event_effect_t* event_effect = &profiles[active_profile_id].event_effects[event];
    effect_config_t effect_to_apply;
    uint16_t duration_ms;
    uint8_t priority;

    // Gérer le changement de profil si configuré (indépendant du flag enabled)
    if (event_effect->action_type != EVENT_ACTION_APPLY_EFFECT) {
        // EVENT_ACTION_SWITCH_PROFILE
        if (event_effect->profile_id >= 0 && event_effect->profile_id < MAX_PROFILES) {
            ESP_LOGI(TAG, "Event %d: Switching to profile %d", event, event_effect->profile_id);
            config_manager_activate_profile(event_effect->profile_id);

            return false;
        }
    }

    // Si l'effet est configuré et activé, l'utiliser
    if (event_effect->enabled) {
        memcpy(&effect_to_apply, &event_effect->effect_config, sizeof(effect_config_t));
        duration_ms = event_effect->duration_ms;
        priority = event_effect->priority;

        // IMPORTANT: La zone doit être déterminée par le type d'événement CAN, pas par la config
        // Cela garantit que les événements latéralisés gardent toujours leur latéralité
        switch (event) {
            case CAN_EVENT_TURN_LEFT:
            case CAN_EVENT_BLINDSPOT_LEFT:
                effect_to_apply.zone = LED_ZONE_LEFT;
                break;
            case CAN_EVENT_TURN_RIGHT:
            case CAN_EVENT_BLINDSPOT_RIGHT:
                effect_to_apply.zone = LED_ZONE_RIGHT;
                break;
            default:
                // Pour les autres événements, garder la zone configurée (ou FULL par défaut)
                if (effect_to_apply.zone != LED_ZONE_LEFT && effect_to_apply.zone != LED_ZONE_RIGHT) {
                    effect_to_apply.zone = LED_ZONE_FULL;
                }
                break;
        }
    } 
    
    if(event_effect->enabled) {
      // Trouver un slot libre pour l'événement
      int free_slot = -1;
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
          // Pas de slot libre, vérifier si on peut écraser un événement de priorité inférieure
          int lowest_priority_slot = -1;
          uint8_t lowest_priority = 255;

          for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
              if (active_events[i].active && active_events[i].priority < priority &&
                  active_events[i].priority < lowest_priority) {
                  lowest_priority = active_events[i].priority;
                  lowest_priority_slot = i;
              }
          }

          if (lowest_priority_slot >= 0) {
              slot = lowest_priority_slot;
              ESP_LOGI(TAG, "Écrasement événement priorité %d par priorité %d", lowest_priority, priority);
          } else {
              // ESP_LOGW(TAG, "Événement '%s' ignoré (pas de slot disponible)",
                      // config_manager_get_event_name(event));
              return false;
          }
      }

      // Enregistrer l'événement actif
      active_events[slot].event = event;
      active_events[slot].effect_config = effect_to_apply;
      active_events[slot].start_time = xTaskGetTickCount();
      active_events[slot].duration_ms = duration_ms;
      active_events[slot].priority = priority;
      active_events[slot].active = true;

      // Appliquer immédiatement l'effet de plus haute priorité actif
      led_effects_set_config(&effect_to_apply);
      effect_override_active = true;

      if (duration_ms > 0) {
          ESP_LOGI(TAG, "Effet '%s' activé pour %dms (priorité %d)",
                  config_manager_get_event_name(event),
                  duration_ms,
                  priority);
      } else {
          ESP_LOGI(TAG, "Effet '%s' activé (permanent, priorité %d)",
                  config_manager_get_event_name(event),
                  priority);
      }

      return true;
    } else {
      // ESP_LOGW(TAG, "Effet par défaut ignoré pour '%s'",
      //         config_manager_get_event_name(event));
    }

    return false;
}

void config_manager_stop_event(can_event_type_t event) {
    // Vérifier que l'événement est valide
    if (event > CAN_EVENT_MAX) {
        return;
    }

    // Désactiver tous les slots qui correspondent à cet événement
    for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
        if (active_events[i].active && active_events[i].event == event) {
            ESP_LOGI(TAG, "Arrêt manuel de l'événement '%s'", config_manager_get_event_name(event));
            active_events[i].active = false;
        }
    }
}

void config_manager_stop_all_events(void) {
    for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
        if (active_events[i].active) {
            ESP_LOGI(TAG, "Arret global de l'evenement '%s'", config_manager_get_event_name(active_events[i].event));
            active_events[i].active = false;
        }
    }
}

void config_manager_update(void) {
    uint32_t now = xTaskGetTickCount();
    bool any_active = false;

    // Vérifier et expirer les événements actifs
    for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
        if (!active_events[i].active) continue;

        // Vérifier si l'événement est expiré (si durée > 0)
        if (active_events[i].duration_ms > 0) {
            uint32_t elapsed = now - active_events[i].start_time;
            if (elapsed >= pdMS_TO_TICKS(active_events[i].duration_ms)) {
                ESP_LOGD(TAG, "Événement '%s' terminé", config_manager_get_event_name(active_events[i].event));
                active_events[i].active = false;
                continue;
            }
        }

        any_active = true;
    }

    // Si des événements sont actifs
    if (any_active) {
        // Trouver la priorité maximale parmi tous les événements actifs
        int max_priority = -1;
        for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
            if (active_events[i].active && active_events[i].priority > max_priority) {
                max_priority = active_events[i].priority;
            }
        }

        // Chercher les effets avec la priorité maximale
        bool left_active = false;
        bool right_active = false;
        bool full_active = false;
        int left_slot = -1;
        int right_slot = -1;
        int full_slot = -1;

        for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
            if (!active_events[i].active || active_events[i].priority != max_priority) continue;

            if (active_events[i].effect_config.zone == LED_ZONE_LEFT) {
                left_active = true;
                left_slot = i;
            } else if (active_events[i].effect_config.zone == LED_ZONE_RIGHT) {
                right_active = true;
                right_slot = i;
            } else if (active_events[i].effect_config.zone == LED_ZONE_FULL) {
                full_active = true;
                full_slot = i;
            }
        }

        // Priorité au FULL si présent, sinon combiner LEFT/RIGHT
        if (full_active) {
            // Effet FULL avec la plus haute priorité
            led_effects_set_config(&active_events[full_slot].effect_config);
            effect_override_active = true;
        } else if (left_active && right_active) {
            // Deux effets directionnels avec la même priorité maximale
            led_effects_set_dual_directional(
                &active_events[left_slot].effect_config,
                &active_events[right_slot].effect_config
            );
            effect_override_active = true;
        } else if (left_active) {
            // Seulement gauche
            led_effects_set_config(&active_events[left_slot].effect_config);
            effect_override_active = true;
        } else if (right_active) {
            // Seulement droite
            led_effects_set_config(&active_events[right_slot].effect_config);
            effect_override_active = true;
        } else {
            // Pas d'effets avec la priorité maximale (?), appliquer le premier actif
            int highest_priority = -1;
            int highest_priority_slot = -1;

            for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
                if (active_events[i].active && active_events[i].priority > highest_priority) {
                    highest_priority = active_events[i].priority;
                    highest_priority_slot = i;
                }
            }

            if (highest_priority_slot >= 0) {
                led_effects_set_config(&active_events[highest_priority_slot].effect_config);
                effect_override_active = true;
            }
        }
    } else if (!any_active && effect_override_active) {
        // Plus aucun événement actif, retour à l'effet par défaut
        if (active_profile_id >= 0) {
            led_effects_set_config(&profiles[active_profile_id].default_effect);
        }
        effect_override_active = false;
        ESP_LOGD(TAG, "Retour à l'effet par défaut");
    }
}

const char* config_manager_get_event_name(can_event_type_t event) {
    if (event >= 0 && event < CAN_EVENT_MAX) {
        return event_names[event];
    }
    return "Unknown";
}

// Table de correspondance enum -> ID alphanumérique
const char* config_manager_enum_to_id(can_event_type_t event) {
    switch (event) {
        case CAN_EVENT_NONE:                return EVENT_ID_NONE;
        case CAN_EVENT_TURN_LEFT:           return EVENT_ID_TURN_LEFT;
        case CAN_EVENT_TURN_RIGHT:          return EVENT_ID_TURN_RIGHT;
        case CAN_EVENT_TURN_HAZARD:         return EVENT_ID_TURN_HAZARD;
        case CAN_EVENT_CHARGING:            return EVENT_ID_CHARGING;
        case CAN_EVENT_CHARGE_COMPLETE:     return EVENT_ID_CHARGE_COMPLETE;
        case CAN_EVENT_CHARGING_STARTED:     return EVENT_ID_CHARGING_STARTED;
        case CAN_EVENT_CHARGING_STOPPED:     return EVENT_ID_CHARGING_STOPPED;
        case CAN_EVENT_CHARGING_CABLE_CONNECTED:     return EVENT_ID_CHARGING_CABLE_CONNECTED;
        case CAN_EVENT_CHARGING_CABLE_DISCONNECTED:     return EVENT_ID_CHARGING_CABLE_DISCONNECTED;
        case CAN_EVENT_CHARGING_PORT_OPENED:     return EVENT_ID_CHARGING_PORT_OPENED;
        case CAN_EVENT_DOOR_OPEN:           return EVENT_ID_DOOR_OPEN;
        case CAN_EVENT_DOOR_CLOSE:          return EVENT_ID_DOOR_CLOSE;
        case CAN_EVENT_LOCKED:              return EVENT_ID_LOCKED;
        case CAN_EVENT_UNLOCKED:            return EVENT_ID_UNLOCKED;
        case CAN_EVENT_BRAKE_ON:            return EVENT_ID_BRAKE_ON;
        case CAN_EVENT_BRAKE_OFF:           return EVENT_ID_BRAKE_OFF;
        case CAN_EVENT_BLINDSPOT_LEFT:      return EVENT_ID_BLINDSPOT_LEFT;
        case CAN_EVENT_BLINDSPOT_RIGHT:     return EVENT_ID_BLINDSPOT_RIGHT;
        case CAN_EVENT_BLINDSPOT_WARNING:     return EVENT_ID_BLINDSPOT_WARNING;
        case CAN_EVENT_FORWARD_COLISSION:     return EVENT_ID_EVENT_FORWARD_COLISSION;
        case CAN_EVENT_NIGHT_MODE_ON:       return EVENT_ID_NIGHT_MODE_ON;
        case CAN_EVENT_NIGHT_MODE_OFF:      return EVENT_ID_NIGHT_MODE_OFF;
        case CAN_EVENT_SPEED_THRESHOLD:     return EVENT_ID_SPEED_THRESHOLD;
        case CAN_EVENT_AUTOPILOT_ENGAGED:   return EVENT_ID_AUTOPILOT_ENGAGED;
        case CAN_EVENT_AUTOPILOT_DISENGAGED: return EVENT_ID_AUTOPILOT_DISENGAGED;
        case CAN_EVENT_AUTOPILOT_ABORTING:  return EVENT_ID_AUTOPILOT_ABORTING;
        case CAN_EVENT_GEAR_DRIVE:          return EVENT_ID_GEAR_DRIVE;
        case CAN_EVENT_GEAR_REVERSE:        return EVENT_ID_GEAR_REVERSE;
        case CAN_EVENT_GEAR_PARK:           return EVENT_ID_GEAR_PARK;
        case CAN_EVENT_SENTRY_MODE_ON:      return EVENT_ID_SENTRY_MODE_ON;
        case CAN_EVENT_SENTRY_MODE_OFF:     return EVENT_ID_SENTRY_MODE_OFF;
        case CAN_EVENT_SENTRY_ALERT:        return EVENT_ID_SENTRY_ALERT;
        default:                            return EVENT_ID_NONE;
    }
}

// Table de correspondance ID alphanumérique -> enum
can_event_type_t config_manager_id_to_enum(const char* id) {
    if (id == NULL) return CAN_EVENT_NONE;

    if (strcmp(id, EVENT_ID_NONE) == 0)                 return CAN_EVENT_NONE;
    if (strcmp(id, EVENT_ID_TURN_LEFT) == 0)            return CAN_EVENT_TURN_LEFT;
    if (strcmp(id, EVENT_ID_TURN_RIGHT) == 0)           return CAN_EVENT_TURN_RIGHT;
    if (strcmp(id, EVENT_ID_TURN_HAZARD) == 0)          return CAN_EVENT_TURN_HAZARD;
    if (strcmp(id, EVENT_ID_CHARGING) == 0)             return CAN_EVENT_CHARGING;
    if (strcmp(id, EVENT_ID_CHARGE_COMPLETE) == 0)      return CAN_EVENT_CHARGE_COMPLETE;
    if (strcmp(id, EVENT_ID_CHARGING_STARTED) == 0)      return CAN_EVENT_CHARGING_STARTED;
    if (strcmp(id, EVENT_ID_CHARGING_STOPPED) == 0)      return CAN_EVENT_CHARGING_STOPPED;
    if (strcmp(id, EVENT_ID_CHARGING_CABLE_CONNECTED) == 0)      return CAN_EVENT_CHARGING_CABLE_CONNECTED;
    if (strcmp(id, EVENT_ID_CHARGING_CABLE_DISCONNECTED) == 0)      return CAN_EVENT_CHARGING_CABLE_DISCONNECTED;
    if (strcmp(id, EVENT_ID_CHARGING_PORT_OPENED) == 0)      return CAN_EVENT_CHARGING_PORT_OPENED;
    if (strcmp(id, EVENT_ID_DOOR_OPEN) == 0)            return CAN_EVENT_DOOR_OPEN;
    if (strcmp(id, EVENT_ID_DOOR_CLOSE) == 0)           return CAN_EVENT_DOOR_CLOSE;
    if (strcmp(id, EVENT_ID_LOCKED) == 0)               return CAN_EVENT_LOCKED;
    if (strcmp(id, EVENT_ID_UNLOCKED) == 0)             return CAN_EVENT_UNLOCKED;
    if (strcmp(id, EVENT_ID_BRAKE_ON) == 0)             return CAN_EVENT_BRAKE_ON;
    if (strcmp(id, EVENT_ID_BRAKE_OFF) == 0)            return CAN_EVENT_BRAKE_OFF;
    if (strcmp(id, EVENT_ID_BLINDSPOT_LEFT) == 0)       return CAN_EVENT_BLINDSPOT_LEFT;
    if (strcmp(id, EVENT_ID_BLINDSPOT_RIGHT) == 0)      return CAN_EVENT_BLINDSPOT_RIGHT;
    if (strcmp(id, EVENT_ID_BLINDSPOT_WARNING) == 0)      return CAN_EVENT_BLINDSPOT_WARNING;
    if (strcmp(id, EVENT_ID_EVENT_FORWARD_COLISSION) == 0)      return CAN_EVENT_FORWARD_COLISSION;
    if (strcmp(id, EVENT_ID_NIGHT_MODE_ON) == 0)        return CAN_EVENT_NIGHT_MODE_ON;
    if (strcmp(id, EVENT_ID_NIGHT_MODE_OFF) == 0)       return CAN_EVENT_NIGHT_MODE_OFF;
    if (strcmp(id, EVENT_ID_SPEED_THRESHOLD) == 0)      return CAN_EVENT_SPEED_THRESHOLD;
    if (strcmp(id, EVENT_ID_AUTOPILOT_ENGAGED) == 0)    return CAN_EVENT_AUTOPILOT_ENGAGED;
    if (strcmp(id, EVENT_ID_AUTOPILOT_DISENGAGED) == 0) return CAN_EVENT_AUTOPILOT_DISENGAGED;
    if (strcmp(id, EVENT_ID_AUTOPILOT_ABORTING) == 0) return CAN_EVENT_AUTOPILOT_ABORTING;
    if (strcmp(id, EVENT_ID_GEAR_DRIVE) == 0)           return CAN_EVENT_GEAR_DRIVE;
    if (strcmp(id, EVENT_ID_GEAR_REVERSE) == 0)         return CAN_EVENT_GEAR_REVERSE;
    if (strcmp(id, EVENT_ID_GEAR_PARK) == 0)            return CAN_EVENT_GEAR_PARK;
    if (strcmp(id, EVENT_ID_SENTRY_MODE_ON) == 0)       return CAN_EVENT_SENTRY_MODE_ON;
    if (strcmp(id, EVENT_ID_SENTRY_MODE_OFF) == 0)      return CAN_EVENT_SENTRY_MODE_OFF;
    if (strcmp(id, EVENT_ID_SENTRY_ALERT) == 0)         return CAN_EVENT_SENTRY_ALERT;

    ESP_LOGW(TAG, "ID d'événement inconnu: %s", id);
    return CAN_EVENT_NONE;
}

// Vérifie si un événement peut déclencher un changement de profil
bool config_manager_event_can_switch_profile(can_event_type_t event) {
    switch (event) {
        case CAN_EVENT_DOOR_OPEN:
        case CAN_EVENT_DOOR_CLOSE:
        case CAN_EVENT_NIGHT_MODE_ON:
        case CAN_EVENT_NIGHT_MODE_OFF:
        case CAN_EVENT_CHARGING:
        case CAN_EVENT_CHARGE_COMPLETE:
        case CAN_EVENT_SPEED_THRESHOLD:
        case CAN_EVENT_LOCKED:
        case CAN_EVENT_UNLOCKED:
        case CAN_EVENT_AUTOPILOT_ENGAGED:
        case CAN_EVENT_AUTOPILOT_DISENGAGED:
        case CAN_EVENT_AUTOPILOT_ABORTING:
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
    ESP_LOGI(TAG, "Factory reset: Erasing all profiles and settings...");

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("profiles", NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for factory reset");
        return false;
    }

    // Effacer tout le namespace "profiles"
    err = nvs_erase_all(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase profiles namespace");
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit profile erasure");
        return false;
    }

    // Effacer la configuration matérielle LED
    err = nvs_open("led_config", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    // Réinitialiser les profils en RAM
    memset(profiles, 0, sizeof(profiles));
    active_profile_id = -1;

    // Créer un profil par défaut (allouer dynamiquement pour éviter stack overflow)
    config_profile_t* default_profile = (config_profile_t*)malloc(sizeof(config_profile_t));
    if (default_profile == NULL) {
        ESP_LOGE(TAG, "Erreur allocation mémoire pour profil par défaut");
        return false;
    }
    config_manager_create_default_profile(default_profile, "Default");
    profiles[0] = *default_profile;
    config_manager_save_profile(0, default_profile);
    free(default_profile);
    config_manager_activate_profile(0);

    ESP_LOGI(TAG, "Factory reset complete. Default profile created.");
    return true;
}

bool config_manager_export_profile(uint8_t profile_id, char* json_buffer, size_t buffer_size) {
    if (profile_id >= MAX_PROFILES || json_buffer == NULL) {
        return false;
    }

    config_profile_t* profile = &profiles[profile_id];

    // Vérifier que le profil existe
    if (profile->name[0] == '\0') {
        ESP_LOGW(TAG, "Profil %d inexistant, impossible d'exporter", profile_id);
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Erreur création JSON root");
        return false;
    }

    // Métadonnées
    cJSON_AddStringToObject(root, "name", profile->name);
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "created_timestamp", profile->created_timestamp);
    cJSON_AddNumberToObject(root, "modified_timestamp", profile->modified_timestamp);

    // Paramètres généraux
    cJSON_AddBoolToObject(root, "auto_night_mode", profile->auto_night_mode);
    cJSON_AddNumberToObject(root, "night_brightness", profile->night_brightness);
    cJSON_AddNumberToObject(root, "speed_threshold", profile->speed_threshold);

    // Effet par défaut
    cJSON *default_effect = cJSON_CreateObject();
    cJSON_AddNumberToObject(default_effect, "effect", profile->default_effect.effect);
    cJSON_AddNumberToObject(default_effect, "brightness", profile->default_effect.brightness);
    cJSON_AddNumberToObject(default_effect, "speed", profile->default_effect.speed);
    cJSON_AddNumberToObject(default_effect, "color1", profile->default_effect.color1);
    cJSON_AddNumberToObject(default_effect, "color2", profile->default_effect.color2);
    cJSON_AddNumberToObject(default_effect, "color3", profile->default_effect.color3);
    cJSON_AddNumberToObject(default_effect, "zone", profile->default_effect.zone);
    cJSON_AddBoolToObject(default_effect, "reverse", profile->default_effect.reverse);
    cJSON_AddItemToObject(root, "default_effect", default_effect);

    // Effet mode nuit
    cJSON *night_effect = cJSON_CreateObject();
    cJSON_AddNumberToObject(night_effect, "effect", profile->night_mode_effect.effect);
    cJSON_AddNumberToObject(night_effect, "brightness", profile->night_mode_effect.brightness);
    cJSON_AddNumberToObject(night_effect, "speed", profile->night_mode_effect.speed);
    cJSON_AddNumberToObject(night_effect, "color1", profile->night_mode_effect.color1);
    cJSON_AddNumberToObject(night_effect, "color2", profile->night_mode_effect.color2);
    cJSON_AddNumberToObject(night_effect, "color3", profile->night_mode_effect.color3);
    cJSON_AddNumberToObject(night_effect, "zone", profile->night_mode_effect.zone);
    cJSON_AddBoolToObject(night_effect, "reverse", profile->night_mode_effect.reverse);
    cJSON_AddItemToObject(root, "night_mode_effect", night_effect);

    // Événements CAN
    cJSON *events = cJSON_CreateArray();
    for (int i = 0; i < CAN_EVENT_MAX; i++) {
        if (profile->event_effects[i].enabled) {
            cJSON *event = cJSON_CreateObject();
            cJSON_AddNumberToObject(event, "event", profile->event_effects[i].event);
            cJSON_AddBoolToObject(event, "enabled", profile->event_effects[i].enabled);
            cJSON_AddNumberToObject(event, "priority", profile->event_effects[i].priority);
            cJSON_AddNumberToObject(event, "duration_ms", profile->event_effects[i].duration_ms);

            cJSON *event_effect = cJSON_CreateObject();
            cJSON_AddNumberToObject(event_effect, "effect", profile->event_effects[i].effect_config.effect);
            cJSON_AddNumberToObject(event_effect, "brightness", profile->event_effects[i].effect_config.brightness);
            cJSON_AddNumberToObject(event_effect, "speed", profile->event_effects[i].effect_config.speed);
            cJSON_AddNumberToObject(event_effect, "color1", profile->event_effects[i].effect_config.color1);
            cJSON_AddNumberToObject(event_effect, "color2", profile->event_effects[i].effect_config.color2);
            cJSON_AddNumberToObject(event_effect, "color3", profile->event_effects[i].effect_config.color3);
            cJSON_AddNumberToObject(event_effect, "zone", profile->event_effects[i].effect_config.zone);
            cJSON_AddBoolToObject(event_effect, "reverse", profile->event_effects[i].effect_config.reverse);
            cJSON_AddItemToObject(event, "effect_config", event_effect);

            cJSON_AddItemToArray(events, event);
        }
    }
    cJSON_AddItemToObject(root, "event_effects", events);

    // Convertir en chaîne JSON
    char* json_str = cJSON_Print(root);
    if (json_str) {
        size_t len = strlen(json_str);
        if (len < buffer_size) {
            strcpy(json_buffer, json_str);
            free(json_str);
            cJSON_Delete(root);
            ESP_LOGI(TAG, "Profil %d exporté avec succès (%d octets)", profile_id, len);
            return true;
        } else {
            ESP_LOGE(TAG, "Buffer trop petit: %d nécessaires, %d disponibles", len + 1, buffer_size);
            free(json_str);
        }
    }

    cJSON_Delete(root);
    return false;
}

bool config_manager_import_profile(uint8_t profile_id, const char* json_string) {
    if (profile_id >= MAX_PROFILES || json_string == NULL) {
        return false;
    }

    cJSON *root = cJSON_Parse(json_string);
    if (!root) {
        ESP_LOGE(TAG, "Erreur parsing JSON");
        return false;
    }

    // Allouer dynamiquement pour éviter stack overflow
    config_profile_t* imported_profile = (config_profile_t*)malloc(sizeof(config_profile_t));
    if (imported_profile == NULL) {
        ESP_LOGE(TAG, "Erreur allocation mémoire");
        cJSON_Delete(root);
        return false;
    }
    memset(imported_profile, 0, sizeof(config_profile_t));

    // Métadonnées
    cJSON *name = cJSON_GetObjectItem(root, "name");
    if (name && cJSON_IsString(name)) {
        strncpy(imported_profile->name, name->valuestring, PROFILE_NAME_MAX_LEN - 1);
    } else {
        ESP_LOGE(TAG, "Champ 'name' manquant ou invalide");
        cJSON_Delete(root);
        free(imported_profile);
        return false;
    }

    cJSON *created = cJSON_GetObjectItem(root, "created_timestamp");
    if (created && cJSON_IsNumber(created)) {
        imported_profile->created_timestamp = created->valueint;
    }

    cJSON *modified = cJSON_GetObjectItem(root, "modified_timestamp");
    if (modified && cJSON_IsNumber(modified)) {
        imported_profile->modified_timestamp = modified->valueint;
    }

    // Paramètres généraux
    cJSON *auto_night = cJSON_GetObjectItem(root, "auto_night_mode");
    if (auto_night && cJSON_IsBool(auto_night)) {
        imported_profile->auto_night_mode = cJSON_IsTrue(auto_night);
    }

    cJSON *night_bright = cJSON_GetObjectItem(root, "night_brightness");
    if (night_bright && cJSON_IsNumber(night_bright)) {
        imported_profile->night_brightness = night_bright->valueint;
    }

    cJSON *speed_thresh = cJSON_GetObjectItem(root, "speed_threshold");
    if (speed_thresh && cJSON_IsNumber(speed_thresh)) {
        imported_profile->speed_threshold = speed_thresh->valueint;
    }

    // Effet par défaut
    cJSON *default_effect = cJSON_GetObjectItem(root, "default_effect");
    if (default_effect && cJSON_IsObject(default_effect)) {
        cJSON *item;
        if ((item = cJSON_GetObjectItem(default_effect, "effect")) && cJSON_IsNumber(item))
            imported_profile->default_effect.effect = item->valueint;
        if ((item = cJSON_GetObjectItem(default_effect, "brightness")) && cJSON_IsNumber(item))
            imported_profile->default_effect.brightness = item->valueint;
        if ((item = cJSON_GetObjectItem(default_effect, "speed")) && cJSON_IsNumber(item))
            imported_profile->default_effect.speed = item->valueint;
        if ((item = cJSON_GetObjectItem(default_effect, "color1")) && cJSON_IsNumber(item))
            imported_profile->default_effect.color1 = item->valueint;
        if ((item = cJSON_GetObjectItem(default_effect, "color2")) && cJSON_IsNumber(item))
            imported_profile->default_effect.color2 = item->valueint;
        if ((item = cJSON_GetObjectItem(default_effect, "color3")) && cJSON_IsNumber(item))
            imported_profile->default_effect.color3 = item->valueint;
        if ((item = cJSON_GetObjectItem(default_effect, "zone")) && cJSON_IsNumber(item))
            imported_profile->default_effect.zone = item->valueint;
        if ((item = cJSON_GetObjectItem(default_effect, "reverse")) && cJSON_IsBool(item))
            imported_profile->default_effect.reverse = cJSON_IsTrue(item);
    }

    // Effet mode nuit
    cJSON *night_effect = cJSON_GetObjectItem(root, "night_mode_effect");
    if (night_effect && cJSON_IsObject(night_effect)) {
        cJSON *item;
        if ((item = cJSON_GetObjectItem(night_effect, "effect")) && cJSON_IsNumber(item))
            imported_profile->night_mode_effect.effect = item->valueint;
        if ((item = cJSON_GetObjectItem(night_effect, "brightness")) && cJSON_IsNumber(item))
            imported_profile->night_mode_effect.brightness = item->valueint;
        if ((item = cJSON_GetObjectItem(night_effect, "speed")) && cJSON_IsNumber(item))
            imported_profile->night_mode_effect.speed = item->valueint;
        if ((item = cJSON_GetObjectItem(night_effect, "color1")) && cJSON_IsNumber(item))
            imported_profile->night_mode_effect.color1 = item->valueint;
        if ((item = cJSON_GetObjectItem(night_effect, "color2")) && cJSON_IsNumber(item))
            imported_profile->night_mode_effect.color2 = item->valueint;
        if ((item = cJSON_GetObjectItem(night_effect, "color3")) && cJSON_IsNumber(item))
            imported_profile->night_mode_effect.color3 = item->valueint;
        if ((item = cJSON_GetObjectItem(night_effect, "zone")) && cJSON_IsNumber(item))
            imported_profile->night_mode_effect.zone = item->valueint;
        if ((item = cJSON_GetObjectItem(night_effect, "reverse")) && cJSON_IsBool(item))
            imported_profile->night_mode_effect.reverse = cJSON_IsTrue(item);
    }

    // Événements CAN
    cJSON *events = cJSON_GetObjectItem(root, "event_effects");
    if (events && cJSON_IsArray(events)) {
        cJSON *event = NULL;
        cJSON_ArrayForEach(event, events) {
            cJSON *event_type = cJSON_GetObjectItem(event, "event");
            if (!event_type || !cJSON_IsNumber(event_type)) continue;

            int evt = event_type->valueint;
            if (evt < 0 || evt >= CAN_EVENT_MAX) continue;

            cJSON *enabled = cJSON_GetObjectItem(event, "enabled");
            if (enabled && cJSON_IsBool(enabled)) {
                imported_profile->event_effects[evt].enabled = cJSON_IsTrue(enabled);
            }

            cJSON *priority = cJSON_GetObjectItem(event, "priority");
            if (priority && cJSON_IsNumber(priority)) {
                imported_profile->event_effects[evt].priority = priority->valueint;
            }

            cJSON *duration = cJSON_GetObjectItem(event, "duration_ms");
            if (duration && cJSON_IsNumber(duration)) {
                imported_profile->event_effects[evt].duration_ms = duration->valueint;
            }

            imported_profile->event_effects[evt].event = evt;

            cJSON *effect_config = cJSON_GetObjectItem(event, "effect_config");
            if (effect_config && cJSON_IsObject(effect_config)) {
                cJSON *item;
                if ((item = cJSON_GetObjectItem(effect_config, "effect")) && cJSON_IsNumber(item))
                    imported_profile->event_effects[evt].effect_config.effect = item->valueint;
                if ((item = cJSON_GetObjectItem(effect_config, "brightness")) && cJSON_IsNumber(item))
                    imported_profile->event_effects[evt].effect_config.brightness = item->valueint;
                if ((item = cJSON_GetObjectItem(effect_config, "speed")) && cJSON_IsNumber(item))
                    imported_profile->event_effects[evt].effect_config.speed = item->valueint;
                if ((item = cJSON_GetObjectItem(effect_config, "color1")) && cJSON_IsNumber(item))
                    imported_profile->event_effects[evt].effect_config.color1 = item->valueint;
                if ((item = cJSON_GetObjectItem(effect_config, "color2")) && cJSON_IsNumber(item))
                    imported_profile->event_effects[evt].effect_config.color2 = item->valueint;
                if ((item = cJSON_GetObjectItem(effect_config, "color3")) && cJSON_IsNumber(item))
                    imported_profile->event_effects[evt].effect_config.color3 = item->valueint;
                if ((item = cJSON_GetObjectItem(effect_config, "zone")) && cJSON_IsNumber(item))
                    imported_profile->event_effects[evt].effect_config.zone = item->valueint;
                if ((item = cJSON_GetObjectItem(effect_config, "reverse")) && cJSON_IsBool(item))
                    imported_profile->event_effects[evt].effect_config.reverse = cJSON_IsTrue(item);
            }
        }
    }

    cJSON_Delete(root);

    // Sauvegarder le profil importé
    bool success = config_manager_save_profile(profile_id, imported_profile);
    if (success) {
        ESP_LOGI(TAG, "Profil %d importé avec succès: %s", profile_id, imported_profile->name);
    } else {
        ESP_LOGE(TAG, "Erreur lors de la sauvegarde du profil %d", profile_id);
    }

    free(imported_profile);
    return success;
}

bool config_manager_get_effect_for_event(can_event_type_t event, can_event_effect_t* event_effect) {
    if (active_profile_id < 0 || active_profile_id >= MAX_PROFILES ||
        event >= CAN_EVENT_MAX || event_effect == NULL) {
        return false;
    }

    memcpy(event_effect, &profiles[active_profile_id].event_effects[event],
           sizeof(can_event_effect_t));
    return true;
}

uint16_t config_manager_get_led_count(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("led_hw", NVS_READONLY, &nvs_handle);

    if (err != ESP_OK) {
        return NUM_LEDS; // Valeur par défaut
    }

    uint16_t led_count;
    err = nvs_get_u16(nvs_handle, "led_count", &led_count);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        return NUM_LEDS; // Valeur par défaut
    }

    return led_count;
}

uint8_t config_manager_get_led_pin(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("led_hw", NVS_READONLY, &nvs_handle);

    if (err != ESP_OK) {
        return LED_PIN; // Valeur par défaut
    }

    uint8_t data_pin;
    err = nvs_get_u8(nvs_handle, "data_pin", &data_pin);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        return LED_PIN; // Valeur par défaut
    }

    return data_pin;
}

bool config_manager_set_led_hardware(uint16_t led_count, uint8_t data_pin) {
    // Validation
    if (led_count < 1 || led_count > 1000) {
        ESP_LOGE(TAG, "Nombre de LEDs invalide: %d (1-1000)", led_count);
        return false;
    }

    if (data_pin > 39) {
        ESP_LOGE(TAG, "Pin GPIO invalide: %d (0-39)", data_pin);
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("led_hw", NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erreur ouverture NVS: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u16(nvs_handle, "led_count", led_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erreur sauvegarde led_count: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_set_u8(nvs_handle, "data_pin", data_pin);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erreur sauvegarde data_pin: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Configuration matérielle LED sauvegardée: %d LEDs, GPIO %d",
             led_count, data_pin);
    return true;
}
