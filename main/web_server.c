/**
 * @file web_server.c
 * @brief Serveur HTTP pour l'interface web et l'API REST
 *
 * Gère:
 * - Serveur HTTP avec routes statiques (HTML, CSS, JS) compressées GZIP
 * - API REST pour configuration profils, événements CAN, effets LED
 * - Portail captif pour configuration WiFi initiale
 * - Mise à jour OTA (Over-The-Air)
 * - Streaming de l'état véhicule et des événements CAN
 */

#include "web_server.h"

#include "audio_input.h"
#include "cJSON.h"
#include "can_bus.h"
#include "config.h"
#include "config_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "led_effects.h"
#include "nvs_flash.h"
#include "ota_update.h"
#include "vehicle_can_unified.h"
#include "wifi_manager.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

// Constantes pour les tailles de buffers
#define BUFFER_SIZE_SMALL       128
#define BUFFER_SIZE_MEDIUM      256
#define BUFFER_SIZE_LARGE       512
#define BUFFER_SIZE_JSON        1024
#define BUFFER_SIZE_PROFILE     16384
#define BUFFER_SIZE_EVENT_MAX   16384

// Constantes pour les limites du système
#define LED_COUNT_MIN           1
#define LED_COUNT_MAX           200
#define MAX_CONTENT_LENGTH      4096

// Constantes de timing
#define VEHICLE_STATE_TIMEOUT_MS  5000
#define TASK_DELAY_MS             1000
#define RETRY_DELAY_BASE_MS       20
#define RETRY_DELAY_MAX_MS        500

// Constantes de configuration serveur
#define HTTP_MAX_URI_HANDLERS   60
#define HTTP_MAX_OPEN_SOCKETS   13

// Constantes de configuration par défaut
#define DEFAULT_BRIGHTNESS      128
#define DEFAULT_SPEED           50
#define DEFAULT_PRIORITY        100

// Cache control HTTP
#define CACHE_ONE_YEAR          "public, max-age=31536000"

/**
 * Clés JSON abrégées utilisées dans l'API REST (pour réduire la taille des paquets)
 *
 * EFFETS LED:
 *   fx  = effect (ID alphanumérique de l'effet)
 *   br  = brightness (0-255)
 *   sp  = speed (0-100)
 *   c1  = color1 (format 0xRRGGBB)
 *   c2  = color2
 *   c3  = color3
 *   rv  = reverse (bool: direction de l'animation)
 *   ar  = audio_reactive (bool)
 *   sm  = sync_mode
 *
 * ÉVÉNEMENTS CAN:
 *   ev  = event (ID alphanumérique de l'événement)
 *   dur = duration_ms (durée en millisecondes)
 *   pri = priority (0-255, plus haut = plus prioritaire)
 *   en  = enabled (bool)
 *   at  = action_type (0=effet, 1=switch profil)
 *   csp = can_switch_profile (bool)
 *
 * SEGMENTS LED:
 *   st  = segment_start (index de départ, 0-based)
 *   ln  = segment_length (longueur, 0=full strip)
 *
 * PROFILS:
 *   pid = profile_id (0-9)
 *
 * RÉPONSES:
 *   st  = status ("ok" ou "error")
 *   msg = message (texte descriptif)
 *   pg  = progress (0-100)
 *   upd = updated (nombre d'éléments mis à jour)
 *   rr  = requires_restart (bool)
 *
 * AUDIO/FFT:
 *   sen = sensitivity (sensibilité micro)
 *   gn  = gain
 *   sr  = sample_rate
 *
 * VÉHICULE:
 *   va  = vehicle_active (bool)
 *   nm  = night_mode (bool)
 *   ap  = autopilot (0-3)
 *   bl  = blindspot_left (bool)
 *   br  = blindspot_right (bool)
 *   scl = side_colission_left (bool)
 *   scr = side_colission_right (bool)
 */

static httpd_handle_t server                 = NULL;
static vehicle_state_t current_vehicle_state = {0};
static esp_err_t event_single_post_handler(httpd_req_t *req);

// HTML de la page principale (embarqué, version compressée GZIP)
extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");
extern const uint8_t i18n_js_gz_start[] asm("_binary_i18n_js_gz_start");
extern const uint8_t i18n_js_gz_end[] asm("_binary_i18n_js_gz_end");
extern const uint8_t script_js_gz_start[] asm("_binary_script_js_gz_start");
extern const uint8_t script_js_gz_end[] asm("_binary_script_js_gz_end");
extern const uint8_t style_css_gz_start[] asm("_binary_style_css_gz_start");
extern const uint8_t style_css_gz_end[] asm("_binary_style_css_gz_end");
extern const uint8_t carlightsync64_png_start[] asm("_binary_carlightsync64_png_start");
extern const uint8_t carlightsync64_png_end[] asm("_binary_carlightsync64_png_end");

// Structure pour les handler des fichiers statiques
typedef struct {
  const char *uri;
  const uint8_t *start;
  const uint8_t *end;
  const char *content_type;
  const char *cache_control;
  const char *content_encoding;
} static_file_route_t;

void web_server_update_vehicle_state(const vehicle_state_t *state) {
  if (!state)
    return;
  // Option simple
  memcpy(&current_vehicle_state, state, sizeof(vehicle_state_t));

  // Si tu veux être un peu parano sur la concurrence :
  // taskENTER_CRITICAL / taskEXIT_CRITICAL autour du memcpy.
}

/**
 * @brief Parse une requête JSON HTTP et retourne l'objet cJSON
 *
 * @param req Requête HTTP
 * @param buffer Buffer pour recevoir le contenu
 * @param buffer_size Taille du buffer
 * @param out_json Pointeur pour recevoir l'objet cJSON parsé (doit être libéré avec cJSON_Delete)
 * @return ESP_OK si succès, ESP_FAIL si erreur (réponse HTTP déjà envoyée)
 */
static esp_err_t parse_json_request(httpd_req_t *req, char *buffer, size_t buffer_size, cJSON **out_json) {
  if (!req || !buffer || !out_json || buffer_size == 0) {
    return ESP_FAIL;
  }

  // Vérifier qu'il y a bien des données à recevoir
  if (req->content_len == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request body");
    return ESP_FAIL;
  }

  // Valider que le contenu ne dépasse pas la taille du buffer
  if (req->content_len >= buffer_size) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request body too large");
    return ESP_FAIL;
  }

  size_t remaining = req->content_len;
  size_t offset    = 0;

  // Lire tout le corps de la requête, même si httpd_req_recv renvoie par fragments
  while (remaining > 0) {
    size_t to_read = MIN(remaining, buffer_size - 1 - offset);
    int ret        = httpd_req_recv(req, buffer + offset, to_read);

    if (ret <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        httpd_resp_send_408(req);
      }
      return ESP_FAIL;
    }

    offset += ret;
    remaining -= ret;
  }

  buffer[offset] = '\0';

  *out_json   = cJSON_Parse(buffer);
  if (*out_json == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  return ESP_OK;
}

