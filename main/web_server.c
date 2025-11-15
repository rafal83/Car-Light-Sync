#include "web_server.h"
#include "config.h"
#include "led_effects.h"
#include "wifi_manager.h"
#include "commander.h"
#include "tesla_can.h"
#include "config_manager.h"
#include "ota_update.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "WebServer";

static httpd_handle_t server = NULL;
static vehicle_state_t current_vehicle_state = {0};

// HTML de la page principale (embarqué, version compressée GZIP)
extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]   asm("_binary_index_html_gz_end");

// Handler pour la page principale
static esp_err_t index_handler(httpd_req_t *req) {
    const size_t index_html_gz_size = (index_html_gz_end - index_html_gz_start);

    // Définir les en-têtes HTTP
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip"); // Indiquer que c'est compressé
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000"); // Cache 1 an

    ESP_LOGI(TAG, "Envoi de la page HTML compressée (%d octets)", index_html_gz_size);

    // Envoyer directement le contenu compressé (petit fichier, pas besoin de chunks)
    esp_err_t err = httpd_resp_send(req, (const char *)index_html_gz_start, index_html_gz_size);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Page HTML envoyée avec succès");
    } else {
        ESP_LOGE(TAG, "Erreur envoi HTML: %s", esp_err_to_name(err));
    }

    return err;
}

// Handler pour obtenir le statut
static esp_err_t status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    
    // Statut WiFi
    wifi_status_t wifi_status;
    wifi_manager_get_status(&wifi_status);
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_status.sta_connected);
    cJSON_AddStringToObject(root, "wifi_ip", wifi_status.sta_ip);
    
    // Statut Commander
    commander_status_t cmd_status;
    commander_get_status(&cmd_status);
    cJSON_AddBoolToObject(root, "commander_connected", cmd_status.connected);
    
    // Statut véhicule
    uint32_t now = xTaskGetTickCount();
    bool vehicle_active = (now - current_vehicle_state.last_update) < pdMS_TO_TICKS(5000);
    cJSON_AddBoolToObject(root, "vehicle_active", vehicle_active);
    
    // Données véhicule complètes
    cJSON *vehicle = cJSON_CreateObject();

    // État général
    cJSON_AddBoolToObject(vehicle, "ignition_on", current_vehicle_state.ignition_on);
    cJSON_AddNumberToObject(vehicle, "gear", current_vehicle_state.gear);
    cJSON_AddNumberToObject(vehicle, "speed", current_vehicle_state.speed_kmh);
    cJSON_AddBoolToObject(vehicle, "brake_pressed", current_vehicle_state.brake_pressed);

    // Portes
    cJSON *doors = cJSON_CreateObject();
    cJSON_AddBoolToObject(doors, "front_left", current_vehicle_state.door_fl);
    cJSON_AddBoolToObject(doors, "front_right", current_vehicle_state.door_fr);
    cJSON_AddBoolToObject(doors, "rear_left", current_vehicle_state.door_rl);
    cJSON_AddBoolToObject(doors, "rear_right", current_vehicle_state.door_rr);
    cJSON_AddBoolToObject(doors, "trunk", current_vehicle_state.trunk_open);
    cJSON_AddBoolToObject(doors, "frunk", current_vehicle_state.frunk_open);
    int doors_open = 0;
    if (current_vehicle_state.door_fl) doors_open++;
    if (current_vehicle_state.door_fr) doors_open++;
    if (current_vehicle_state.door_rl) doors_open++;
    if (current_vehicle_state.door_rr) doors_open++;
    cJSON_AddNumberToObject(doors, "count_open", doors_open);
    cJSON_AddItemToObject(vehicle, "doors", doors);

    // Verrouillage
    cJSON_AddBoolToObject(vehicle, "locked", current_vehicle_state.locked);

    // Fenêtres (0-100%)
    cJSON *windows = cJSON_CreateObject();
    cJSON_AddNumberToObject(windows, "front_left", current_vehicle_state.window_fl);
    cJSON_AddNumberToObject(windows, "front_right", current_vehicle_state.window_fr);
    cJSON_AddNumberToObject(windows, "rear_left", current_vehicle_state.window_rl);
    cJSON_AddNumberToObject(windows, "rear_right", current_vehicle_state.window_rr);
    cJSON_AddItemToObject(vehicle, "windows", windows);

    // Lumières
    cJSON *lights = cJSON_CreateObject();
    cJSON_AddBoolToObject(lights, "headlights", current_vehicle_state.headlights_on);
    cJSON_AddBoolToObject(lights, "high_beams", current_vehicle_state.high_beams_on);
    cJSON_AddBoolToObject(lights, "fog_lights", current_vehicle_state.fog_lights_on);
    cJSON_AddNumberToObject(lights, "turn_signal", current_vehicle_state.turn_signal);
    cJSON_AddItemToObject(vehicle, "lights", lights);

    // Charge
    cJSON *charge = cJSON_CreateObject();
    cJSON_AddBoolToObject(charge, "charging", current_vehicle_state.charging);
    cJSON_AddNumberToObject(charge, "percent", current_vehicle_state.charge_percent);
    cJSON_AddNumberToObject(charge, "power_kw", current_vehicle_state.charge_power_kw);
    cJSON_AddItemToObject(vehicle, "charge", charge);

    // Batterie et autres
    cJSON_AddNumberToObject(vehicle, "battery_12v", current_vehicle_state.battery_voltage);
    cJSON_AddNumberToObject(vehicle, "odometer_km", current_vehicle_state.odometer_km);

    // Sécurité
    cJSON *safety = cJSON_CreateObject();
    cJSON_AddBoolToObject(safety, "blindspot_left", current_vehicle_state.blindspot_left);
    cJSON_AddBoolToObject(safety, "blindspot_right", current_vehicle_state.blindspot_right);
    cJSON_AddBoolToObject(safety, "night_mode", current_vehicle_state.night_mode);
    cJSON_AddItemToObject(vehicle, "safety", safety);

    cJSON_AddItemToObject(root, "vehicle", vehicle);

    // Profil actuellement appliqu�� (peut changer temporairement via ��v��nements)
    int active_profile_id = config_manager_get_active_profile_id();
    cJSON_AddNumberToObject(root, "active_profile_id", active_profile_id);
    config_profile_t *active_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
    if (active_profile && config_manager_get_active_profile(active_profile)) {
        cJSON_AddStringToObject(root, "active_profile_name", active_profile->name);
    } else {
        cJSON_AddStringToObject(root, "active_profile_name", "None");
    }
    if (active_profile) {
        free(active_profile);
    }
    
    const char *json_string = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    
    free((void *)json_string);
    cJSON_Delete(root);
    
    return ESP_OK;
}