static esp_err_t static_file_handler(httpd_req_t *req) {
  static_file_route_t *route = (static_file_route_t *)req->user_ctx;
  const size_t file_size     = (route->end - route->start);

  // Vérifier si la connexion est toujours valide
  int sockfd                 = httpd_req_to_sockfd(req);
  if (sockfd < 0) {
    ESP_LOGW(TAG_WEBSERVER, "Socket invalide pour URI %s", route->uri);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, route->content_type);
  if (route->cache_control) {
    httpd_resp_set_hdr(req, "Cache-Control", route->cache_control);
  }
  if (route->content_encoding) {
    httpd_resp_set_hdr(req, "Content-Encoding", route->content_encoding);
  }

  // Fermer la connexion après l'envoi (désactive keep-alive pour cette requête)
  httpd_resp_set_hdr(req, "Connection", "close");

// Toujours envoyer par chunks pour éviter EAGAIN, même pour petits fichiers
#define CHUNK_SIZE 1024 // Réduit à 1KB pour éviter buffer overflow
#define MAX_RETRIES 10  // Augmenté à 10 retries

  const uint8_t *data = route->start;
  size_t remaining    = file_size;
  esp_err_t err       = ESP_OK;

  ESP_LOGD(TAG_WEBSERVER, "Envoi fichier %s (%d bytes)", route->uri, file_size);

  while (remaining > 0) {
    size_t chunk_size = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;

    // Réessayer jusqu'à MAX_RETRIES fois en cas d'EAGAIN
    int retries       = 0;
    while (retries < MAX_RETRIES) {
      err = httpd_resp_send_chunk(req, (const char *)data, chunk_size);

      if (err == ESP_OK) {
        break; // Succès
      }

      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        retries++;
        // Délai exponentiel: 20ms, 40ms, 80ms, etc.
        int delay_ms = RETRY_DELAY_BASE_MS * (1 << (retries - 1));
        if (delay_ms > RETRY_DELAY_MAX_MS)
          delay_ms = RETRY_DELAY_MAX_MS;
        ESP_LOGD(TAG_WEBSERVER, "EAGAIN pour %s, retry %d/%d (wait %dms)", route->uri, retries, MAX_RETRIES, delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
      } else {
        // Autre erreur, abandonner
        ESP_LOGW(TAG_WEBSERVER, "Erreur envoi chunk pour %s: %s (errno: %d)", route->uri, esp_err_to_name(err), errno);
        return err;
      }
    }

    if (err != ESP_OK) {
      ESP_LOGW(TAG_WEBSERVER, "Échec envoi après %d retries pour %s", MAX_RETRIES, route->uri);
      return err;
    }

    data += chunk_size;
    remaining -= chunk_size;

    // Petit délai entre chunks pour laisser le buffer TCP se vider
    if (remaining > 0) {
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }

  // Terminer l'envoi chunké
  err = httpd_resp_send_chunk(req, NULL, 0);
  if (err != ESP_OK) {
    ESP_LOGW(TAG_WEBSERVER, "Erreur fin chunk pour %s: %s", route->uri, esp_err_to_name(err));
  } else {
    ESP_LOGD(TAG_WEBSERVER, "Fichier %s envoyé avec succès", route->uri);
  }

  return err;
}

// Handler d'erreur 404 pour le portail captif
// Redirige toutes les URLs non trouvées vers la page principale
static esp_err_t captive_portal_404_handler(httpd_req_t *req, httpd_err_code_t err) {
  if (err == HTTPD_404_NOT_FOUND) {
    // Envoyer la page principale (index.html) pour toutes les requêtes non
    // trouvées
    const size_t file_size = (index_html_gz_end - index_html_gz_start);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    ESP_LOGD(TAG_WEBSERVER, "Portail captif: redirection de %s vers index.html", req->uri);
    return httpd_resp_send(req, (const char *)index_html_gz_start, file_size);
  }

  // Pour les autres erreurs, retourner l'erreur par défaut
  return ESP_FAIL;
}

// Handler pour obtenir le statut
static esp_err_t status_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();

  // Statut WiFi
  wifi_status_t wifi_status;
  wifi_manager_get_status(&wifi_status);
  cJSON_AddBoolToObject(root, "wc", wifi_status.sta_connected);
  cJSON_AddStringToObject(root, "wip", wifi_status.sta_ip);

  // Statut CAN Bus - Body
  can_bus_status_t can_body_status;
  can_bus_get_status(CAN_BUS_BODY, &can_body_status);
  cJSON *can_body = cJSON_CreateObject();
  cJSON_AddBoolToObject(can_body, "r", can_body_status.rx_count > 0); // can_body_status.running);
  // cJSON_AddNumberToObject(can_body, "rx", can_body_status.rx_count);
  // cJSON_AddNumberToObject(can_body, "tx", can_body_status.tx_count);
  cJSON_AddNumberToObject(can_body, "er", can_body_status.errors);
  cJSON_AddItemToObject(root, "cbb", can_body);

  // Statut CAN Bus - Chassis
  can_bus_status_t can_chassis_status;
  can_bus_get_status(CAN_BUS_CHASSIS, &can_chassis_status);
  cJSON *can_chassis = cJSON_CreateObject();
  cJSON_AddBoolToObject(can_chassis, "r",
                        can_chassis_status.rx_count > 0); // can_chassis_status.running);
  // cJSON_AddNumberToObject(can_chassis, "rx", can_chassis_status.rx_count);
  // cJSON_AddNumberToObject(can_chassis, "tx", can_chassis_status.tx_count);
  cJSON_AddNumberToObject(can_chassis, "er", can_chassis_status.errors);
  cJSON_AddItemToObject(root, "cbc", can_chassis);

  // Statut véhicule
  uint32_t now        = xTaskGetTickCount();
  bool vehicle_active = (now - current_vehicle_state.last_update_ms) < pdMS_TO_TICKS(VEHICLE_STATE_TIMEOUT_MS);
  cJSON_AddBoolToObject(root, "va", vehicle_active);

  // Données véhicule complètes
  cJSON *vehicle = cJSON_CreateObject();

  // État général
  cJSON_AddNumberToObject(vehicle, "g", current_vehicle_state.gear);
  cJSON_AddNumberToObject(vehicle, "s", current_vehicle_state.speed_kph);
  cJSON_AddNumberToObject(vehicle, "bp", current_vehicle_state.brake_pressed);

  // Portes
  cJSON *doors = cJSON_CreateObject();
  cJSON_AddBoolToObject(doors, "fl", current_vehicle_state.door_front_left_open);
  cJSON_AddBoolToObject(doors, "fr", current_vehicle_state.door_front_right_open);
  cJSON_AddBoolToObject(doors, "rl", current_vehicle_state.door_rear_left_open);
  cJSON_AddBoolToObject(doors, "rr", current_vehicle_state.door_rear_right_open);
  cJSON_AddBoolToObject(doors, "t", current_vehicle_state.trunk_open);
  cJSON_AddBoolToObject(doors, "f", current_vehicle_state.frunk_open);
  cJSON_AddNumberToObject(doors, "co", current_vehicle_state.doors_open_count);
  cJSON_AddItemToObject(vehicle, "doors", doors);

  // Verrouillage
  cJSON_AddBoolToObject(vehicle, "lk", current_vehicle_state.locked);

  // Lumières
  cJSON *lights = cJSON_CreateObject();
  cJSON_AddBoolToObject(lights, "h", current_vehicle_state.headlights);
  cJSON_AddBoolToObject(lights, "hb", current_vehicle_state.high_beams);
  cJSON_AddBoolToObject(lights, "fg", current_vehicle_state.fog_lights);
  cJSON_AddBoolToObject(lights, "tl", current_vehicle_state.turn_left);
  cJSON_AddBoolToObject(lights, "tr", current_vehicle_state.turn_right);
  cJSON_AddBoolToObject(lights, "hz", current_vehicle_state.hazard);
  cJSON_AddItemToObject(vehicle, "lights", lights);

  // Charge
  cJSON *charge = cJSON_CreateObject();
  cJSON_AddBoolToObject(charge, "ch", current_vehicle_state.charging);
  cJSON_AddNumberToObject(charge, "pct", current_vehicle_state.soc_percent);
  cJSON_AddNumberToObject(charge, "pw", current_vehicle_state.charge_power_kw);
  cJSON_AddItemToObject(vehicle, "charge", charge);

  // Batterie et autres
  cJSON_AddNumberToObject(vehicle, "blv", current_vehicle_state.battery_voltage_LV);
  cJSON_AddNumberToObject(vehicle, "bhv", current_vehicle_state.battery_voltage_HV);
  cJSON_AddNumberToObject(vehicle, "odo", current_vehicle_state.odometer_km);

  // Sécurité
  cJSON *safety = cJSON_CreateObject();
  cJSON_AddBoolToObject(safety, "bsl", current_vehicle_state.blindspot_left);
  cJSON_AddBoolToObject(safety, "bsr", current_vehicle_state.blindspot_right);
  cJSON_AddBoolToObject(safety, "scl", current_vehicle_state.side_collision_left);
  cJSON_AddBoolToObject(safety, "scr", current_vehicle_state.side_collision_right);
  cJSON_AddBoolToObject(safety, "nm", current_vehicle_state.night_mode);
  cJSON_AddNumberToObject(safety, "bri", current_vehicle_state.brightness);
  cJSON_AddBoolToObject(safety, "sm", current_vehicle_state.sentry_mode);
  cJSON_AddNumberToObject(safety, "ap", current_vehicle_state.autopilot);
  cJSON_AddBoolToObject(safety, "sa", current_vehicle_state.sentry_alert);
  cJSON_AddItemToObject(vehicle, "safety", safety);

  cJSON_AddItemToObject(root, "vehicle", vehicle);

  // Profil actuellement appliqué (peut changer temporairement via évènements)
  int active_profile_id = config_manager_get_active_profile_id();
  cJSON_AddNumberToObject(root, "pid", active_profile_id);
  config_profile_t *active_profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (active_profile && config_manager_get_active_profile(active_profile)) {
    cJSON_AddStringToObject(root, "pn", active_profile->name);
  } else {
    cJSON_AddStringToObject(root, "pn", "None");
  }
  if (active_profile) {
    free(active_profile);
  }

  const char *json_string = cJSON_PrintUnformatted(root);
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
  cJSON_AddNumberToObject(root, "fx", config.effect);
  cJSON_AddNumberToObject(root, "br", config.brightness);
  cJSON_AddNumberToObject(root, "sp", config.speed);
  cJSON_AddNumberToObject(root, "c1", config.color1);
  cJSON_AddNumberToObject(root, "c2", config.color2);
  cJSON_AddNumberToObject(root, "c3", config.color3);
  cJSON_AddNumberToObject(root, "sm", config.sync_mode);
  cJSON_AddBoolToObject(root, "rv", config.reverse);

  // Ajouter les paramètres du profil actif (allouer dynamiquement pour éviter
  // stack overflow)
  config_profile_t *profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (profile != NULL && config_manager_get_active_profile(profile)) {
    free(profile);
  } else if (profile != NULL) {
    free(profile);
  }

  // Ajouter la configuration matérielle LED
  cJSON_AddNumberToObject(root, "lc", config_manager_get_led_count());


  // Réglages globaux (wheel control)
  cJSON_AddBoolToObject(root, "wheel_ctl", config_manager_get_wheel_control_enabled());
  cJSON_AddNumberToObject(root, "wheel_spd", config_manager_get_wheel_control_speed_limit());

  const char *json_string = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_string);

  free((void *)json_string);
  cJSON_Delete(root);

  return ESP_OK;
}

// Handler pour définir l'effet
static esp_err_t effect_handler(httpd_req_t *req) {
  char content[BUFFER_SIZE_LARGE];
  cJSON *root = NULL;

  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  effect_config_t config;
  led_effects_get_config(&config);

  const cJSON *effect = cJSON_GetObjectItem(root, "fx");
  if (effect)
    config.effect = effect->valueint;

  const cJSON *brightness = cJSON_GetObjectItem(root, "br");
  if (brightness)
    config.brightness = brightness->valueint;

  const cJSON *speed = cJSON_GetObjectItem(root, "sp");
  if (speed)
    config.speed = speed->valueint;

  const cJSON *color1 = cJSON_GetObjectItem(root, "c1");
  if (color1)
    config.color1 = color1->valueint;

  const cJSON *color2 = cJSON_GetObjectItem(root, "c2");
  if (color2)
    config.color2 = color2->valueint;

  const cJSON *color3 = cJSON_GetObjectItem(root, "c3");
  if (color3)
    config.color3 = color3->valueint;

  led_effects_set_config(&config);

  cJSON_Delete(root);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\"}");

  return ESP_OK;
}

// Handler pour sauvegarder la configuration
static esp_err_t save_handler(httpd_req_t *req) {
  // Sauvegarder tous les paramètres: LED + Audio
  bool led_success   = led_effects_save_config();
  bool audio_success = audio_input_save_config();

  if (led_success && audio_success) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    ESP_LOGI(TAG_WEBSERVER, "Configuration sauvegardée (LED + Audio)");
  } else {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");
    ESP_LOGE(TAG_WEBSERVER, "Échec sauvegarde - LED: %s, Audio: %s", led_success ? "OK" : "FAIL", audio_success ? "OK" : "FAIL");
  }

  return ESP_OK;
}

// Handler pour configurer le matériel LED (POST)
static esp_err_t config_post_handler(httpd_req_t *req) {
  char content[BUFFER_SIZE_MEDIUM];
  cJSON *root = NULL;

  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  const cJSON *led_count_json     = cJSON_GetObjectItem(root, "lc");
  const cJSON *wheel_ctl_json     = cJSON_GetObjectItem(root, "wheel_ctl");
  const cJSON *wheel_spd_json     = cJSON_GetObjectItem(root, "wheel_spd");

  if (led_count_json == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing led_count");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  uint16_t led_count = (uint16_t)led_count_json->valueint;

  // Appliquer réglages wheel control (optionnels)
  if (wheel_ctl_json && cJSON_IsBool(wheel_ctl_json)) {
    config_manager_set_wheel_control_enabled(cJSON_IsTrue(wheel_ctl_json));
  }
  if (wheel_spd_json && cJSON_IsNumber(wheel_spd_json)) {
    config_manager_set_wheel_control_speed_limit((float)wheel_spd_json->valuedouble);
  }

  // Validation
  if (led_count < LED_COUNT_MIN || led_count > LED_COUNT_MAX) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "led_count must be 1-200");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  // Sauvegarder en NVS (seulement le nombre de LEDs)
  bool success = config_manager_set_led_count(led_count);

  cJSON_Delete(root);

  if (success) {
    if (!led_effects_set_led_count(led_count)) {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to apply new LED count");
      return ESP_FAIL;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "st", "ok");
    cJSON_AddStringToObject(response, "msg", "Configuration saved and applied.");
    cJSON_AddBoolToObject(response, "rr", false);

    const char *json_string = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);

    free((void *)json_string);
    cJSON_Delete(response);

    ESP_LOGI(TAG_WEBSERVER, "Configuration LED appliquée: %d LEDs, GPIO %d", led_count, LED_PIN);
  } else {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save configuration");
  }

  return ESP_OK;
}

// Handler pour lister les profils
static esp_err_t profiles_handler(httpd_req_t *req) {
  // Allouer un seul profil à la fois pour économiser la mémoire
  config_profile_t *profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (profile == NULL) {
    ESP_LOGE(TAG_WEBSERVER, "Erreur allocation mémoire pour profil");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
    return ESP_FAIL;
  }

  int active_id         = config_manager_get_active_profile_id();
  cJSON *root           = cJSON_CreateObject();
  cJSON *profiles_array = cJSON_CreateArray();

  // Charger chaque profil un par un (scanner dynamiquement)
  for (int i = 0; i < MAX_PROFILE_SCAN_LIMIT; i++) {
    if (!config_manager_load_profile(i, profile)) {
      continue; // Profil n'existe pas, passer au suivant
    }

    cJSON *profile_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(profile_obj, "id", i);
    cJSON_AddStringToObject(profile_obj, "n", profile->name);
    cJSON_AddBoolToObject(profile_obj, "ac", (i == active_id));

    // Ajouter l'effet par défaut
    cJSON *default_effect_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(default_effect_obj, "fx", profile->default_effect.effect);
    cJSON_AddNumberToObject(default_effect_obj, "br", profile->default_effect.brightness);
    cJSON_AddNumberToObject(default_effect_obj, "sp", profile->default_effect.speed);
    cJSON_AddNumberToObject(default_effect_obj, "c1", profile->default_effect.color1);
    cJSON_AddBoolToObject(default_effect_obj, "ar", profile->default_effect.audio_reactive);
    cJSON_AddBoolToObject(default_effect_obj, "rv", profile->default_effect.reverse);
    cJSON_AddNumberToObject(default_effect_obj, "st", profile->default_effect.segment_start);
    cJSON_AddNumberToObject(default_effect_obj, "ln", profile->default_effect.segment_length);
    cJSON_AddItemToObject(profile_obj, "default_effect", default_effect_obj);

    // Ajouter les paramètres de luminosité dynamique
    cJSON_AddBoolToObject(profile_obj, "dbe", profile->dynamic_brightness_enabled);
    cJSON_AddNumberToObject(profile_obj, "dbr", profile->dynamic_brightness_rate);

    cJSON_AddItemToArray(profiles_array, profile_obj);
  }

  cJSON_AddItemToObject(root, "profiles", profiles_array);

  // Charger le nom du profil actif
  if (active_id >= 0 && config_manager_load_profile(active_id, profile)) {
    cJSON_AddStringToObject(root, "an", profile->name);
  } else {
    cJSON_AddStringToObject(root, "an", "None");
  }

  // Ajouter les statistiques NVS
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open("profiles", NVS_READONLY, &nvs_handle);
  if (err == ESP_OK) {
    nvs_stats_t nvs_stats;
    err = nvs_get_stats(NULL, &nvs_stats);
    if (err == ESP_OK) {
      size_t used_entries = nvs_stats.used_entries;
      size_t free_entries = nvs_stats.free_entries;
      size_t total_entries = nvs_stats.total_entries;

      cJSON *storage = cJSON_CreateObject();
      cJSON_AddNumberToObject(storage, "used", used_entries);
      cJSON_AddNumberToObject(storage, "free", free_entries);
      cJSON_AddNumberToObject(storage, "total", total_entries);
      cJSON_AddNumberToObject(storage, "usage_pct", (total_entries > 0) ? (used_entries * 100 / total_entries) : 0);
      cJSON_AddItemToObject(root, "storage", storage);
    }
    nvs_close(nvs_handle);
  }

  const char *json_string = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_string);

  free((void *)json_string);
  cJSON_Delete(root);
  free(profile);

  return ESP_OK;
}