// Handler pour obtenir la configuration
static esp_err_t config_handler(httpd_req_t *req) {
    effect_config_t config;
    led_effects_get_config(&config);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "effect", config.effect);
    cJSON_AddNumberToObject(root, "brightness", config.brightness);
    cJSON_AddNumberToObject(root, "speed", config.speed);
    cJSON_AddNumberToObject(root, "color1", config.color1);
    cJSON_AddNumberToObject(root, "color2", config.color2);
    cJSON_AddNumberToObject(root, "color3", config.color3);
    cJSON_AddNumberToObject(root, "sync_mode", config.sync_mode);
    cJSON_AddBoolToObject(root, "reverse", config.reverse);

    // Ajouter les paramètres du profil actif (allouer dynamiquement pour éviter stack overflow)
    config_profile_t *profile = (config_profile_t*)malloc(sizeof(config_profile_t));
    if (profile != NULL && config_manager_get_active_profile(profile)) {
        cJSON_AddBoolToObject(root, "auto_night_mode", profile->auto_night_mode);
        cJSON_AddNumberToObject(root, "night_brightness", profile->night_brightness);
        free(profile);
    } else if (profile != NULL) {
        free(profile);
    }

    // Ajouter la configuration matérielle LED
    cJSON_AddNumberToObject(root, "led_count", config_manager_get_led_count());
    cJSON_AddNumberToObject(root, "data_pin", config_manager_get_led_pin());

    // Ajouter la configuration du sens de la strip
    cJSON_AddBoolToObject(root, "strip_reverse", led_effects_get_reverse());

    const char *json_string = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);

    free((void *)json_string);
    cJSON_Delete(root);

    return ESP_OK;
}

// Handler pour définir l'effet
static esp_err_t effect_handler(httpd_req_t *req) {
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    
    content[ret] = '\0';
    
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    effect_config_t config;
    led_effects_get_config(&config);
    
    cJSON *effect = cJSON_GetObjectItem(root, "effect");
    if (effect) config.effect = effect->valueint;
    
    cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
    if (brightness) config.brightness = brightness->valueint;
    
    cJSON *speed = cJSON_GetObjectItem(root, "speed");
    if (speed) config.speed = speed->valueint;
    
    cJSON *color1 = cJSON_GetObjectItem(root, "color1");
    if (color1) config.color1 = color1->valueint;
    
    cJSON *color2 = cJSON_GetObjectItem(root, "color2");
    if (color2) config.color2 = color2->valueint;
    
    cJSON *color3 = cJSON_GetObjectItem(root, "color3");
    if (color3) config.color3 = color3->valueint;
    
    led_effects_set_config(&config);
    
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    
    ESP_LOGI(TAG, "Effet configuré: %d", config.effect);
    
    return ESP_OK;
}

// Handler pour sauvegarder la configuration
static esp_err_t save_handler(httpd_req_t *req) {
    bool success = led_effects_save_config();

    if (success) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");
    }

    return ESP_OK;
}

// Handler pour configurer le matériel LED (POST)
static esp_err_t config_post_handler(httpd_req_t *req) {
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    content[ret] = '\0';

    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *led_count_json = cJSON_GetObjectItem(root, "led_count");
    cJSON *data_pin_json = cJSON_GetObjectItem(root, "data_pin");
    cJSON *strip_reverse_json = cJSON_GetObjectItem(root, "strip_reverse");

    if (led_count_json == NULL || data_pin_json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing led_count or data_pin");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    uint16_t led_count = (uint16_t)led_count_json->valueint;
    uint8_t data_pin = (uint8_t)data_pin_json->valueint;

    // Appliquer le strip_reverse si présent
    if (strip_reverse_json != NULL) {
        led_effects_set_reverse(cJSON_IsTrue(strip_reverse_json));
    }

    // Validation
    if (led_count < 1 || led_count > 1000) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "led_count must be 1-1000");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    if (data_pin > 39) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "data_pin must be 0-39");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Sauvegarder en NVS
    bool success = config_manager_set_led_hardware(led_count, data_pin);

    cJSON_Delete(root);

    if (success) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "Configuration saved. Restart required to apply changes.");
        cJSON_AddBoolToObject(response, "restart_required", true);

        const char *json_string = cJSON_Print(response);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json_string);

        free((void *)json_string);
        cJSON_Delete(response);

        ESP_LOGI(TAG, "Configuration LED mise à jour: %d LEDs, GPIO %d", led_count, data_pin);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save configuration");
    }

    return ESP_OK;
}