// Handler pour activer un profil
static esp_err_t profile_activate_handler(httpd_req_t *req) {
  char content[BUFFER_SIZE_SMALL];
  cJSON *root = NULL;

  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  const cJSON *profile_id = cJSON_GetObjectItem(root, "pid");
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
  char content[BUFFER_SIZE_MEDIUM];
  cJSON *root = NULL;

  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *name = cJSON_GetObjectItem(root, "name");
  if (name == NULL || name->valuestring == NULL) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing name");
    return ESP_FAIL;
  }

  // Vérifier d'abord s'il reste assez d'espace NVS
  if (!config_manager_can_create_profile()) {
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Insufficient storage space\"}");
    return ESP_FAIL;
  }

  // Allouer dynamiquement pour éviter le stack overflow
  config_profile_t *temp        = (config_profile_t *)malloc(sizeof(config_profile_t));
  config_profile_t *new_profile = (config_profile_t *)malloc(sizeof(config_profile_t));

  if (temp == NULL || new_profile == NULL) {
    ESP_LOGE(TAG_WEBSERVER, "Échec allocation mémoire pour profil");
    if (temp)
      free(temp);
    if (new_profile)
      free(new_profile);
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
    return ESP_FAIL;
  }

  // Trouver un slot libre (scanner dynamiquement)
  for (int i = 0; i < MAX_PROFILE_SCAN_LIMIT; i++) {
    if (!config_manager_load_profile(i, temp)) {
      // Slot libre
      ESP_LOGI(TAG_WEBSERVER, "Création profil '%s' dans slot %d", name->valuestring, i);
      config_manager_create_default_profile(new_profile, name->valuestring);

      bool saved = config_manager_save_profile(i, new_profile);
      free(temp);
      free(new_profile);
      cJSON_Delete(root);

      if (saved) {
        config_manager_activate_profile(i);
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
  char content[BUFFER_SIZE_SMALL];
  cJSON *root = NULL;

  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  const cJSON *profile_id = cJSON_GetObjectItem(root, "pid");
  if (profile_id) {
    bool success    = config_manager_delete_profile(profile_id->valueint);
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "st", success ? "ok" : "error");
    if (!success) {
      cJSON_AddStringToObject(response, "msg", "Profile in use by an event or deletion failed");
    } else {
      config_manager_activate_profile(0);
    }
    const char *json_string = cJSON_PrintUnformatted(response);
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

// Handler pour renommer un profil
static esp_err_t profile_rename_handler(httpd_req_t *req) {
  char content[BUFFER_SIZE_SMALL];
  cJSON *root = NULL;

  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  const cJSON *profile_id = cJSON_GetObjectItem(root, "pid");
  const cJSON *name       = cJSON_GetObjectItem(root, "name");

  if (!profile_id || !name || !cJSON_IsString(name) || strlen(name->valuestring) == 0) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid profile_id/name");
    return ESP_FAIL;
  }

  bool success = config_manager_rename_profile((uint16_t)profile_id->valueint, name->valuestring);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, success ? "{\"st\":\"ok\"}" : "{\"st\":\"error\"}");

  cJSON_Delete(root);
  return success ? ESP_OK : ESP_FAIL;
}

// Handler pour factory reset
static esp_err_t factory_reset_handler(httpd_req_t *req) {
  bool success    = config_manager_factory_reset();

  cJSON *response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "st", success ? "ok" : "error");
  if (success) {
    cJSON_AddStringToObject(response, "msg", "Factory reset successful. Device will restart.");
  } else {
    cJSON_AddStringToObject(response, "msg", "Factory reset failed");
  }

  const char *json_string = cJSON_PrintUnformatted(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_string);
  free((void *)json_string);
  cJSON_Delete(response);

  // Redémarrer l'ESP32 après un court délai
  if (success) {
    vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
    esp_restart();
  }

  return ESP_OK;
}

// Handler pour mettre à jour les paramètres d'un profil
static esp_err_t profile_update_handler(httpd_req_t *req) {
  char content[BUFFER_SIZE_LARGE]; // Augmenté pour accepter settings + effet par défaut
  cJSON *root = NULL;

  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *profile_id_json = cJSON_GetObjectItem(root, "pid");
  if (!profile_id_json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing profile_id");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  uint16_t profile_id = (uint16_t)profile_id_json->valueint;

  // Allouer dynamiquement pour éviter stack overflow
  config_profile_t *profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (profile == NULL) {
    ESP_LOGE(TAG_WEBSERVER, "Erreur allocation mémoire");
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

  // Mettre à jour l'effet par défaut si fourni
  bool default_effect_updated      = false;
  const cJSON *effect_json         = cJSON_GetObjectItem(root, "fx");
  const cJSON *brightness_json     = cJSON_GetObjectItem(root, "br");
  const cJSON *speed_json          = cJSON_GetObjectItem(root, "sp");
  const cJSON *color_json          = cJSON_GetObjectItem(root, "c1");
  const cJSON *audio_reactive_json = cJSON_GetObjectItem(root, "ar");
  const cJSON *reverse_json        = cJSON_GetObjectItem(root, "rv");
  const cJSON *segment_start_json  = cJSON_GetObjectItem(root, "st");
  const cJSON *segment_length_json = cJSON_GetObjectItem(root, "ln");
  const cJSON *dyn_bright_enabled_json = cJSON_GetObjectItem(root, "dbe");
  const cJSON *dyn_bright_rate_json = cJSON_GetObjectItem(root, "dbr");

  if (effect_json) {
    profile->default_effect.effect = (led_effect_t)effect_json->valueint;
    default_effect_updated         = true;
  }
  if (brightness_json) {
    profile->default_effect.brightness = (uint8_t)brightness_json->valueint;
    default_effect_updated             = true;
  }
  if (speed_json) {
    profile->default_effect.speed = (uint8_t)speed_json->valueint;
    default_effect_updated        = true;
  }
  if (color_json) {
    profile->default_effect.color1 = (uint32_t)color_json->valueint;
    default_effect_updated         = true;
  }
  if (audio_reactive_json && cJSON_IsBool(audio_reactive_json)) {
    profile->default_effect.audio_reactive = cJSON_IsTrue(audio_reactive_json);
    default_effect_updated                 = true;
  }
  if (reverse_json && cJSON_IsBool(reverse_json)) {
    profile->default_effect.reverse = cJSON_IsTrue(reverse_json);
    default_effect_updated          = true;
  }
  if (segment_start_json) {
    profile->default_effect.segment_start = (uint16_t)segment_start_json->valueint;
    default_effect_updated                = true;
  }
  if (segment_length_json) {
    profile->default_effect.segment_length = (uint16_t)segment_length_json->valueint;
    default_effect_updated                 = true;
  }

  // Mettre à jour les paramètres de luminosité dynamique
  bool dynamic_brightness_updated = false;
  if (dyn_bright_enabled_json && cJSON_IsBool(dyn_bright_enabled_json)) {
    profile->dynamic_brightness_enabled = cJSON_IsTrue(dyn_bright_enabled_json);
    dynamic_brightness_updated = true;
  }
  if (dyn_bright_rate_json && cJSON_IsNumber(dyn_bright_rate_json)) {
    profile->dynamic_brightness_rate = (uint8_t)dyn_bright_rate_json->valueint;
    dynamic_brightness_updated = true;
  }

  // Sauvegarder le profil
  bool success = config_manager_save_profile(profile_id, profile);

  // Si c'est le profil actif, mettre à jour la configuration
  if (success && config_manager_get_active_profile_id() == profile_id) {
    // Appliquer l'effet par défaut si modifié
    if (default_effect_updated) {
      config_manager_stop_all_events();
      led_effects_set_config(&profile->default_effect);
      ESP_LOGI(TAG_WEBSERVER, "Applied updated default effect");
    }
  }

  free(profile);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, success ? "{\"st\":\"ok\"}" : "{\"st\":\"error\"}");

  cJSON_Delete(root);
  return ESP_OK;
}

// Handler pour assigner un effet à un événement
static esp_err_t event_effect_handler(httpd_req_t *req) {
  char content[BUFFER_SIZE_LARGE];
  cJSON *root = NULL;

  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  int profile_id = config_manager_get_active_profile_id();
  if (profile_id < 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No active profile");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  const cJSON *event      = cJSON_GetObjectItem(root, "event");
  const cJSON *effect     = cJSON_GetObjectItem(root, "effect");
  const cJSON *duration   = cJSON_GetObjectItem(root, "duration");
  const cJSON *priority   = cJSON_GetObjectItem(root, "priority");
  const cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
  const cJSON *speed      = cJSON_GetObjectItem(root, "speed");
  const cJSON *color1     = cJSON_GetObjectItem(root, "color1");

  if (event && effect) {
    effect_config_t effect_config;
    effect_config.effect = effect->valueint;

    // Vérifier que l'effet n'est pas un effet audio (non autorisé dans les
    // événements)
    if (led_effects_is_audio_effect(effect_config.effect)) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Audio effects cannot be assigned to CAN events");
      cJSON_Delete(root);
      return ESP_FAIL;
    }

    effect_config.brightness = brightness ? brightness->valueint : DEFAULT_BRIGHTNESS;
    effect_config.speed      = speed ? speed->valueint : DEFAULT_SPEED;
    effect_config.color1     = color1 ? color1->valueint : 0xFF0000;

    bool success             = config_manager_set_event_effect(profile_id, event->valueint, &effect_config, duration ? duration->valueint : 0, priority ? priority->valueint : DEFAULT_PRIORITY);

    if (success) {
      // Sauvegarder le profil (allouer dynamiquement pour éviter stack
      // overflow)
      config_profile_t *profile = (config_profile_t *)malloc(sizeof(config_profile_t));
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

  uint16_t profile_id = atoi(param_value);

  // Allouer un buffer pour le JSON (16KB devrait suffire pour un profil complet)
  char *json_buffer  = malloc(BUFFER_SIZE_PROFILE);
  if (!json_buffer) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
    return ESP_FAIL;
  }

  bool success = config_manager_export_profile(profile_id, json_buffer, BUFFER_SIZE_PROFILE);

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

static int find_free_profile_slot(int preferred_id) {
  // Vérifier d'abord s'il reste assez d'espace NVS
  if (!config_manager_can_create_profile()) {
    ESP_LOGW(TAG_WEBSERVER, "Espace NVS insuffisant pour créer un nouveau profil");
    return -1;
  }

  config_profile_t *profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (profile == NULL) {
    ESP_LOGE(TAG_WEBSERVER, "Erreur allocation mémoire pour recherche de slot");
    return -1;
  }

  int target_id = -1;

  if (preferred_id >= 0) {
    if (!config_manager_load_profile((uint16_t)preferred_id, profile)) {
      target_id = preferred_id;
    }
  }

  if (target_id < 0) {
    for (int i = 0; i < MAX_PROFILE_SCAN_LIMIT; i++) {
      if (!config_manager_load_profile(i, profile)) {
        target_id = i;
        break;
      }
    }
  }

  free(profile);
  return target_id;
}

// Handler pour importer un profil depuis JSON
static esp_err_t profile_import_handler(httpd_req_t *req) {
  // Allouer un buffer pour recevoir le JSON
  char *content = malloc(BUFFER_SIZE_PROFILE);
  if (!content) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
    return ESP_FAIL;
  }

  cJSON *root = NULL;
  if (parse_json_request(req, content, BUFFER_SIZE_PROFILE, &root) != ESP_OK) {
    free(content);
    return ESP_FAIL;
  }

  const cJSON *profile_id_json = cJSON_GetObjectItem(root, "profile_id");
  const cJSON *profile_data    = cJSON_GetObjectItem(root, "profile_data");

  if (!profile_data) {
    cJSON_Delete(root);
    free(content);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing profile_data");
    return ESP_FAIL;
  }

  int preferred_id = -1;
  if (profile_id_json && cJSON_IsNumber(profile_id_json)) {
    preferred_id = profile_id_json->valueint;
  }

  // Convertir profile_data en chaîne JSON
  char *profile_json = cJSON_PrintUnformatted(profile_data);
  if (!profile_json) {
    cJSON_Delete(root);
    free(content);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to serialize profile");
    return ESP_FAIL;
  }

  int target_id = find_free_profile_slot(preferred_id);
  if (target_id < 0) {
    free(profile_json);
    cJSON_Delete(root);
    free(content);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"st\":\"error\",\"msg\":\"Insufficient storage space or no free profile slots\"}");
    return ESP_FAIL;
  }

  bool success = config_manager_import_profile((uint16_t)target_id, profile_json);

  free(profile_json);
  cJSON_Delete(root);
  free(content);

  httpd_resp_set_type(req, "application/json");
  if (success) {
    // Activer le profil importé
    config_manager_activate_profile((uint16_t)target_id);

    char response[64];
    snprintf(response, sizeof(response), "{\"st\":\"ok\",\"pid\":%d}", target_id);
    httpd_resp_sendstr(req, response);
  } else {
    httpd_resp_sendstr(req, "{\"st\":\"error\",\"msg\":\"Import failed\"}");
  }

  return ESP_OK;
}

// Handler pour obtenir les informations OTA
static esp_err_t ota_info_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();

  cJSON_AddStringToObject(root, "v", ota_get_current_version());

  ota_progress_t progress;
  ota_get_progress(&progress);

  cJSON_AddNumberToObject(root, "st", progress.state);
  cJSON_AddNumberToObject(root, "pg", progress.progress);
  cJSON_AddNumberToObject(root, "ws", progress.written_size);
  cJSON_AddNumberToObject(root, "ts", progress.total_size);
  cJSON_AddNumberToObject(root, "rc", ota_get_reboot_countdown());

  if (strlen(progress.error_msg) > 0) {
    cJSON_AddStringToObject(root, "err", progress.error_msg);
  }

  const char *json_string = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_string);

  free((void *)json_string);
  cJSON_Delete(root);

  return ESP_OK;
}

// Handler pour uploader le firmware OTA
static esp_err_t ota_upload_handler(httpd_req_t *req) {
  char buf[BUFFER_SIZE_JSON];
  int remaining = req->content_len;
  int received;
  bool first_chunk = true;
  esp_err_t ret    = ESP_OK;

  ESP_LOGI(TAG_WEBSERVER, "Début upload OTA, taille: %d octets", remaining);

  // Démarrer l'OTA
  ret = ota_begin(req->content_len);
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
      ESP_LOGE(TAG_WEBSERVER, "Erreur réception données OTA");
      ota_abort();
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
      return ESP_FAIL;
    }

    // Écrire dans la partition OTA
    ret = ota_write(buf, received);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG_WEBSERVER, "Erreur écriture OTA");
      ota_abort();
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
      return ESP_FAIL;
    }

    remaining -= received;

    if (first_chunk) {
      ESP_LOGI(TAG_WEBSERVER, "Premier chunk reçu et écrit");
      first_chunk = false;
    }
  }

  // Terminer l'OTA
  ret = ota_end();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_WEBSERVER, "Erreur fin OTA");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG_WEBSERVER, "Upload OTA terminé avec succès");

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req,
                     "{\"status\":\"ok\",\"message\":\"Upload successful, "
                     "restart to apply\"}");

  return ESP_OK;
}

// Handler pour redémarrer l'ESP32
static esp_err_t ota_restart_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Restarting...\"}");

  ESP_LOGI(TAG_WEBSERVER, "Redémarrage demandé via API");

  // Redémarrer après un court délai
  vTaskDelay(pdMS_TO_TICKS(1000));
  ota_restart();

  return ESP_OK;
}

// Handler pour arrêter un événement CAN
static esp_err_t stop_event_handler(httpd_req_t *req) {
  char content[BUFFER_SIZE_SMALL];
  cJSON *root = NULL;

  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
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
  } else {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid event parameter");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  // Arrêter l'événement
  config_manager_stop_event(event);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"st\":\"ok\"}");

  cJSON_Delete(root);
  return ESP_OK;
}