// Handler pour lister les profils
static esp_err_t profiles_handler(httpd_req_t *req) {
    // Allouer dynamiquement pour éviter stack overflow (10 profils × 1900 bytes = 19KB!)
    config_profile_t *profiles = (config_profile_t*)malloc(MAX_PROFILES * sizeof(config_profile_t));
    if (profiles == NULL) {
        ESP_LOGE(TAG, "Erreur allocation mémoire pour profils");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    int count = config_manager_list_profiles(profiles, MAX_PROFILES);
    int active_id = config_manager_get_active_profile_id();

    cJSON *root = cJSON_CreateObject();
    cJSON *profiles_array = cJSON_CreateArray();

    for (int i = 0; i < count; i++) {
        cJSON *profile_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(profile_obj, "id", i);
        cJSON_AddStringToObject(profile_obj, "name", profiles[i].name);
        cJSON_AddBoolToObject(profile_obj, "active", (i == active_id));

        // Ajouter l'effet par défaut
        cJSON *default_effect_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(default_effect_obj, "effect", profiles[i].default_effect.effect);
        cJSON_AddNumberToObject(default_effect_obj, "brightness", profiles[i].default_effect.brightness);
        cJSON_AddNumberToObject(default_effect_obj, "speed", profiles[i].default_effect.speed);
        cJSON_AddNumberToObject(default_effect_obj, "color1", profiles[i].default_effect.color1);
        cJSON_AddItemToObject(profile_obj, "default_effect", default_effect_obj);

        cJSON_AddItemToArray(profiles_array, profile_obj);
    }

    cJSON_AddItemToObject(root, "profiles", profiles_array);
    cJSON_AddStringToObject(root, "active_name",
                            (active_id >= 0) ? profiles[active_id].name : "None");

    const char *json_string = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);

    free((void *)json_string);
    cJSON_Delete(root);
    free(profiles);

    return ESP_OK;
}

// Handler pour activer un profil
static esp_err_t profile_activate_handler(httpd_req_t *req) {
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    
    content[ret] = '\0';
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *profile_id = cJSON_GetObjectItem(root, "profile_id");
    if (profile_id) {
        bool success = config_manager_activate_profile(profile_id->valueint);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, success ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing profile_id");
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler pour créer un nouveau profil
static esp_err_t profile_create_handler(httpd_req_t *req) {
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }

    content[ret] = '\0';
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *name = cJSON_GetObjectItem(root, "name");
    if (name == NULL || name->valuestring == NULL) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing name");
        return ESP_FAIL;
    }

    // Allouer dynamiquement pour éviter le stack overflow
    config_profile_t *temp = (config_profile_t*)malloc(sizeof(config_profile_t));
    config_profile_t *new_profile = (config_profile_t*)malloc(sizeof(config_profile_t));

    if (temp == NULL || new_profile == NULL) {
        ESP_LOGE(TAG, "Échec allocation mémoire pour profil");
        if (temp) free(temp);
        if (new_profile) free(new_profile);
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    // Trouver un slot libre
    bool slot_found = false;
    for (int i = 0; i < MAX_PROFILES; i++) {
        if (!config_manager_load_profile(i, temp)) {
            // Slot libre
            ESP_LOGI(TAG, "Création profil '%s' dans slot %d", name->valuestring, i);
            config_manager_create_default_profile(new_profile, name->valuestring);

            bool saved = config_manager_save_profile(i, new_profile);
            free(temp);
            free(new_profile);
            cJSON_Delete(root);

            if (saved) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
                return ESP_OK;
            } else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save profile");
                return ESP_FAIL;
            }
        }
    }

    // Aucun slot libre
    free(temp);
    free(new_profile);
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No free slots");
    return ESP_FAIL;
}

// Handler pour supprimer un profil
static esp_err_t profile_delete_handler(httpd_req_t *req) {
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    
    content[ret] = '\0';
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *profile_id = cJSON_GetObjectItem(root, "profile_id");
    if (profile_id) {
        bool success = config_manager_delete_profile(profile_id->valueint);
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "status", success ? "ok" : "error");
        if (!success) {
            cJSON_AddStringToObject(response, "message", "Profile in use by an event or deletion failed");
        }
        const char *json_string = cJSON_Print(response);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json_string);
        free((void *)json_string);
        cJSON_Delete(response);
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing profile_id");
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler pour factory reset
static esp_err_t factory_reset_handler(httpd_req_t *req) {
    bool success = config_manager_factory_reset();

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", success ? "ok" : "error");
    if (success) {
        cJSON_AddStringToObject(response, "message", "Factory reset successful. Device will restart.");
    } else {
        cJSON_AddStringToObject(response, "message", "Factory reset failed");
    }

    const char *json_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    free((void *)json_string);
    cJSON_Delete(response);

    // Redémarrer l'ESP32 après un court délai
    if (success) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    return ESP_OK;
}