// Handler pour obtenir la liste des effets disponibles
static esp_err_t effects_list_handler(httpd_req_t *req) {
  cJSON *root    = cJSON_CreateObject();
  cJSON *effects = cJSON_CreateArray();

  // Parcourir tous les effets disponibles
  for (int i = 0; i < EFFECT_MAX; i++) {
    cJSON *effect = cJSON_CreateObject();
    cJSON_AddStringToObject(effect, "id", led_effects_enum_to_id((led_effect_t)i));
    cJSON_AddStringToObject(effect, "n", led_effects_get_name((led_effect_t)i));
    cJSON_AddBoolToObject(effect, "cr", led_effects_requires_can((led_effect_t)i));
    cJSON_AddBoolToObject(effect, "ae", led_effects_is_audio_effect((led_effect_t)i));
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
  cJSON *root        = cJSON_CreateObject();
  cJSON *event_types = cJSON_CreateArray();

  // Parcourir tous les types d'événements CAN
  for (int i = 0; i < CAN_EVENT_MAX; i++) {
    cJSON *event_type = cJSON_CreateObject();
    cJSON_AddStringToObject(event_type, "id", config_manager_enum_to_id((can_event_type_t)i));
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
  char content[BUFFER_SIZE_SMALL];
  cJSON *root = NULL;

  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
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

  ESP_LOGI(TAG_WEBSERVER, "Simulation événement CAN: %s (%d)", config_manager_enum_to_id(event), event);

  cJSON *response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "st", success ? "ok" : "error");
  cJSON_AddStringToObject(response, "ev", config_manager_enum_to_id(event));

  const char *json_string = cJSON_PrintUnformatted(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_string);

  free((void *)json_string);
  cJSON_Delete(response);
  cJSON_Delete(root);

  return ESP_OK;
}

// Handler pour obtenir tous les événements CAN (GET)
static bool apply_event_update_from_json(config_profile_t *profile, int profile_id, cJSON *event_obj) {
  if (profile == NULL || event_obj == NULL) {
    return false;
  }

  const cJSON *event_json       = cJSON_GetObjectItem(event_obj, "ev");
  const cJSON *effect_json      = cJSON_GetObjectItem(event_obj, "fx");
  const cJSON *brightness_json  = cJSON_GetObjectItem(event_obj, "br");
  const cJSON *speed_json       = cJSON_GetObjectItem(event_obj, "sp");
  const cJSON *color_json       = cJSON_GetObjectItem(event_obj, "c1");
  const cJSON *reverse_json     = cJSON_GetObjectItem(event_obj, "rv");
  const cJSON *duration_json    = cJSON_GetObjectItem(event_obj, "dur");
  const cJSON *priority_json    = cJSON_GetObjectItem(event_obj, "pri");
  const cJSON *enabled_json     = cJSON_GetObjectItem(event_obj, "en");
  const cJSON *action_type_json = cJSON_GetObjectItem(event_obj, "at");
  const cJSON *profile_id_json  = cJSON_GetObjectItem(event_obj, "pid");
  const cJSON *segment_start    = cJSON_GetObjectItem(event_obj, "st");
  const cJSON *segment_length   = cJSON_GetObjectItem(event_obj, "ln");

  if (event_json == NULL || effect_json == NULL || !cJSON_IsString(event_json) || !cJSON_IsString(effect_json)) {
    ESP_LOGW(TAG_WEBSERVER, "Payload d'evenement invalide");
    return false;
  }

  can_event_type_t event_type = config_manager_id_to_enum(event_json->valuestring);
  if (event_type <= CAN_EVENT_NONE || event_type >= CAN_EVENT_MAX) {
    ESP_LOGW(TAG_WEBSERVER, "Evenement inconnu: %s", event_json->valuestring);
    return false;
  }

  effect_config_t effect_config = profile->event_effects[event_type].effect_config;
  effect_config.effect          = led_effects_id_to_enum(effect_json->valuestring);
  if (brightness_json && cJSON_IsNumber(brightness_json)) {
    effect_config.brightness = brightness_json->valueint;
  }
  if (speed_json && cJSON_IsNumber(speed_json)) {
    effect_config.speed = speed_json->valueint;
  }
  if (color_json && cJSON_IsNumber(color_json)) {
    effect_config.color1 = color_json->valueint;
  }
  if (segment_start && cJSON_IsNumber(segment_start)) {
    effect_config.segment_start = segment_start->valueint;
  }
  if (segment_length && cJSON_IsNumber(segment_length)) {
    effect_config.segment_length = segment_length->valueint;
  }
  if (reverse_json && cJSON_IsBool(reverse_json)) {
    effect_config.reverse = cJSON_IsTrue(reverse_json);
  }
  effect_config.sync_mode = SYNC_OFF;

  uint16_t duration       = duration_json && cJSON_IsNumber(duration_json) ? duration_json->valueint : profile->event_effects[event_type].duration_ms;
  uint8_t priority        = priority_json && cJSON_IsNumber(priority_json) ? priority_json->valueint : profile->event_effects[event_type].priority;
  bool enabled            = enabled_json ? cJSON_IsTrue(enabled_json) : profile->event_effects[event_type].enabled;

  if (!config_manager_set_event_effect(profile_id, event_type, &effect_config, duration, priority)) {
    return false;
  }
  config_manager_set_event_enabled(profile_id, event_type, enabled);

  profile->event_effects[event_type].event = event_type;
  memcpy(&profile->event_effects[event_type].effect_config, &effect_config, sizeof(effect_config_t));
  profile->event_effects[event_type].duration_ms = duration;
  profile->event_effects[event_type].priority    = priority;
  profile->event_effects[event_type].enabled     = enabled;

  if (action_type_json && cJSON_IsNumber(action_type_json)) {
    int action_type = action_type_json->valueint;
    if (action_type >= EVENT_ACTION_APPLY_EFFECT && action_type <= EVENT_ACTION_SWITCH_PROFILE) {
      profile->event_effects[event_type].action_type = (event_action_type_t)action_type;
    }
  }
  if (profile_id_json && cJSON_IsNumber(profile_id_json)) {
    profile->event_effects[event_type].profile_id = profile_id_json->valueint;
  }

  return true;
}

static esp_err_t events_get_handler(httpd_req_t *req) {
  cJSON *root         = cJSON_CreateObject();
  cJSON *events_array = cJSON_CreateArray();

  // Itérer à travers tous les événements CAN (sauf CAN_EVENT_NONE)
  for (int i = CAN_EVENT_TURN_LEFT; i < CAN_EVENT_MAX; i++) {
    can_event_type_t event_type = (can_event_type_t)i;
    can_event_effect_t event_effect;

    // Obtenir la configuration de l'événement
    if (config_manager_get_effect_for_event(event_type, &event_effect)) {
      cJSON *event_obj = cJSON_CreateObject();

      // Utiliser des IDs alphanumériques
      cJSON_AddStringToObject(event_obj, "ev", config_manager_enum_to_id(event_effect.event));
      cJSON_AddStringToObject(event_obj, "fx", led_effects_enum_to_id(event_effect.effect_config.effect));
      cJSON_AddNumberToObject(event_obj, "br", event_effect.effect_config.brightness);
      cJSON_AddNumberToObject(event_obj, "sp", event_effect.effect_config.speed);
      cJSON_AddNumberToObject(event_obj, "c1", event_effect.effect_config.color1);
      cJSON_AddBoolToObject(event_obj, "rv", event_effect.effect_config.reverse);
      cJSON_AddNumberToObject(event_obj, "dur", event_effect.duration_ms);
      cJSON_AddNumberToObject(event_obj, "pri", event_effect.priority);
      cJSON_AddBoolToObject(event_obj, "en", event_effect.enabled);
      cJSON_AddNumberToObject(event_obj, "at", event_effect.action_type);
      cJSON_AddNumberToObject(event_obj, "pid", event_effect.profile_id);
      cJSON_AddBoolToObject(event_obj, "csp", config_manager_event_can_switch_profile(event_type));
      cJSON_AddNumberToObject(event_obj, "st", event_effect.effect_config.segment_start);
      cJSON_AddNumberToObject(event_obj, "ln", event_effect.effect_config.segment_length);

      cJSON_AddItemToArray(events_array, event_obj);
    }
  }

  cJSON_AddItemToObject(root, "events", events_array);

  char *json_string = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_string);

  free(json_string);
  cJSON_Delete(root);

  return ESP_OK;
}

// Handler pour mettre à jour les événements CAN (POST)
static esp_err_t events_post_handler(httpd_req_t *req) {
  size_t content_len = req->content_len;

  // Allouer de la mémoire pour le contenu (peut être grand)
  if (content_len > BUFFER_SIZE_EVENT_MAX) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too large");
    return ESP_FAIL;
  }

  char *content = (char *)malloc(content_len + 1);
  if (content == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
    return ESP_FAIL;
  }

  cJSON *root = NULL;
  if (parse_json_request(req, content, content_len + 1, &root) != ESP_OK) {
    free(content);
    return ESP_FAIL;
  }
  free(content);

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

  // Charger le profil une seule fois (allouer dynamiquement pour éviter stack
  // overflow)
  config_profile_t *profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (profile == NULL) {
    ESP_LOGE(TAG_WEBSERVER, "Erreur allocation mémoire");
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
  cJSON *event_obj  = NULL;
  cJSON_ArrayForEach(event_obj, events_array) {
    if (apply_event_update_from_json(profile, profile_id, event_obj)) {
      updated_count++;
    }
  }

  // Sauvegarder le profil UNE SEULE FOIS après avoir traité tous les événements
  config_manager_save_profile(profile_id, profile);
  free(profile);

  cJSON_Delete(root);

  // Réponse
  cJSON *response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "st", "ok");
  cJSON_AddNumberToObject(response, "upd", updated_count);

  const char *json_string = cJSON_PrintUnformatted(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_string);

  free((void *)json_string);
  cJSON_Delete(response);

  ESP_LOGI(TAG_WEBSERVER, "%d événements CAN mis à jour", updated_count);

  return ESP_OK;
}

// ============================================================================
// HANDLERS AUDIO
// ============================================================================

// Handler GET /api/audio/status - Statut et configuration du micro
static esp_err_t audio_status_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  audio_config_t config;
  audio_input_get_config(&config);

  cJSON_AddBoolToObject(root, "en", audio_input_is_enabled());
  cJSON_AddNumberToObject(root, "sen", config.sensitivity);
  cJSON_AddNumberToObject(root, "gn", config.gain);
  cJSON_AddBoolToObject(root, "ag", config.auto_gain);
  cJSON_AddBoolToObject(root, "ffe", config.fft_enabled);

  const char *json_str = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);

  cJSON_free((void *)json_str);
  cJSON_Delete(root);
  return ESP_OK;
}