// Handler pour mettre à jour les paramètres d'un profil
static esp_err_t profile_update_handler(httpd_req_t *req) {
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }

    content[ret] = '\0';
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *profile_id_json = cJSON_GetObjectItem(root, "profile_id");
    if (!profile_id_json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing profile_id");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    uint8_t profile_id = (uint8_t)profile_id_json->valueint;

    // Allouer dynamiquement pour éviter stack overflow
    config_profile_t *profile = (config_profile_t*)malloc(sizeof(config_profile_t));
    if (profile == NULL) {
        ESP_LOGE(TAG, "Erreur allocation mémoire");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    if (!config_manager_load_profile(profile_id, profile)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Profile not found");
        free(profile);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Mettre à jour les paramètres si fournis
    cJSON *auto_night_mode = cJSON_GetObjectItem(root, "auto_night_mode");
    if (auto_night_mode) {
        profile->auto_night_mode = cJSON_IsTrue(auto_night_mode);
    }

    cJSON *night_brightness = cJSON_GetObjectItem(root, "night_brightness");
    if (night_brightness) {
        profile->night_brightness = (uint8_t)night_brightness->valueint;
    }

    // Sauvegarder le profil
    bool success = config_manager_save_profile(profile_id, profile);

    // Si c'est le profil actif, mettre à jour la configuration du mode nuit mais ne pas l'activer
    // Le mode nuit sera activé/désactivé par les événements CAN
    if (success && config_manager_get_active_profile_id() == profile_id) {
        // On garde l'état actuel du mode nuit mais on met à jour la luminosité
        bool current_night_mode = led_effects_get_night_mode();
        led_effects_set_night_mode(current_night_mode, profile->night_brightness);
        ESP_LOGI(TAG, "Updated night mode settings (auto: %s, brightness: %d, current state: %s)",
                 profile->auto_night_mode ? "ENABLED" : "DISABLED",
                 profile->night_brightness,
                 current_night_mode ? "ON" : "OFF");
    }

    free(profile);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, success ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");

    cJSON_Delete(root);
    return ESP_OK;
}

// Handler pour mettre à jour l'effet par défaut d'un profil
static esp_err_t profile_update_default_handler(httpd_req_t *req) {
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }

    content[ret] = '\0';
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *profile_id_json = cJSON_GetObjectItem(root, "profile_id");
    if (!profile_id_json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing profile_id");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    uint8_t profile_id = (uint8_t)profile_id_json->valueint;

    // Allouer dynamiquement pour éviter stack overflow
    config_profile_t *profile = (config_profile_t*)malloc(sizeof(config_profile_t));
    if (profile == NULL) {
        ESP_LOGE(TAG, "Erreur allocation mémoire");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    if (!config_manager_load_profile(profile_id, profile)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Profile not found");
        free(profile);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Mettre à jour l'effet par défaut
    cJSON *effect_json = cJSON_GetObjectItem(root, "effect");
    cJSON *brightness_json = cJSON_GetObjectItem(root, "brightness");
    cJSON *speed_json = cJSON_GetObjectItem(root, "speed");
    cJSON *color_json = cJSON_GetObjectItem(root, "color1");

    if (effect_json) {
        profile->default_effect.effect = (led_effect_t)effect_json->valueint;
    }
    if (brightness_json) {
        profile->default_effect.brightness = (uint8_t)brightness_json->valueint;
    }
    if (speed_json) {
        profile->default_effect.speed = (uint8_t)speed_json->valueint;
    }
    if (color_json) {
        profile->default_effect.color1 = (uint32_t)color_json->valueint;
    }

    // Sauvegarder le profil
    bool success = config_manager_save_profile(profile_id, profile);

    // Si c'est le profil actif, appliquer l'effet immédiatement
    if (success && profile_id == config_manager_get_active_profile_id()) {
        led_effects_set_config(&profile->default_effect);
        // Garder l'état actuel du mode nuit (contrôlé par les événements CAN)
        bool current_night_mode = led_effects_get_night_mode();
        led_effects_set_night_mode(current_night_mode, profile->night_brightness);
        ESP_LOGI(TAG, "Applied default effect (night mode state: %s, brightness=%d)",
                 current_night_mode ? "ON" : "OFF", profile->night_brightness);
    }

    free(profile);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, success ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");

    cJSON_Delete(root);
    return ESP_OK;
}

// Handler pour assigner un effet à un événement
static esp_err_t event_effect_handler(httpd_req_t *req) {
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    
    content[ret] = '\0';
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    int profile_id = config_manager_get_active_profile_id();
    if (profile_id < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No active profile");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    cJSON *event = cJSON_GetObjectItem(root, "event");
    cJSON *effect = cJSON_GetObjectItem(root, "effect");
    cJSON *duration = cJSON_GetObjectItem(root, "duration");
    cJSON *priority = cJSON_GetObjectItem(root, "priority");
    cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
    cJSON *speed = cJSON_GetObjectItem(root, "speed");
    cJSON *color1 = cJSON_GetObjectItem(root, "color1");
    
    if (event && effect) {
        effect_config_t effect_config;
        effect_config.effect = effect->valueint;
        effect_config.brightness = brightness ? brightness->valueint : 128;
        effect_config.speed = speed ? speed->valueint : 50;
        effect_config.color1 = color1 ? color1->valueint : 0xFF0000;
        
        bool success = config_manager_set_event_effect(
            profile_id,
            event->valueint,
            &effect_config,
            duration ? duration->valueint : 0,
            priority ? priority->valueint : 100
        );
        
        if (success) {
            // Sauvegarder le profil (allouer dynamiquement pour éviter stack overflow)
            config_profile_t *profile = (config_profile_t*)malloc(sizeof(config_profile_t));
            if (profile != NULL) {
                if (config_manager_get_active_profile(profile)) {
                    config_manager_save_profile(profile_id, profile);
                }
                free(profile);
            }
        }
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, success ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler pour exporter un profil en JSON
static esp_err_t profile_export_handler(httpd_req_t *req) {
    // Récupérer le profile_id depuis les paramètres de query
    char query[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing profile_id parameter");
        return ESP_FAIL;
    }

    char param_value[16];
    if (httpd_query_key_value(query, "profile_id", param_value, sizeof(param_value)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing profile_id parameter");
        return ESP_FAIL;
    }

    uint8_t profile_id = atoi(param_value);

    // Allouer un buffer pour le JSON (8KB devrait suffire pour un profil complet)
    char* json_buffer = malloc(8192);
    if (!json_buffer) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    bool success = config_manager_export_profile(profile_id, json_buffer, 8192);

    if (success) {
        // Envoyer le JSON avec les bons headers pour le téléchargement
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=profile.json");
        httpd_resp_sendstr(req, json_buffer);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Export failed");
    }

    free(json_buffer);
    return ESP_OK;
}

// Handler pour importer un profil depuis JSON
static esp_err_t profile_import_handler(httpd_req_t *req) {
    // Allouer un buffer pour recevoir le JSON
    char* content = malloc(8192);
    if (!content) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, content, 8192 - 1);

    if (ret <= 0) {
        free(content);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }

    content[ret] = '\0';

    // Parser pour obtenir le profile_id et le JSON
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        free(content);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *profile_id_json = cJSON_GetObjectItem(root, "profile_id");
    cJSON *profile_data = cJSON_GetObjectItem(root, "profile_data");

    if (!profile_id_json || !profile_data) {
        cJSON_Delete(root);
        free(content);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing profile_id or profile_data");
        return ESP_FAIL;
    }

    uint8_t profile_id = (uint8_t)profile_id_json->valueint;

    // Convertir profile_data en chaîne JSON
    char* profile_json = cJSON_PrintUnformatted(profile_data);
    if (!profile_json) {
        cJSON_Delete(root);
        free(content);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to serialize profile");
        return ESP_FAIL;
    }

    bool success = config_manager_import_profile(profile_id, profile_json);

    free(profile_json);
    cJSON_Delete(root);
    free(content);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, success ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");

    return ESP_OK;
}

// Handler pour connecter au Commander
static esp_err_t commander_connect_handler(httpd_req_t *req) {
    // Se connecter au WiFi S3XY_OBD
    esp_err_t ret = wifi_manager_connect_sta(PANDA_WIFI_SSID, PANDA_WIFI_PASSWORD);
    
    if (ret == ESP_OK) {
        // Attendre la connexion WiFi
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // Se connecter au Commander à l'adresse fixe
        ret = commander_connect(COMMANDER_IP, COMMANDER_PORT);
    }
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", (ret == ESP_OK) ? "ok" : "error");
    
    const char *json_string = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    
    free((void *)json_string);
    cJSON_Delete(root);
    
    return ESP_OK;
}

// Handler pour déconnecter du Commander
static esp_err_t commander_disconnect_handler(httpd_req_t *req) {
    commander_disconnect();
    wifi_manager_disconnect_sta();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    
    return ESP_OK;
}

// Callback pour les frames CAN reçues
static void can_frame_callback(const can_frame_t* frame, void* user_data) {
    process_can_frame(frame, &current_vehicle_state);
    led_effects_update_vehicle_state(&current_vehicle_state);
}

// Handler pour obtenir les informations OTA
static esp_err_t ota_info_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "version", ota_get_current_version());

    ota_progress_t progress;
    ota_get_progress(&progress);

    cJSON_AddNumberToObject(root, "state", progress.state);
    cJSON_AddNumberToObject(root, "progress", progress.progress);
    cJSON_AddNumberToObject(root, "written_size", progress.written_size);
    cJSON_AddNumberToObject(root, "total_size", progress.total_size);

    if (strlen(progress.error_msg) > 0) {
        cJSON_AddStringToObject(root, "error", progress.error_msg);
    }

    const char *json_string = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);

    free((void *)json_string);
    cJSON_Delete(root);

    return ESP_OK;
}

// Handler pour uploader le firmware OTA
static esp_err_t ota_upload_handler(httpd_req_t *req) {
    char buf[1024];
    int remaining = req->content_len;
    int received;
    bool first_chunk = true;
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Début upload OTA, taille: %d octets", remaining);

    // Démarrer l'OTA
    ret = ota_begin();
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    // Recevoir et écrire les données
    while (remaining > 0) {
        // Lire les données du socket
        received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                // Réessayer
                continue;
            }
            ESP_LOGE(TAG, "Erreur réception données OTA");
            ota_abort();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
            return ESP_FAIL;
        }

        // Écrire dans la partition OTA
        ret = ota_write(buf, received);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Erreur écriture OTA");
            ota_abort();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            return ESP_FAIL;
        }

        remaining -= received;

        if (first_chunk) {
            ESP_LOGI(TAG, "Premier chunk reçu et écrit");
            first_chunk = false;
        }
    }

    // Terminer l'OTA
    ret = ota_end();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur fin OTA");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Upload OTA terminé avec succès");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Upload successful, restart to apply\"}");

    return ESP_OK;
}