// Handler POST /api/audio/enable - Activer/désactiver le micro
static esp_err_t audio_enable_handler(httpd_req_t *req) {
  char buf[BUFFER_SIZE_SMALL];
  cJSON *root = NULL;
  if (parse_json_request(req, buf, sizeof(buf), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
  if (cJSON_IsBool(enabled)) {
    bool enable = cJSON_IsTrue(enabled);
    if (audio_input_set_enabled(enable)) {
      audio_input_save_config();
      ESP_LOGI(TAG_WEBSERVER, "Micro %s", enable ? "activé" : "désactivé");
    } else {
      cJSON_Delete(root);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to toggle audio");
      return ESP_FAIL;
    }
  }

  cJSON_Delete(root);

  cJSON *response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "ok", true);
  const char *json_str = cJSON_PrintUnformatted(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);
  cJSON_free((void *)json_str);
  cJSON_Delete(response);

  return ESP_OK;
}

// Handler POST /api/audio/config - Mettre à jour la configuration audio
static esp_err_t audio_config_handler(httpd_req_t *req) {
  char buf[BUFFER_SIZE_MEDIUM];
  cJSON *root = NULL;
  if (parse_json_request(req, buf, sizeof(buf), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  audio_config_t config;
  audio_input_get_config(&config);

  // Mettre à jour les paramètres fournis
  cJSON *sensitivity = cJSON_GetObjectItem(root, "sen");
  if (cJSON_IsNumber(sensitivity)) {
    config.sensitivity = (uint8_t)sensitivity->valueint;
    audio_input_set_sensitivity(config.sensitivity);
  }

  cJSON *gain = cJSON_GetObjectItem(root, "gn");
  if (cJSON_IsNumber(gain)) {
    config.gain = (uint8_t)gain->valueint;
    audio_input_set_gain(config.gain);
  }

  cJSON *autoGain = cJSON_GetObjectItem(root, "ag");
  if (cJSON_IsBool(autoGain)) {
    config.auto_gain = cJSON_IsTrue(autoGain);
    audio_input_set_auto_gain(config.auto_gain);
  }

  cJSON *fftEnabled = cJSON_GetObjectItem(root, "ffe");
  if (cJSON_IsBool(fftEnabled)) {
    config.fft_enabled = cJSON_IsTrue(fftEnabled);
    audio_input_set_fft_enabled(config.fft_enabled);
  }

  // Sauvegarder la configuration
  audio_input_save_config();

  cJSON_Delete(root);

  cJSON *response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "ok", true);
  const char *json_str = cJSON_PrintUnformatted(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);
  cJSON_free((void *)json_str);
  cJSON_Delete(response);

  ESP_LOGI(TAG_WEBSERVER, "Configuration audio mise à jour");
  return ESP_OK;
}

// Handler GET /api/audio/data - Données audio en temps réel (avec FFT si
// activé)
static esp_err_t audio_data_handler(httpd_req_t *req) {
  audio_data_t audio_data;
  audio_fft_data_t fft_data;
  cJSON *root = cJSON_CreateObject();

  // Données audio de base
  if (audio_input_get_data(&audio_data)) {
    cJSON_AddNumberToObject(root, "amp", audio_data.amplitude);
    cJSON_AddNumberToObject(root, "ba", audio_data.bass);
    cJSON_AddNumberToObject(root, "md", audio_data.mid);
    cJSON_AddNumberToObject(root, "tr", audio_data.treble);
    cJSON_AddNumberToObject(root, "bpm", audio_data.bpm);
    cJSON_AddBoolToObject(root, "bd", audio_data.beat_detected);
    cJSON_AddBoolToObject(root, "av", true);
  } else {
    cJSON_AddBoolToObject(root, "av", false);
  }

  // Données FFT (si activées)
  if (audio_input_is_fft_enabled() && audio_input_get_fft_data(&fft_data)) {
    // Créer un objet FFT
    cJSON *fft_obj     = cJSON_CreateObject();

    // Ajouter les bandes FFT
    cJSON *bands_array = cJSON_CreateArray();
    for (int i = 0; i < AUDIO_FFT_BANDS; i++) {
      cJSON_AddItemToArray(bands_array, cJSON_CreateNumber(fft_data.bands[i]));
    }
    cJSON_AddItemToObject(fft_obj, "bands", bands_array);

    // Ajouter les métadonnées FFT
    cJSON_AddNumberToObject(fft_obj, "pf", fft_data.peak_freq);
    cJSON_AddNumberToObject(fft_obj, "sc", fft_data.spectral_centroid);
    cJSON_AddNumberToObject(fft_obj, "db", fft_data.dominant_band);
    cJSON_AddNumberToObject(fft_obj, "be", fft_data.bass_energy);
    cJSON_AddNumberToObject(fft_obj, "me", fft_data.mid_energy);
    cJSON_AddNumberToObject(fft_obj, "te", fft_data.treble_energy);
    cJSON_AddBoolToObject(fft_obj, "kd", fft_data.kick_detected);
    cJSON_AddBoolToObject(fft_obj, "sd", fft_data.snare_detected);
    cJSON_AddBoolToObject(fft_obj, "vd", fft_data.vocal_detected);
    cJSON_AddBoolToObject(fft_obj, "av", true);

    cJSON_AddItemToObject(root, "fft", fft_obj);
  } else {
    // FFT non disponible
    cJSON *fft_obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(fft_obj, "av", false);
    cJSON_AddItemToObject(root, "fft", fft_obj);
  }

  const char *json_str = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);

  cJSON_free((void *)json_str);
  cJSON_Delete(root);
  return ESP_OK;
}

// Handler POST /api/audio/fft - Activer/désactiver la FFT
// NOTE: Cette route est dépréciée. Utilisez /api/audio/config pour configurer
// et /api/save pour sauvegarder
static esp_err_t audio_fft_enable_handler(httpd_req_t *req) {
  char content[100];
  cJSON *root = NULL;
  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *enabled = cJSON_GetObjectItem(root, "en");
  if (enabled != NULL && cJSON_IsBool(enabled)) {
    audio_input_set_fft_enabled(cJSON_IsTrue(enabled));
  }

  cJSON_Delete(root);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\"}");

  ESP_LOGI(TAG_WEBSERVER, "FFT mode: %s (not saved)", cJSON_IsTrue(enabled) ? "enabled" : "disabled");
  return ESP_OK;
}

// Handler GET /api/audio/fft/data - Données FFT en temps réel
static esp_err_t audio_fft_data_handler(httpd_req_t *req) {
  audio_fft_data_t fft_data;
  cJSON *root = cJSON_CreateObject();

  if (audio_input_get_fft_data(&fft_data)) {
    // Ajouter les bandes FFT
    cJSON *bands_array = cJSON_CreateArray();
    for (int i = 0; i < AUDIO_FFT_BANDS; i++) {
      cJSON_AddItemToArray(bands_array, cJSON_CreateNumber(fft_data.bands[i]));
    }
    cJSON_AddItemToObject(root, "bands", bands_array);

    // Ajouter les métadonnées
    cJSON_AddNumberToObject(root, "pf", fft_data.peak_freq);
    cJSON_AddNumberToObject(root, "sc", fft_data.spectral_centroid);
    cJSON_AddNumberToObject(root, "db", fft_data.dominant_band);
    cJSON_AddNumberToObject(root, "be", fft_data.bass_energy);
    cJSON_AddNumberToObject(root, "me", fft_data.mid_energy);
    cJSON_AddNumberToObject(root, "te", fft_data.treble_energy);
    cJSON_AddBoolToObject(root, "kd", fft_data.kick_detected);
    cJSON_AddBoolToObject(root, "sd", fft_data.snare_detected);
    cJSON_AddBoolToObject(root, "vd", fft_data.vocal_detected);
    cJSON_AddBoolToObject(root, "av", true);
  } else {
    cJSON_AddBoolToObject(root, "av", false);
  }

  const char *json_str = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);

  cJSON_free((void *)json_str);
  cJSON_Delete(root);
  return ESP_OK;
}

// Handler GET /api/audio/fft/status - Statut FFT
static esp_err_t audio_fft_status_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();

  cJSON_AddBoolToObject(root, "en", audio_input_is_fft_enabled());
  cJSON_AddNumberToObject(root, "bands", AUDIO_FFT_BANDS);
  cJSON_AddNumberToObject(root, "sr", AUDIO_SAMPLE_RATE);
  cJSON_AddNumberToObject(root, "sz", AUDIO_FFT_SIZE);

  const char *json_str = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);

  cJSON_free((void *)json_str);
  cJSON_Delete(root);
  return ESP_OK;
}

esp_err_t web_server_init(void) {
  ESP_LOGI(TAG_WEBSERVER, "Serveur web initialise");
  return ESP_OK;
}

esp_err_t web_server_start(void) {
  httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
  config.server_port      = WEB_SERVER_PORT;
  config.max_uri_handlers = HTTP_MAX_URI_HANDLERS;
  config.max_open_sockets = HTTP_MAX_OPEN_SOCKETS;
  config.lru_purge_enable = true; // Ferme les plus anciennes connexions si limite atteinte
  // config.core_id          = 1;    // Serveur web sur core 1 (WiFi sur core 0)

  ESP_LOGI(TAG_WEBSERVER, "Demarrage du serveur web sur port %d", config.server_port);

  if (httpd_start(&server, &config) == ESP_OK) {
    static static_file_route_t static_files[] = {{"/", index_html_gz_start, index_html_gz_end, "text/html", "CACHE_ONE_YEAR", "gzip"},
                                                 {"/generate_204", index_html_gz_start, index_html_gz_end, "text/html", "CACHE_ONE_YEAR", "gzip"},
                                                 {"/i18n.js", i18n_js_gz_start, i18n_js_gz_end, "text/javascript", "CACHE_ONE_YEAR", "gzip"},
                                                 {"/script.js", script_js_gz_start, script_js_gz_end, "text/javascript", "CACHE_ONE_YEAR", "gzip"},
                                                 {"/style.css", style_css_gz_start, style_css_gz_end, "text/css", "CACHE_ONE_YEAR", "gzip"},
                                                 {"/carlightsync64.png", carlightsync64_png_start, carlightsync64_png_end, "image/png", "CACHE_ONE_YEAR", NULL}};
    int num_static_files                      = sizeof(static_files) / sizeof(static_files[0]);

    for (int i = 0; i < num_static_files; i++) {
      httpd_uri_t uri = {.uri = static_files[i].uri, .method = HTTP_GET, .handler = static_file_handler, .user_ctx = &static_files[i]};
      httpd_register_uri_handler(server, &uri);
    }

    httpd_uri_t config_uri = {.uri = "/api/config", .method = HTTP_GET, .handler = config_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &config_uri);

    httpd_uri_t status_uri = {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &status_uri);

    httpd_uri_t effect_uri = {.uri = "/api/effect", .method = HTTP_POST, .handler = effect_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &effect_uri);

    httpd_uri_t save_uri = {.uri = "/api/save", .method = HTTP_POST, .handler = save_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &save_uri);

    // Routes pour les profils
    httpd_uri_t profiles_uri = {.uri = "/api/profiles", .method = HTTP_GET, .handler = profiles_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &profiles_uri);

    httpd_uri_t profile_activate_uri = {.uri = "/api/profile/activate", .method = HTTP_POST, .handler = profile_activate_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &profile_activate_uri);

    httpd_uri_t profile_create_uri = {.uri = "/api/profile/create", .method = HTTP_POST, .handler = profile_create_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &profile_create_uri);

    httpd_uri_t profile_delete_uri = {.uri = "/api/profile/delete", .method = HTTP_POST, .handler = profile_delete_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &profile_delete_uri);

    httpd_uri_t profile_rename_uri = {.uri = "/api/profile/rename", .method = HTTP_POST, .handler = profile_rename_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &profile_rename_uri);

    httpd_uri_t factory_reset_uri = {.uri = "/api/factory-reset", .method = HTTP_POST, .handler = factory_reset_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &factory_reset_uri);

    httpd_uri_t profile_update_uri = {.uri = "/api/profile/update", .method = HTTP_POST, .handler = profile_update_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &profile_update_uri);

    httpd_uri_t event_effect_uri = {.uri = "/api/event-effect", .method = HTTP_POST, .handler = event_effect_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &event_effect_uri);

    httpd_uri_t profile_export_uri = {.uri = "/api/profile/export", .method = HTTP_GET, .handler = profile_export_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &profile_export_uri);

    httpd_uri_t profile_import_uri = {.uri = "/api/profile/import", .method = HTTP_POST, .handler = profile_import_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &profile_import_uri);

    // Routes OTA
    httpd_uri_t ota_info_uri = {.uri = "/api/ota/info", .method = HTTP_GET, .handler = ota_info_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &ota_info_uri);

    httpd_uri_t ota_upload_uri = {.uri = "/api/ota/upload", .method = HTTP_POST, .handler = ota_upload_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &ota_upload_uri);

    httpd_uri_t ota_restart_uri = {.uri = "/api/ota/restart", .method = HTTP_POST, .handler = ota_restart_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &ota_restart_uri);

    // Route de simulation
    httpd_uri_t simulate_event_uri = {.uri = "/api/simulate/event", .method = HTTP_POST, .handler = simulate_event_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &simulate_event_uri);

    httpd_uri_t stop_event_uri = {.uri = "/api/stop/event", .method = HTTP_POST, .handler = stop_event_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &stop_event_uri);

    // Routes pour obtenir les listes d'effets et d'événements
    httpd_uri_t effects_list_uri = {.uri = "/api/effects", .method = HTTP_GET, .handler = effects_list_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &effects_list_uri);

    httpd_uri_t event_types_list_uri = {.uri = "/api/event-types", .method = HTTP_GET, .handler = event_types_list_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &event_types_list_uri);

    // Route POST /api/config pour configurer le matériel LED
    httpd_uri_t config_post_uri = {.uri = "/api/config", .method = HTTP_POST, .handler = config_post_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &config_post_uri);

    // Route POST /api/events pour mettre à jour les événements CAN
    // IMPORTANT: Enregistrer POST AVANT GET pour éviter les conflits
    httpd_uri_t events_post_uri = {.uri = "/api/events", .method = HTTP_POST, .handler = events_post_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &events_post_uri);

    httpd_uri_t event_single_post_uri = {.uri = "/api/events/update", .method = HTTP_POST, .handler = event_single_post_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &event_single_post_uri);

    // Route GET /api/events pour obtenir les événements CAN
    httpd_uri_t events_get_uri = {.uri = "/api/events", .method = HTTP_GET, .handler = events_get_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &events_get_uri);

    // Routes audio (micro INMP441)
    httpd_uri_t audio_status_uri = {.uri = "/api/audio/status", .method = HTTP_GET, .handler = audio_status_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &audio_status_uri);

    httpd_uri_t audio_enable_uri = {.uri = "/api/audio/enable", .method = HTTP_POST, .handler = audio_enable_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &audio_enable_uri);

    httpd_uri_t audio_config_uri = {.uri = "/api/audio/config", .method = HTTP_POST, .handler = audio_config_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &audio_config_uri);

    httpd_uri_t audio_data_uri = {.uri = "/api/audio/data", .method = HTTP_GET, .handler = audio_data_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &audio_data_uri);

    // Routes FFT
    httpd_uri_t audio_fft_status_uri = {.uri = "/api/audio/fft/status", .method = HTTP_GET, .handler = audio_fft_status_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &audio_fft_status_uri);

    httpd_uri_t audio_fft_enable_uri = {.uri = "/api/audio/fft/enable", .method = HTTP_POST, .handler = audio_fft_enable_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &audio_fft_enable_uri);

    httpd_uri_t audio_fft_data_uri = {.uri = "/api/audio/fft/data", .method = HTTP_GET, .handler = audio_fft_data_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &audio_fft_data_uri);

    // Alias pour les nouveaux endpoints système
    httpd_uri_t system_restart_uri = {.uri = "/api/system/restart", .method = HTTP_POST, .handler = ota_restart_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &system_restart_uri);

    httpd_uri_t system_factory_reset_uri = {.uri = "/api/system/factory-reset", .method = HTTP_POST, .handler = factory_reset_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &system_factory_reset_uri);

    // Enregistrer le handler d'erreur 404 pour le portail captif
    // Toutes les URLs non trouvées seront redirigées vers la page principale
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_portal_404_handler);

    ESP_LOGI(TAG_WEBSERVER, "Serveur web démarré avec support portail captif (handler 404)");
    return ESP_OK;
  }

  ESP_LOGE(TAG_WEBSERVER, "Erreur démarrage serveur web");
  return ESP_FAIL;
}

static esp_err_t event_single_post_handler(httpd_req_t *req) {
  size_t content_len = req->content_len;

  if (content_len > MAX_CONTENT_LENGTH) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too large");
    return ESP_FAIL;
  }

  char *content = (char *)malloc(content_len + 1);
  if (content == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
    return ESP_FAIL;
  }

  cJSON *root = NULL;
  if (parse_json_request(req, content, content_len + 1, &root) != ESP_OK) {
    free(content);
    return ESP_FAIL;
  }
  free(content);

  cJSON *event_obj = cJSON_GetObjectItem(root, "event");
  if (event_obj == NULL || !cJSON_IsObject(event_obj)) {
    if (cJSON_IsObject(root)) {
      event_obj = root;
    } else {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid event payload");
      cJSON_Delete(root);
      return ESP_FAIL;
    }
  }

  int profile_id = config_manager_get_active_profile_id();
  if (profile_id < 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No active profile");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  config_profile_t *profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (profile == NULL) {
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

  bool updated = apply_event_update_from_json(profile, profile_id, event_obj);
  if (updated) {
    config_manager_save_profile(profile_id, profile);
  }

  free(profile);
  cJSON_Delete(root);

  if (!updated) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Event update failed");
    return ESP_FAIL;
  }

  cJSON *response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "st", "ok");
  const char *json_string = cJSON_PrintUnformatted(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_string);
  free((void *)json_string);
  cJSON_Delete(response);
  return ESP_OK;
}

esp_err_t web_server_stop(void) {
  if (server) {
    httpd_stop(server);
    server = NULL;
    ESP_LOGI(TAG_WEBSERVER, "Serveur web arrêté");
  }
  return ESP_OK;
}

bool web_server_is_running(void) {
  return server != NULL;
}