// Handler pour redémarrer l'ESP32
static esp_err_t ota_restart_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Restarting...\"}");

    ESP_LOGI(TAG, "Redémarrage demandé via API");

    // Redémarrer après un court délai
    vTaskDelay(pdMS_TO_TICKS(1000));
    ota_restart();

    return ESP_OK;
}

// Handler pour arrêter un événement CAN
static esp_err_t stop_event_handler(httpd_req_t *req) {
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }

    content[ret] = '\0';
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *event_item = cJSON_GetObjectItem(root, "event");
    if (event_item == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing event parameter");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    can_event_type_t event = CAN_EVENT_NONE;
    if (cJSON_IsString(event_item)) {
        event = config_manager_id_to_enum(event_item->valuestring);
    } else if (cJSON_IsNumber(event_item)) {
        // Support legacy payloads that still send a numeric enum
        event = (can_event_type_t)event_item->valueint;
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid event parameter");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Arrêter l'événement
    config_manager_stop_event(event);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");

    cJSON_Delete(root);
    return ESP_OK;
}

// Handler pour obtenir la liste des effets disponibles
static esp_err_t effects_list_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON *effects = cJSON_CreateArray();

    // Parcourir tous les effets disponibles
    for (int i = 0; i < EFFECT_MAX; i++) {
        cJSON *effect = cJSON_CreateObject();
        cJSON_AddStringToObject(effect, "id", led_effects_enum_to_id((led_effect_t)i));
        cJSON_AddStringToObject(effect, "name", led_effects_get_name((led_effect_t)i));
        cJSON_AddBoolToObject(effect, "can_required", led_effects_requires_can((led_effect_t)i));
        cJSON_AddItemToArray(effects, effect);
    }

    cJSON_AddItemToObject(root, "effects", effects);

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler pour obtenir la liste des types d'événements CAN
static esp_err_t event_types_list_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON *event_types = cJSON_CreateArray();

    // Parcourir tous les types d'événements CAN
    for (int i = 0; i < CAN_EVENT_MAX; i++) {
        cJSON *event_type = cJSON_CreateObject();
        cJSON_AddStringToObject(event_type, "id", config_manager_enum_to_id((can_event_type_t)i));
        cJSON_AddStringToObject(event_type, "name", config_manager_get_event_name((can_event_type_t)i));
        cJSON_AddItemToArray(event_types, event_type);
    }

    cJSON_AddItemToObject(root, "event_types", event_types);

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler pour simuler un événement CAN
static esp_err_t simulate_event_handler(httpd_req_t *req) {
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }

    content[ret] = '\0';
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *event_item = cJSON_GetObjectItem(root, "event");
    if (event_item == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing event parameter");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    can_event_type_t event = CAN_EVENT_NONE;
    if (cJSON_IsString(event_item)) {
        event = config_manager_id_to_enum(event_item->valuestring);
    } else if (cJSON_IsNumber(event_item)) {
        // Support legacy payloads that still send numeric enums
        event = (can_event_type_t)event_item->valueint;
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid event parameter");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Vérifier que l'événement est valide
    if (event <= CAN_EVENT_NONE || event >= CAN_EVENT_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid event type");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Traiter l'événement
    bool success = config_manager_process_can_event(event);

    ESP_LOGI(TAG, "Simulation événement CAN: %s (%d)",
             config_manager_get_event_name(event), event);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", success ? "ok" : "error");
    cJSON_AddStringToObject(response, "event", config_manager_get_event_name(event));

    const char *json_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);

    free((void *)json_string);
    cJSON_Delete(response);
    cJSON_Delete(root);

    return ESP_OK;
}

// Handler pour obtenir tous les événements CAN (GET)
static esp_err_t events_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON *events_array = cJSON_CreateArray();

    // Itérer à travers tous les événements CAN (sauf CAN_EVENT_NONE)
    for (int i = CAN_EVENT_TURN_LEFT; i < CAN_EVENT_MAX; i++) {
        can_event_type_t event_type = (can_event_type_t)i;
        can_event_effect_t event_effect;

        // Obtenir la configuration de l'événement
        if (config_manager_get_effect_for_event(event_type, &event_effect)) {
            cJSON *event_obj = cJSON_CreateObject();

            // Utiliser des IDs alphanumériques
            cJSON_AddStringToObject(event_obj, "event", config_manager_enum_to_id(event_effect.event));
            cJSON_AddStringToObject(event_obj, "effect", led_effects_enum_to_id(event_effect.effect_config.effect));
            cJSON_AddNumberToObject(event_obj, "brightness", event_effect.effect_config.brightness);
            cJSON_AddNumberToObject(event_obj, "speed", event_effect.effect_config.speed);
            cJSON_AddNumberToObject(event_obj, "color", event_effect.effect_config.color1);
            cJSON_AddNumberToObject(event_obj, "duration", event_effect.duration_ms);
            cJSON_AddNumberToObject(event_obj, "priority", event_effect.priority);
            cJSON_AddBoolToObject(event_obj, "enabled", event_effect.enabled);
            cJSON_AddNumberToObject(event_obj, "action_type", event_effect.action_type);
            cJSON_AddNumberToObject(event_obj, "profile_id", event_effect.profile_id);
            cJSON_AddBoolToObject(event_obj, "can_switch_profile", config_manager_event_can_switch_profile(event_type));

            cJSON_AddItemToArray(events_array, event_obj);
        }
    }

    cJSON_AddItemToObject(root, "events", events_array);

    const char *json_string = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);

    free((void *)json_string);
    cJSON_Delete(root);

    return ESP_OK;
}

// Handler pour mettre à jour les événements CAN (POST)
static esp_err_t events_post_handler(httpd_req_t *req) {
    char *content = NULL;
    int ret = 0;
    size_t content_len = req->content_len;

    // Allouer de la mémoire pour le contenu (peut être grand)
    if (content_len > 8192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too large");
        return ESP_FAIL;
    }

    content = (char *)malloc(content_len + 1);
    if (content == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, content, content_len);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        free(content);
        return ESP_FAIL;
    }

    content[ret] = '\0';

    cJSON *root = cJSON_Parse(content);
    free(content);

    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *events_array = cJSON_GetObjectItem(root, "events");
    if (events_array == NULL || !cJSON_IsArray(events_array)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid events array");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    int profile_id = config_manager_get_active_profile_id();
    if (profile_id < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No active profile");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Charger le profil une seule fois (allouer dynamiquement pour éviter stack overflow)
    config_profile_t *profile = (config_profile_t*)malloc(sizeof(config_profile_t));
    if (profile == NULL) {
        ESP_LOGE(TAG, "Erreur allocation mémoire");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    if (!config_manager_load_profile(profile_id, profile)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to load profile");
        free(profile);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Traiter chaque événement
    int updated_count = 0;
    cJSON *event_obj = NULL;
    cJSON_ArrayForEach(event_obj, events_array) {
        cJSON *event_json = cJSON_GetObjectItem(event_obj, "event");
        cJSON *effect_json = cJSON_GetObjectItem(event_obj, "effect");
        cJSON *brightness_json = cJSON_GetObjectItem(event_obj, "brightness");
        cJSON *speed_json = cJSON_GetObjectItem(event_obj, "speed");
        cJSON *color_json = cJSON_GetObjectItem(event_obj, "color");
        cJSON *duration_json = cJSON_GetObjectItem(event_obj, "duration");
        cJSON *priority_json = cJSON_GetObjectItem(event_obj, "priority");
        cJSON *enabled_json = cJSON_GetObjectItem(event_obj, "enabled");
        cJSON *action_type_json = cJSON_GetObjectItem(event_obj, "action_type");
        cJSON *profile_id_json = cJSON_GetObjectItem(event_obj, "profile_id");

        if (event_json == NULL || effect_json == NULL) {
            continue; // Skip invalid entries
        }

        // Les IDs sont maintenant des strings uniquement
        if (!cJSON_IsString(event_json) || !cJSON_IsString(effect_json)) {
            ESP_LOGW(TAG, "Format invalide: event et effect doivent être des strings");
            continue;
        }

        can_event_type_t event_type = config_manager_id_to_enum(event_json->valuestring);
        if (event_type <= CAN_EVENT_NONE || event_type >= CAN_EVENT_MAX) {
            ESP_LOGW(TAG, "Type d'événement invalide: %s", event_json->valuestring);
            continue;
        }

        // Créer la configuration d'effet
        effect_config_t effect_config;
        effect_config.effect = led_effects_id_to_enum(effect_json->valuestring);
        effect_config.brightness = brightness_json ? brightness_json->valueint : 128;
        effect_config.speed = speed_json ? speed_json->valueint : 50;
        effect_config.color1 = color_json ? color_json->valueint : 0xFF0000;
        effect_config.color2 = 0;
        effect_config.color3 = 0;
        effect_config.sync_mode = SYNC_OFF;

        // Définir le flag reverse en fonction du type d'événement
        // LEFT events = reverse true (animation centre -> gauche)
        // RIGHT events = reverse false (animation centre -> droite)
        effect_config.reverse = (event_type == CAN_EVENT_TURN_LEFT ||
                                 event_type == CAN_EVENT_BLINDSPOT_LEFT);

        uint16_t duration = duration_json ? duration_json->valueint : 0;
        uint8_t priority = priority_json ? priority_json->valueint : 100;

        // Mettre à jour l'événement
        if (config_manager_set_event_effect(profile_id, event_type, &effect_config,
                                           duration, priority)) {
            updated_count++;
        }

        // Mettre à jour le flag enabled directement dans le profil
        if (enabled_json != NULL) {
            profile->event_effects[event_type].enabled = cJSON_IsTrue(enabled_json);
        }

        // Mettre à jour action_type et profile_id
        if (action_type_json != NULL) {
            int action_type = action_type_json->valueint;
            if (action_type >= EVENT_ACTION_APPLY_EFFECT && action_type <= EVENT_ACTION_BOTH) {
                profile->event_effects[event_type].action_type = (event_action_type_t)action_type;
            }
        }
        if (profile_id_json != NULL) {
            profile->event_effects[event_type].profile_id = profile_id_json->valueint;
        }
    }

    // Sauvegarder le profil UNE SEULE FOIS après avoir traité tous les événements
    config_manager_save_profile(profile_id, profile);
    free(profile);

    cJSON_Delete(root);

    // Réponse
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "updated", updated_count);

    const char *json_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);

    free((void *)json_string);
    cJSON_Delete(response);

    ESP_LOGI(TAG, "%d événements CAN mis à jour", updated_count);

    return ESP_OK;
}

esp_err_t web_server_init(void) {
    // Enregistrer le callback CAN
    commander_register_callback(can_frame_callback, NULL);
    
    ESP_LOGI(TAG, "Serveur web initialisé");
    return ESP_OK;
}

esp_err_t web_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    config.max_uri_handlers = 30; // Augmenté pour supporter toutes les routes (actuellement 26)
    config.lru_purge_enable = true;
    #ifndef CONFIG_HAS_PSRAM
    config.stack_size = 24576; // Augmenter la stack pour la version sans PSRAM
#else
    config.stack_size = 16384; // Stack par défaut pour la version PSRAM
#endif
    config.recv_wait_timeout = 30; // Timeout de réception (30s au lieu de 10s)
    config.send_wait_timeout = 30; // Timeout d'envoi (30s au lieu de 10s)

    ESP_LOGI(TAG, "Démarrage du serveur web sur port %d", config.server_port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        // Routes
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &index_uri);
        
        httpd_uri_t status_uri = {
            .uri = "/api/status",
            .method = HTTP_GET,
            .handler = status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &status_uri);
        
        httpd_uri_t config_uri = {
            .uri = "/api/config",
            .method = HTTP_GET,
            .handler = config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &config_uri);
        
        httpd_uri_t effect_uri = {
            .uri = "/api/effect",
            .method = HTTP_POST,
            .handler = effect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &effect_uri);
        
        httpd_uri_t save_uri = {
            .uri = "/api/save",
            .method = HTTP_POST,
            .handler = save_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &save_uri);
        
        httpd_uri_t cmd_connect_uri = {
            .uri = "/api/commander/connect",
            .method = HTTP_POST,
            .handler = commander_connect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &cmd_connect_uri);
        
        httpd_uri_t cmd_disconnect_uri = {
            .uri = "/api/commander/disconnect",
            .method = HTTP_POST,
            .handler = commander_disconnect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &cmd_disconnect_uri);
        
        // Routes pour les profils
        httpd_uri_t profiles_uri = {
            .uri = "/api/profiles",
            .method = HTTP_GET,
            .handler = profiles_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &profiles_uri);
        
        httpd_uri_t profile_activate_uri = {
            .uri = "/api/profile/activate",
            .method = HTTP_POST,
            .handler = profile_activate_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &profile_activate_uri);
        
        httpd_uri_t profile_create_uri = {
            .uri = "/api/profile/create",
            .method = HTTP_POST,
            .handler = profile_create_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &profile_create_uri);
        
        httpd_uri_t profile_delete_uri = {
            .uri = "/api/profile/delete",
            .method = HTTP_POST,
            .handler = profile_delete_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &profile_delete_uri);

        httpd_uri_t factory_reset_uri = {
            .uri = "/api/factory-reset",
            .method = HTTP_POST,
            .handler = factory_reset_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &factory_reset_uri);

        httpd_uri_t profile_update_uri = {
            .uri = "/api/profile/update",
            .method = HTTP_POST,
            .handler = profile_update_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &profile_update_uri);

        httpd_uri_t profile_update_default_uri = {
            .uri = "/api/profile/update/default",
            .method = HTTP_POST,
            .handler = profile_update_default_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &profile_update_default_uri);

        httpd_uri_t event_effect_uri = {
            .uri = "/api/event-effect",
            .method = HTTP_POST,
            .handler = event_effect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &event_effect_uri);

        httpd_uri_t profile_export_uri = {
            .uri = "/api/profile/export",
            .method = HTTP_GET,
            .handler = profile_export_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &profile_export_uri);

        httpd_uri_t profile_import_uri = {
            .uri = "/api/profile/import",
            .method = HTTP_POST,
            .handler = profile_import_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &profile_import_uri);

        // Routes OTA
        httpd_uri_t ota_info_uri = {
            .uri = "/api/ota/info",
            .method = HTTP_GET,
            .handler = ota_info_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &ota_info_uri);

        httpd_uri_t ota_upload_uri = {
            .uri = "/api/ota/upload",
            .method = HTTP_POST,
            .handler = ota_upload_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &ota_upload_uri);

        httpd_uri_t ota_restart_uri = {
            .uri = "/api/ota/restart",
            .method = HTTP_POST,
            .handler = ota_restart_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &ota_restart_uri);

        // Route de simulation
        httpd_uri_t simulate_event_uri = {
            .uri = "/api/simulate/event",
            .method = HTTP_POST,
            .handler = simulate_event_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &simulate_event_uri);

        httpd_uri_t stop_event_uri = {
            .uri = "/api/stop/event",
            .method = HTTP_POST,
            .handler = stop_event_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &stop_event_uri);

        // Routes pour obtenir les listes d'effets et d'événements
        httpd_uri_t effects_list_uri = {
            .uri = "/api/effects",
            .method = HTTP_GET,
            .handler = effects_list_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &effects_list_uri);

        httpd_uri_t event_types_list_uri = {
            .uri = "/api/event-types",
            .method = HTTP_GET,
            .handler = event_types_list_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &event_types_list_uri);

        // Route POST /api/config pour configurer le matériel LED
        httpd_uri_t config_post_uri = {
            .uri = "/api/config",
            .method = HTTP_POST,
            .handler = config_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &config_post_uri);

        // Route POST /api/events pour mettre à jour les événements CAN
        // IMPORTANT: Enregistrer POST AVANT GET pour éviter les conflits
        httpd_uri_t events_post_uri = {
            .uri = "/api/events",
            .method = HTTP_POST,
            .handler = events_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &events_post_uri);

        // Route GET /api/events pour obtenir les événements CAN
        httpd_uri_t events_get_uri = {
            .uri = "/api/events",
            .method = HTTP_GET,
            .handler = events_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &events_get_uri);

        ESP_LOGI(TAG, "Serveur web démarré");
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Erreur démarrage serveur web");
    return ESP_FAIL;
}

esp_err_t web_server_stop(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Serveur web arrêté");
    }
    return ESP_OK;
}

bool web_server_is_running(void) {
    return server != NULL;
}
