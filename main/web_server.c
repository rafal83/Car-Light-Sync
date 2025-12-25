/**
 * @file web_server.c
 * @brief HTTP server for the web interface and REST API
 *
 * Handles:
 * - HTTP server with static routes (HTML, CSS, JS) served as GZIP
 * - REST API for profiles, CAN events, and LED effects
 * - Captive portal for initial WiFi configuration
 * - OTA updates (Over-The-Air)
 * - Vehicle state streaming and CAN event streaming
 */

#include "web_server.h"

#include "audio_input.h"
#include "cJSON.h"
#include "can_bus.h"
#include "canserver_udp_server.h" // For the CANServer UDP service
#include "config.h"
#include "config_manager.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "espnow_link.h"
#include "gvret_tcp_server.h" // For the GVRET TCP service
#include "led_effects.h"
#include "log_stream.h" // For real-time log streaming
#include "nvs_flash.h"
#include "settings_manager.h"
#include "spiffs_storage.h"
#include "ota_update.h"
#include "vehicle_can_unified.h"
#include "wifi_manager.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

// Buffer size constants
#define BUFFER_SIZE_SMALL 128
#define BUFFER_SIZE_MEDIUM 256
#define BUFFER_SIZE_LARGE 512
#define BUFFER_SIZE_JSON 1024
#define BUFFER_SIZE_PROFILE 16384
#define BUFFER_SIZE_EVENT_MAX 16384

// System limit constants
#define LED_COUNT_MIN 1
#define LED_COUNT_MAX 200
#define MAX_CONTENT_LENGTH 4096

// Timing constants
#define VEHICLE_STATE_TIMEOUT_MS 5000
#define TASK_DELAY_MS 1000
#define RETRY_DELAY_BASE_MS 20
#define RETRY_DELAY_MAX_MS 500

// Server configuration constants
#define HTTP_MAX_URI_HANDLERS 60
#define HTTP_MAX_OPEN_SOCKETS 13

// Default configuration constants
#define DEFAULT_BRIGHTNESS 128
#define DEFAULT_SPEED 50
#define DEFAULT_PRIORITY 100

// Cache control HTTP
#define CACHE_ONE_YEAR "public, max-age=31536000"

/**
 * Shortened JSON keys used in the REST API (to reduce payload size)
 *
 * LED EFFECTS:
 *   fx  = effect (alphanumeric effect ID)
 *   br  = brightness (0-255)
 *   sp  = speed (0-100)
 *   c1  = color1 (format 0xRRGGBB)
 *   c2  = color2
 *   c3  = color3
 *   rv  = reverse (bool: animation direction)
 *   ar  = audio_reactive (bool)
 *   sm  = sync_mode
 *
 * CAN EVENTS:
 *   ev  = event (alphanumeric event ID)
 *   dur = duration_ms (duration in milliseconds)
 *   pri = priority (0-255, higher = more priority)
 *   en  = enabled (bool)
 *   at  = action_type (0=effect, 1=switch profile)
 *   csp = can_switch_profile (bool)
 *
 * LED SEGMENTS:
 *   st  = segment_start (start index, 0-based)
 *   ln  = segment_length (length, 0=full strip)
 *
 * PROFILES:
 *   pid = profile_id (0-9)
 *
 * RESPONSES:
 *   st  = status ("ok" or "error")
 *   msg = message (descriptive text)
 *   pg  = progress (0-100)
 *   upd = updated (number of updated elements)
 *   rr  = requires_restart (bool)
 *
 * AUDIO/FFT:
 *   sen = sensitivity (microphone sensitivity)
 *   gn  = gain
 *   sr  = sample_rate
 *
 * VEHICLE:
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

// Main page HTML (embedded, GZIP-compressed version)
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

// Structure for static file handlers
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

  memcpy(&current_vehicle_state, state, sizeof(vehicle_state_t));
}

/**
 * @brief Parse an HTTP JSON request and return the cJSON object
 *
 * @param req HTTP request
 * @param buffer Buffer to receive the content
 * @param buffer_size Buffer size
 * @param out_json Pointer to receive the parsed cJSON object (must be freed with cJSON_Delete)
 * @return ESP_OK on success, ESP_FAIL on error (HTTP response already sent)
 */
static esp_err_t parse_json_request(httpd_req_t *req, char *buffer, size_t buffer_size, cJSON **out_json) {
  if (!req || !buffer || !out_json || buffer_size == 0) {
    return ESP_FAIL;
  }

  // Ensure there is a body to read
  if (req->content_len == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request body");
    return ESP_FAIL;
  }

  // Validate that the body does not exceed buffer size
  if (req->content_len >= buffer_size) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request body too large");
    return ESP_FAIL;
  }

  size_t remaining = req->content_len;
  size_t offset    = 0;

  // Read the entire body, even if httpd_req_recv returns fragments
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

  *out_json      = cJSON_Parse(buffer);
  if (*out_json == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  return ESP_OK;
}

static esp_err_t static_file_handler(httpd_req_t *req) {
  static_file_route_t *route = (static_file_route_t *)req->user_ctx;
  const size_t file_size     = (route->end - route->start);

  // Check if the connection is still valid
  int sockfd                 = httpd_req_to_sockfd(req);
  if (sockfd < 0) {
    ESP_LOGW(TAG_WEBSERVER, "Invalid socket for URI %s", route->uri);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, route->content_type);
  if (route->cache_control) {
    httpd_resp_set_hdr(req, "Cache-Control", route->cache_control);
  }
  if (route->content_encoding) {
    httpd_resp_set_hdr(req, "Content-Encoding", route->content_encoding);
  }

  // Close the connection after sending (disables keep-alive for this request)
  httpd_resp_set_hdr(req, "Connection", "close");

// Always send by chunks to avoid EAGAIN, even for small files
#define CHUNK_SIZE 1024 // Reduced to 1 KB to avoid buffer overflow
#define MAX_RETRIES 10  // Increased to 10 retries

  const uint8_t *data = route->start;
  size_t remaining    = file_size;
  esp_err_t err       = ESP_OK;

  ESP_LOGD(TAG_WEBSERVER, "Envoi fichier %s (%d bytes)", route->uri, file_size);

  while (remaining > 0) {
    size_t chunk_size = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;

    // Retry up to MAX_RETRIES when EAGAIN occurs
    int retries       = 0;
    while (retries < MAX_RETRIES) {
      err = httpd_resp_send_chunk(req, (const char *)data, chunk_size);

      if (err == ESP_OK) {
        break; // Success
      }

      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        retries++;
        // Exponential backoff: 20 ms, 40 ms, 80 ms, etc.
        int delay_ms = RETRY_DELAY_BASE_MS * (1 << (retries - 1));
        if (delay_ms > RETRY_DELAY_MAX_MS)
          delay_ms = RETRY_DELAY_MAX_MS;
        ESP_LOGD(TAG_WEBSERVER, "EAGAIN for %s, retry %d/%d (wait %dms)", route->uri, retries, MAX_RETRIES, delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
      } else {
        // Other error, abort
        ESP_LOGW(TAG_WEBSERVER, "Chunk send error for %s: %s (errno: %d)", route->uri, esp_err_to_name(err), errno);
        return err;
      }
    }

    if (err != ESP_OK) {
      ESP_LOGW(TAG_WEBSERVER, "Failed to send after %d retries for %s", MAX_RETRIES, route->uri);
      return err;
    }

    data += chunk_size;
    remaining -= chunk_size;

    // Small delay between chunks to let the TCP buffer drain
    if (remaining > 0) {
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }

  // Finish the chunked send
  err = httpd_resp_send_chunk(req, NULL, 0);
  if (err != ESP_OK) {
    ESP_LOGW(TAG_WEBSERVER, "Error finishing chunks for %s: %s", route->uri, esp_err_to_name(err));
  } else {
    ESP_LOGD(TAG_WEBSERVER, "File %s sent successfully", route->uri);
  }

  return err;
}

// 404 handler for the captive portal
// Redirects all unknown URLs to the main page
static esp_err_t captive_portal_404_handler(httpd_req_t *req, httpd_err_code_t err) {
  if (err == HTTPD_404_NOT_FOUND) {
    // Send the main page (index.html) for all not-found requests
    const size_t file_size = (index_html_gz_end - index_html_gz_start);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    ESP_LOGD(TAG_WEBSERVER, "Portail captif: redirection de %s vers index.html", req->uri);
    return httpd_resp_send(req, (const char *)index_html_gz_start, file_size);
  }

  // For other errors, return the default
  return ESP_FAIL;
}

// Handler to get status
static esp_err_t status_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();

  // Statut WiFi
  wifi_status_t wifi_status;
  wifi_manager_get_status(&wifi_status);
  cJSON_AddBoolToObject(root, "wc", wifi_status.sta_connected);
  cJSON_AddStringToObject(root, "wip", wifi_status.sta_ip);

  // ESP-NOW test info (poll-friendly)
  cJSON *esn = cJSON_CreateObject();
  cJSON_AddNumberToObject(esn, "lt_us", (double)espnow_link_get_last_test_rx_us());
  cJSON_AddItemToObject(root, "esn", esn);

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

  // Vehicle status
  uint32_t now        = xTaskGetTickCount();
  bool vehicle_active = (now - current_vehicle_state.last_update_ms) < pdMS_TO_TICKS(VEHICLE_STATE_TIMEOUT_MS);
  cJSON_AddBoolToObject(root, "va", vehicle_active);

  // Full vehicle data
  cJSON *vehicle = cJSON_CreateObject();

  // General state
  cJSON_AddNumberToObject(vehicle, "g", current_vehicle_state.gear);
  cJSON_AddNumberToObject(vehicle, "s", current_vehicle_state.speed_kph);
  cJSON_AddNumberToObject(vehicle, "bp", current_vehicle_state.brake_pressed);
  cJSON_AddNumberToObject(vehicle, "ap", current_vehicle_state.accel_pedal_pos);

  // Portes
  cJSON *doors = cJSON_CreateObject();
  cJSON_AddBoolToObject(doors, "fl", current_vehicle_state.door_front_left_open);
  cJSON_AddBoolToObject(doors, "fr", current_vehicle_state.door_front_right_open);
  cJSON_AddBoolToObject(doors, "rl", current_vehicle_state.door_rear_left_open);
  cJSON_AddBoolToObject(doors, "rr", current_vehicle_state.door_rear_right_open);
  cJSON_AddBoolToObject(doors, "t", current_vehicle_state.trunk_open);
  cJSON_AddBoolToObject(doors, "f", current_vehicle_state.frunk_open);
  cJSON_AddItemToObject(vehicle, "doors", doors);

  // Verrouillage
  cJSON_AddBoolToObject(vehicle, "lk", current_vehicle_state.locked);

  // Lights
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

  // Energie / puissance
  cJSON_AddNumberToObject(vehicle, "pe", current_vehicle_state.pack_energy);
  cJSON_AddNumberToObject(vehicle, "re", current_vehicle_state.remaining_energy);
  cJSON_AddNumberToObject(vehicle, "rp", current_vehicle_state.rear_power);
  cJSON_AddNumberToObject(vehicle, "rpl", current_vehicle_state.rear_power_limit);
  cJSON_AddNumberToObject(vehicle, "fp", current_vehicle_state.front_power);
  cJSON_AddNumberToObject(vehicle, "fpl", current_vehicle_state.front_power_limit);
  cJSON_AddNumberToObject(vehicle, "mr", current_vehicle_state.max_regen);
  cJSON_AddNumberToObject(vehicle, "tt", current_vehicle_state.train_type);

  // Batterie et autres
  cJSON_AddNumberToObject(vehicle, "blv", current_vehicle_state.battery_voltage_LV);
  cJSON_AddNumberToObject(vehicle, "bhv", current_vehicle_state.battery_voltage_HV);
  cJSON_AddNumberToObject(vehicle, "odo", current_vehicle_state.odometer_km);

  // Safety
  cJSON *safety = cJSON_CreateObject();
  cJSON_AddBoolToObject(safety, "bsl", current_vehicle_state.blindspot_left);
  cJSON_AddBoolToObject(safety, "bsla", current_vehicle_state.blindspot_left_alert);
  cJSON_AddBoolToObject(safety, "bsr", current_vehicle_state.blindspot_right);
  cJSON_AddBoolToObject(safety, "bsra", current_vehicle_state.blindspot_right_alert);
  cJSON_AddBoolToObject(safety, "scl", current_vehicle_state.side_collision_left);
  cJSON_AddBoolToObject(safety, "scr", current_vehicle_state.side_collision_right);
  cJSON_AddBoolToObject(safety, "nm", current_vehicle_state.night_mode);
  cJSON_AddNumberToObject(safety, "bri", current_vehicle_state.brightness);
  cJSON_AddBoolToObject(safety, "sm", current_vehicle_state.sentry_mode);
  cJSON_AddNumberToObject(safety, "ap", current_vehicle_state.autopilot);
  cJSON_AddNumberToObject(safety, "apa1", current_vehicle_state.autopilot_alert_lv1);
  cJSON_AddNumberToObject(safety, "apa2", current_vehicle_state.autopilot_alert_lv2);
  cJSON_AddBoolToObject(safety, "sa", current_vehicle_state.sentry_alert);
  cJSON_AddItemToObject(vehicle, "safety", safety);

  cJSON_AddItemToObject(root, "vehicle", vehicle);

  // Currently applied profile (may change temporarily via events)
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

  // Calcul de la charge CPU (ESP32-C6 mono-core / ESP32-S3 dual-core)
  // Uses FreeRTOS runtime stats (now enabled in sdkconfig)
  static uint32_t last_idle_time     = 0;
  static uint32_t last_total_time    = 0;
  static uint32_t cpu_usage_filtered = 0;

  uint32_t current_total_time        = 0;
  uint32_t cpu_usage                 = 0;

  // Allocate memory for task info (estimate ~30 tasks max for the servers)
  const UBaseType_t max_tasks        = 30;
  TaskStatus_t *task_status_array    = (TaskStatus_t *)malloc(max_tasks * sizeof(TaskStatus_t));

  if (task_status_array != NULL) {
    UBaseType_t task_count     = uxTaskGetSystemState(task_status_array, max_tasks, &current_total_time);

    // Compute total runtime of ALL tasks
    uint32_t total_runtime     = 0;
    uint32_t current_idle_time = 0;

    for (UBaseType_t i = 0; i < task_count; i++) {
      total_runtime += task_status_array[i].ulRunTimeCounter;

      // Find IDLE tasks (name "IDLE" or "IDLE0" single-core, "IDLE0" and "IDLE1" dual-core)
      const char *task_name = task_status_array[i].pcTaskName;
      if (strncmp(task_name, "IDLE", 4) == 0) {
        current_idle_time += task_status_array[i].ulRunTimeCounter;
      }
    }

    free(task_status_array);

    // Compute CPU percentage if previous data is available
    if (last_total_time > 0 && total_runtime > last_total_time) {
      uint32_t idle_delta  = current_idle_time - last_idle_time;
      uint32_t total_delta = total_runtime - last_total_time;

      if (total_delta > 0 && idle_delta <= total_delta) {
        // CPU usage = 100 - (idle_time / total_time * 100)
        cpu_usage = 100 - ((idle_delta * 100) / total_delta);
        if (cpu_usage > 100)
          cpu_usage = 100; // Clamp to 100%

        // Simple filter to smooth variations
        cpu_usage_filtered = (cpu_usage_filtered * 3 + cpu_usage) / 4;
      } else {
        // If deltas are inconsistent, keep the last value
        cpu_usage = cpu_usage_filtered;
      }
    } else {
      // Reset if overflow or inconsistent
      cpu_usage = cpu_usage_filtered;
    }

    last_idle_time  = current_idle_time;
    last_total_time = total_runtime;
  } else {
    cpu_usage = cpu_usage_filtered;
  }

  cJSON_AddNumberToObject(root, "cpu", cpu_usage_filtered);

  // Memory usage (heap)
  size_t free_heap          = esp_get_free_heap_size();
  size_t total_heap         = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
  uint32_t mem_used_percent = (total_heap > 0) ? (((total_heap - free_heap) * 100) / total_heap) : 0;
  cJSON_AddNumberToObject(root, "mem", mem_used_percent);

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

  // Add active profile settings (allocate dynamically to avoid stack overflow)
  config_profile_t *profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (profile != NULL && config_manager_get_active_profile(profile)) {
    free(profile);
  } else if (profile != NULL) {
    free(profile);
  }

  // Add LED hardware configuration
  cJSON_AddNumberToObject(root, "lc", config_manager_get_led_count());

  // Global settings (wheel control)
  cJSON_AddBoolToObject(root, "wheel_ctl", config_manager_get_wheel_control_enabled());
  cJSON_AddNumberToObject(root, "wheel_spd", config_manager_get_wheel_control_speed_limit());

  const char *json_string = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_string);

  free((void *)json_string);
  cJSON_Delete(root);

  return ESP_OK;
}

// GET ESP-NOW config (role/type)
static esp_err_t espnow_config_get_handler(httpd_req_t *req) {
  espnow_role_t role             = espnow_link_get_role();
  espnow_slave_type_t slave_type = espnow_link_get_slave_type();
  uint64_t now_us = esp_timer_get_time();
  uint8_t self_mac[6] = {0};
  espnow_link_get_mac(self_mac);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "role", espnow_link_role_to_str(role));
  cJSON_AddStringToObject(root, "type", espnow_link_slave_type_to_str(slave_type));
  cJSON_AddBoolToObject(root, "connected", espnow_link_is_connected());
  cJSON_AddNumberToObject(root, "channel", (double)espnow_link_get_channel());
  cJSON_AddBoolToObject(root, "pairing", espnow_link_is_pairing_active());
  cJSON *self = cJSON_CreateObject();
  char self_mac_str[18];
  snprintf(self_mac_str, sizeof(self_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           self_mac[0], self_mac[1], self_mac[2], self_mac[3], self_mac[4], self_mac[5]);
  cJSON_AddStringToObject(self, "mac", self_mac_str);
  cJSON_AddNumberToObject(self, "device_id", espnow_link_get_device_id());
  cJSON_AddItemToObject(root, "self", self);

  cJSON *types = cJSON_CreateArray();
  for (int t = 0; t < ESP_NOW_SLAVE_MAX; t++) {
    cJSON *item = cJSON_CreateObject();
    const char *name = espnow_link_slave_type_to_str((espnow_slave_type_t)t);
    cJSON_AddStringToObject(item, "id", name);
    cJSON_AddNumberToObject(item, "value", t);
    cJSON_AddItemToArray(types, item);
  }
  cJSON_AddItemToObject(root, "types", types);

  if (role == ESP_NOW_ROLE_SLAVE) {
    espnow_peer_info_t master_info = {0};
    if (espnow_link_get_master_info(&master_info)) {
      cJSON *master = cJSON_CreateObject();
      char mac_str[18];
      snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
               master_info.mac[0], master_info.mac[1], master_info.mac[2],
               master_info.mac[3], master_info.mac[4], master_info.mac[5]);
      cJSON_AddStringToObject(master, "mac", mac_str);
      cJSON_AddStringToObject(master, "type", espnow_link_slave_type_to_str(master_info.type));
      cJSON_AddNumberToObject(master, "device_id", master_info.device_id);
      cJSON_AddNumberToObject(master, "last_seen_us", (double)master_info.last_seen_us);
      uint64_t age_ms = (master_info.last_seen_us && now_us > master_info.last_seen_us)
                            ? (now_us - master_info.last_seen_us) / 1000ULL
                            : 0;
      cJSON_AddNumberToObject(master, "age_ms", (double)age_ms);
      cJSON_AddNumberToObject(master, "channel", (double)master_info.channel);
      cJSON_AddItemToObject(root, "master", master);
    }
  }

  const char *json_string = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_string);
  free((void *)json_string);
  cJSON_Delete(root);
  return ESP_OK;
}

// POST ESP-NOW config (role/type) -> saves to SPIFFS, reboot required to apply
static esp_err_t espnow_config_post_handler(httpd_req_t *req) {
  char content[BUFFER_SIZE_SMALL] = {0};
  cJSON *root = NULL;
  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  const cJSON *role_json = cJSON_GetObjectItem(root, "role");
  const cJSON *type_json = cJSON_GetObjectItem(root, "type");

  espnow_role_t new_role             = espnow_link_get_role();
  espnow_slave_type_t new_slave_type = espnow_link_get_slave_type();

  bool ok = true;
  if (role_json && cJSON_IsString(role_json)) {
    ok = espnow_link_role_from_str(role_json->valuestring, &new_role);
  }
  if (ok && type_json && cJSON_IsString(type_json)) {
    ok = espnow_link_slave_type_from_str(type_json->valuestring, &new_slave_type);
  }

  if (!ok) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid role/type");
    return ESP_FAIL;
  }

  esp_err_t err1 = settings_set_u8("espnow_role", (uint8_t)new_role);
  esp_err_t err2 = settings_set_u8("espnow_type", (uint8_t)new_slave_type);

  // Update the current state so the response and future requests reflect the new choices immediately
  espnow_link_set_role_type(new_role, new_slave_type);

  cJSON_Delete(root);

  if (err1 != ESP_OK || err2 != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save SPIFFS");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG_WEBSERVER, "ESP-NOW config saved: role=%s type=%s (SPIFFS)",
           espnow_link_role_to_str(new_role),
           espnow_link_slave_type_to_str(new_slave_type));

  cJSON *resp = cJSON_CreateObject();
  cJSON_AddStringToObject(resp, "st", "ok");
  cJSON_AddStringToObject(resp, "role", espnow_link_role_to_str(new_role));
  cJSON_AddStringToObject(resp, "type", espnow_link_slave_type_to_str(new_slave_type));
  cJSON_AddBoolToObject(resp, "restart_required", true);

  const char *json_string = cJSON_PrintUnformatted(resp);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_string);
  free((void *)json_string);
  cJSON_Delete(resp);
  return ESP_OK;
}

// POST to enable ESP-NOW pairing mode on the master
static esp_err_t espnow_pairing_post_handler(httpd_req_t *req) {
  char content[BUFFER_SIZE_SMALL] = {0};
  cJSON *root = NULL;
  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
    // parse_json_request already sent an error response
    return ESP_FAIL;
  }

  const cJSON *enable_json = cJSON_GetObjectItem(root, "enable");
  if (!cJSON_IsBool(enable_json)) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'enable' field (must be boolean)");
    return ESP_FAIL;
  }

  bool enable = cJSON_IsTrue(enable_json);
  uint32_t duration_s = 60; // Default duration 60 seconds

  const cJSON *duration_json = cJSON_GetObjectItem(root, "duration");
  if (cJSON_IsNumber(duration_json)) {
    duration_s = duration_json->valueint;
  }

  cJSON_Delete(root);

  esp_err_t err = espnow_link_set_pairing_mode(enable, duration_s);

  if (err == ESP_ERR_NOT_SUPPORTED) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Pairing mode is only available for master role");
      return ESP_FAIL;
  } else if (err != ESP_OK) {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set pairing mode");
      return ESP_FAIL;
  }
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"st\":\"ok\"}");
  
  return ESP_OK;
}

// POST to trigger ESP-NOW slave pairing broadcast
static esp_err_t espnow_slave_pairing_post_handler(httpd_req_t *req) {
  esp_err_t err = espnow_link_trigger_slave_pairing();

  if (err == ESP_ERR_NOT_SUPPORTED) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Pairing trigger is only available for slave role");
      return ESP_FAIL;
  } else if (err != ESP_OK) {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to trigger slave pairing");
      return ESP_FAIL;
  }
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"st\":\"ok\"}");
  
  return ESP_OK;
}

// POST to disconnect ESP-NOW slave from current master
static esp_err_t espnow_slave_disconnect_post_handler(httpd_req_t *req) {
  esp_err_t err = espnow_link_disconnect();
  if (err == ESP_ERR_NOT_SUPPORTED) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Disconnect is only available for slave role");
      return ESP_FAIL;
  } else if (err != ESP_OK) {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to disconnect ESP-NOW");
      return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"st\":\"ok\"}");
  return ESP_OK;
}

// GET peers
static esp_err_t espnow_peers_get_handler(httpd_req_t *req) {
  espnow_peer_info_t peers[ESPNOW_MAX_PEERS] = {0};
  size_t count = 0;
  espnow_link_get_peers(peers, ESPNOW_MAX_PEERS, &count);
  uint64_t now_us = esp_timer_get_time();

  cJSON *root = cJSON_CreateArray();
  for (size_t i = 0; i < count; i++) {
    cJSON *p = cJSON_CreateObject();
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             peers[i].mac[0], peers[i].mac[1], peers[i].mac[2],
             peers[i].mac[3], peers[i].mac[4], peers[i].mac[5]);
    cJSON_AddStringToObject(p, "mac", mac_str);
    cJSON_AddStringToObject(p, "role", espnow_link_role_to_str(peers[i].role));
    cJSON_AddStringToObject(p, "type", espnow_link_slave_type_to_str(peers[i].type));
    cJSON_AddNumberToObject(p, "device_id", peers[i].device_id);
    cJSON_AddNumberToObject(p, "last_seen_us", (double)peers[i].last_seen_us);
    uint64_t age_ms = (peers[i].last_seen_us && now_us > peers[i].last_seen_us)
                          ? (now_us - peers[i].last_seen_us) / 1000ULL
                          : 0;
    cJSON_AddNumberToObject(p, "age_ms", (double)age_ms);
    cJSON_AddNumberToObject(p, "channel", (double)peers[i].channel);
    cJSON_AddItemToArray(root, p);
  }

  const char *json_string = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_string);
  free((void *)json_string);
  cJSON_Delete(root);
  return ESP_OK;
}

// POST test frame (master only)
static esp_err_t espnow_test_frame_post_handler(httpd_req_t *req) {
  if (espnow_link_get_role() != ESP_NOW_ROLE_MASTER) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Test frame only available on master");
    return ESP_FAIL;
  }
  char content[BUFFER_SIZE_SMALL] = {0};
  cJSON *root = NULL;
  uint8_t mac_bytes[6] = {0};
  bool has_mac = false;
  if (req->content_len > 0 && req->content_len < sizeof(content)) {
    if (parse_json_request(req, content, sizeof(content), &root) == ESP_OK) {
      const cJSON *mac_json = cJSON_GetObjectItem(root, "mac");
      if (cJSON_IsString(mac_json) && mac_json->valuestring) {
        int a,b,c,d,e,f;
        if (sscanf(mac_json->valuestring, "%x:%x:%x:%x:%x:%x", &a,&b,&c,&d,&e,&f) == 6) {
          mac_bytes[0]=a; mac_bytes[1]=b; mac_bytes[2]=c; mac_bytes[3]=d; mac_bytes[4]=e; mac_bytes[5]=f;
          has_mac = true;
        }
      }
    }
  }
  if (root) cJSON_Delete(root);

  esp_err_t err = espnow_link_send_test_frame(has_mac ? mac_bytes : NULL);
  if (err == ESP_ERR_NOT_FOUND) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Peer not found");
    return ESP_FAIL;
  } else if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send test frame");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"st\":\"ok\"}");
  return ESP_OK;
}

// POST disconnect peer (master only)
static esp_err_t espnow_disconnect_peer_post_handler(httpd_req_t *req) {
  if (espnow_link_get_role() != ESP_NOW_ROLE_MASTER) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Disconnect peer only available on master");
    return ESP_FAIL;
  }
  char content[BUFFER_SIZE_SMALL] = {0};
  cJSON *root = NULL;
  uint8_t mac_bytes[6] = {0};
  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
    return ESP_FAIL;
  }
  const cJSON *mac_json = cJSON_GetObjectItem(root, "mac");
  if (!cJSON_IsString(mac_json) || !mac_json->valuestring) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mac");
    return ESP_FAIL;
  }
  int a,b,c,d,e,f;
  if (sscanf(mac_json->valuestring, "%x:%x:%x:%x:%x:%x", &a,&b,&c,&d,&e,&f) != 6) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid mac format");
    return ESP_FAIL;
  }
  mac_bytes[0]=a; mac_bytes[1]=b; mac_bytes[2]=c; mac_bytes[3]=d; mac_bytes[4]=e; mac_bytes[5]=f;
  cJSON_Delete(root);

  esp_err_t err = espnow_link_disconnect_peer(mac_bytes);
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to disconnect peer");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"st\":\"ok\"}");
  return ESP_OK;
}

// Handler to configure LED hardware (POST)
static esp_err_t config_post_handler(httpd_req_t *req) {
  char content[BUFFER_SIZE_MEDIUM];
  cJSON *root = NULL;

  if (parse_json_request(req, content, sizeof(content), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  const cJSON *led_count_json = cJSON_GetObjectItem(root, "lc");
  const cJSON *wheel_ctl_json = cJSON_GetObjectItem(root, "wheel_ctl");
  const cJSON *wheel_spd_json = cJSON_GetObjectItem(root, "wheel_spd");

  if (led_count_json == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing led_count");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  uint16_t led_count = (uint16_t)led_count_json->valueint;

  // Apply wheel control settings (optional)
  if (wheel_ctl_json && cJSON_IsBool(wheel_ctl_json)) {
    config_manager_set_wheel_control_enabled(cJSON_IsTrue(wheel_ctl_json));
  }
  if (wheel_spd_json && cJSON_IsNumber(wheel_spd_json)) {
    config_manager_set_wheel_control_speed_limit(wheel_spd_json->valueint);
  }

  // Validation
  if (led_count < LED_COUNT_MIN || led_count > LED_COUNT_MAX) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "led_count must be 1-200");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  // Save to SPIFFS (LED count only)
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

    ESP_LOGI(TAG_WEBSERVER, "Configuration applied: %d LEDs, Wheel: %d, Speed: %d", led_count, config_manager_get_wheel_control_enabled(), config_manager_get_wheel_control_speed_limit());
  } else {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save configuration");
  }

  return ESP_OK;
}

// Handler to list profiles
static esp_err_t profiles_handler(httpd_req_t *req) {
  // Allocate one profile at a time to save memory
  config_profile_t *profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (profile == NULL) {
    ESP_LOGE(TAG_WEBSERVER, "Memory allocation error for profile");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
    return ESP_FAIL;
  }

  int active_id         = config_manager_get_active_profile_id();
  cJSON *root           = cJSON_CreateObject();
  cJSON *profiles_array = cJSON_CreateArray();

  // Load each profile one by one (dynamic scan)
  for (int i = 0; i < MAX_PROFILE_SCAN_LIMIT; i++) {
    if (!config_manager_load_profile(i, profile)) {
      continue; // Profile does not exist, skip
    }

    cJSON *profile_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(profile_obj, "id", i);
    cJSON_AddStringToObject(profile_obj, "n", profile->name);
    cJSON_AddBoolToObject(profile_obj, "ac", (i == active_id));

    // Add default effect
    cJSON *default_effect_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(default_effect_obj, "fx", profile->default_effect.effect);
    cJSON_AddNumberToObject(default_effect_obj, "br", profile->default_effect.brightness);
    cJSON_AddNumberToObject(default_effect_obj, "sp", profile->default_effect.speed);
    cJSON_AddNumberToObject(default_effect_obj, "c1", profile->default_effect.color1);
    cJSON_AddNumberToObject(default_effect_obj, "c2", profile->default_effect.color2);
    cJSON_AddNumberToObject(default_effect_obj, "c3", profile->default_effect.color3);
    cJSON_AddBoolToObject(default_effect_obj, "ar", profile->default_effect.audio_reactive);
    cJSON_AddBoolToObject(default_effect_obj, "rv", profile->default_effect.reverse);
    cJSON_AddNumberToObject(default_effect_obj, "st", profile->default_effect.segment_start);
    cJSON_AddNumberToObject(default_effect_obj, "ln", profile->default_effect.segment_length);
    cJSON_AddBoolToObject(default_effect_obj, "ape", profile->default_effect.accel_pedal_pos_enabled);
    cJSON_AddNumberToObject(default_effect_obj, "apo", profile->default_effect.accel_pedal_offset);
    cJSON_AddItemToObject(profile_obj, "default_effect", default_effect_obj);

    // Add dynamic brightness parameters
    cJSON_AddBoolToObject(profile_obj, "dbe", profile->dynamic_brightness_enabled);
    cJSON_AddNumberToObject(profile_obj, "dbr", profile->dynamic_brightness_rate);
    cJSON *dyn_excluded = cJSON_CreateArray();
    for (int evt = CAN_EVENT_NONE + 1; evt < CAN_EVENT_MAX; evt++) {
      if (profile->dynamic_brightness_exclude_mask & (1ULL << evt)) {
        cJSON_AddItemToArray(dyn_excluded, cJSON_CreateString(config_manager_enum_to_id((can_event_type_t)evt)));
      }
    }
    cJSON_AddItemToObject(profile_obj, "dbe_ex", dyn_excluded);

    cJSON_AddItemToArray(profiles_array, profile_obj);
  }

  cJSON_AddItemToObject(root, "profiles", profiles_array);

  // Load active profile name
  if (active_id >= 0 && config_manager_load_profile(active_id, profile)) {
    cJSON_AddStringToObject(root, "an", profile->name);
  } else {
    cJSON_AddStringToObject(root, "an", "None");
  }

  // Add SPIFFS statistics
  size_t spiffs_total = 0, spiffs_used = 0;
  esp_err_t err = spiffs_get_stats(&spiffs_total, &spiffs_used);
  if (err == ESP_OK) {
    size_t spiffs_free = spiffs_total - spiffs_used;

    cJSON *storage = cJSON_CreateObject();
    cJSON_AddNumberToObject(storage, "used", spiffs_used);
    cJSON_AddNumberToObject(storage, "free", spiffs_free);
    cJSON_AddNumberToObject(storage, "total", spiffs_total);
    cJSON_AddNumberToObject(storage, "usage_pct", (spiffs_total > 0) ? (spiffs_used * 100 / spiffs_total) : 0);
    cJSON_AddItemToObject(root, "storage", storage);
  }

  const char *json_string = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_string);

  free((void *)json_string);
  cJSON_Delete(root);
  free(profile);

  return ESP_OK;
}

// Handler to activate a profile
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

// Handler to create a new profile
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

  // First check if SPIFFS has enough space
  if (!config_manager_can_create_profile()) {
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Insufficient storage space\"}");
    return ESP_FAIL;
  }

  // Allocate dynamically to avoid stack overflow
  config_profile_t *temp        = (config_profile_t *)malloc(sizeof(config_profile_t));
  config_profile_t *new_profile = (config_profile_t *)malloc(sizeof(config_profile_t));

  if (temp == NULL || new_profile == NULL) {
    ESP_LOGE(TAG_WEBSERVER, "Memory allocation failed for profile");
    if (temp)
      free(temp);
    if (new_profile)
      free(new_profile);
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
    return ESP_FAIL;
  }

  // Find a free slot (dynamic scan)
  for (int i = 0; i < MAX_PROFILE_SCAN_LIMIT; i++) {
    if (!config_manager_load_profile(i, temp)) {
      // Free slot
      ESP_LOGI(TAG_WEBSERVER, "Creating profile '%s' in slot %d", name->valuestring, i);
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

// Handler to delete a profile
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
      config_manager_activate_profile(profile_id->valueint - 1);
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

// Handler to rename a profile
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

// Handler for factory reset
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

  // Restart the ESP32 after a short delay
  if (success) {
    vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
    esp_restart();
  }

  return ESP_OK;
}

// Handler to update profile settings
static esp_err_t profile_update_handler(httpd_req_t *req) {
  char content[BUFFER_SIZE_JSON]; // Increased to accept settings + default effect
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

  uint16_t profile_id       = (uint16_t)profile_id_json->valueint;

  // Allocate dynamically to avoid stack overflow
  config_profile_t *profile = (config_profile_t *)malloc(sizeof(config_profile_t));
  if (profile == NULL) {
    ESP_LOGE(TAG_WEBSERVER, "Memory allocation error");
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

  // Update default effect if provided
  bool default_effect_updated           = false;
  const cJSON *effect_json              = cJSON_GetObjectItem(root, "fx");
  const cJSON *brightness_json          = cJSON_GetObjectItem(root, "br");
  const cJSON *speed_json               = cJSON_GetObjectItem(root, "sp");
  const cJSON *color_json               = cJSON_GetObjectItem(root, "c1");
  const cJSON *color2_json              = cJSON_GetObjectItem(root, "c2");
  const cJSON *color3_json              = cJSON_GetObjectItem(root, "c3");
  const cJSON *audio_reactive_json      = cJSON_GetObjectItem(root, "ar");
  const cJSON *reverse_json             = cJSON_GetObjectItem(root, "rv");
  const cJSON *segment_start_json       = cJSON_GetObjectItem(root, "st");
  const cJSON *segment_length_json      = cJSON_GetObjectItem(root, "ln");
  const cJSON *accel_pedal_enabled_json = cJSON_GetObjectItem(root, "ape");
  const cJSON *accel_pedal_offset_json  = cJSON_GetObjectItem(root, "apo");
  const cJSON *dyn_bright_enabled_json  = cJSON_GetObjectItem(root, "dbe");
  const cJSON *dyn_bright_rate_json     = cJSON_GetObjectItem(root, "dbr");
  const cJSON *dyn_bright_exclude_json  = cJSON_GetObjectItem(root, "dbe_ex");

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
  if (color2_json) {
    profile->default_effect.color2 = (uint32_t)color2_json->valueint;
    default_effect_updated         = true;
  }
  if (color3_json) {
    profile->default_effect.color3 = (uint32_t)color3_json->valueint;
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
  if (accel_pedal_enabled_json && cJSON_IsBool(accel_pedal_enabled_json)) {
    profile->default_effect.accel_pedal_pos_enabled = cJSON_IsTrue(accel_pedal_enabled_json);
    default_effect_updated                          = true;
  }
  if (accel_pedal_offset_json) {
    profile->default_effect.accel_pedal_offset = (uint8_t)accel_pedal_offset_json->valueint;
    default_effect_updated                     = true;
  }

  // Update dynamic brightness parameters
  if (dyn_bright_enabled_json && cJSON_IsBool(dyn_bright_enabled_json)) {
    profile->dynamic_brightness_enabled = cJSON_IsTrue(dyn_bright_enabled_json);
  }
  if (dyn_bright_rate_json && cJSON_IsNumber(dyn_bright_rate_json)) {
    profile->dynamic_brightness_rate = (uint8_t)dyn_bright_rate_json->valueint;
  }
  if (dyn_bright_exclude_json && cJSON_IsArray(dyn_bright_exclude_json)) {
    uint64_t exclude_mask = 0;
    const cJSON *excluded = NULL;
    cJSON_ArrayForEach(excluded, dyn_bright_exclude_json) {
      if (excluded && cJSON_IsString(excluded)) {
        can_event_type_t evt = config_manager_id_to_enum(excluded->valuestring);
        if (evt > CAN_EVENT_NONE && evt < CAN_EVENT_MAX) {
          exclude_mask |= (1ULL << evt);
        }
      }
    }
    profile->dynamic_brightness_exclude_mask = exclude_mask;
  }

  // Save profile
  bool success = config_manager_save_profile(profile_id, profile);

  // If it is the active profile, update the configuration
  if (success && config_manager_get_active_profile_id() == profile_id) {
    // Apply the default effect if modified
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

// Handler to export a profile as JSON
static esp_err_t profile_export_handler(httpd_req_t *req) {
  // Retrieve profile_id from query parameters
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

  // Allocate a buffer for JSON (16 KB should be enough for a full profile)
  char *json_buffer   = malloc(BUFFER_SIZE_PROFILE);
  if (!json_buffer) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
    return ESP_FAIL;
  }

  bool success = config_manager_export_profile(profile_id, json_buffer, BUFFER_SIZE_PROFILE);

  if (success) {
    // Send the JSON with correct headers for download
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
  // First ensure there is enough SPIFFS space
  if (!config_manager_can_create_profile()) {
    ESP_LOGW(TAG_WEBSERVER, "Insufficient SPIFFS space to create a new profile");
    return -1;
  }

  int target_id = -1;

  // If a preferred ID is provided, check if it is free
  if (preferred_id >= 0 && preferred_id < MAX_PROFILE_SCAN_LIMIT) {
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/spiffs/profiles/profile_%d.bin", preferred_id);
    if (!spiffs_file_exists(filepath)) {
      target_id = preferred_id;
      ESP_LOGI(TAG_WEBSERVER, "Slot %d free (preferred)", preferred_id);
    } else {
      ESP_LOGW(TAG_WEBSERVER, "Preferred slot %d already in use", preferred_id);
    }
  }

  // Otherwise, search for the first free slot
  if (target_id < 0) {
    for (int i = 0; i < MAX_PROFILE_SCAN_LIMIT; i++) {
      char filepath[64];
      snprintf(filepath, sizeof(filepath), "/spiffs/profiles/profile_%d.bin", i);
      if (!spiffs_file_exists(filepath)) {
        target_id = i;
        ESP_LOGI(TAG_WEBSERVER, "Slot %d free (scan)", i);
        break;
      }
    }
  }

  if (target_id < 0) {
    ESP_LOGE(TAG_WEBSERVER, "No free slot found");
  }

  return target_id;
}

// Handler to import a profile from JSON
static esp_err_t profile_import_handler(httpd_req_t *req) {
  // Allocate a buffer to receive the JSON
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

  // Convert profile_data into a JSON string
  char *profile_json = cJSON_PrintUnformatted(profile_data);
  if (!profile_json) {
    cJSON_Delete(root);
    free(content);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to serialize profile");
    return ESP_FAIL;
  }

  int target_id = find_free_profile_slot(preferred_id);
  if (target_id < 0) {
    ESP_LOGE(TAG_WEBSERVER, "Import failed: no free profile slot (preferred_id=%d)", preferred_id);
    free(profile_json);
    cJSON_Delete(root);
    free(content);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"st\":\"error\",\"msg\":\"Insufficient storage space or no free profile slots\"}");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG_WEBSERVER, "Importing profile to slot %d (preferred=%d)", target_id, preferred_id);
  // Use direct import to avoid JSON re-generation (saves memory)
  bool success = config_manager_import_profile_direct((uint16_t)target_id, profile_json);

  if (!success) {
    ESP_LOGE(TAG_WEBSERVER, "Import failed: config_manager_import_profile_direct returned false");
  } else {
    ESP_LOGI(TAG_WEBSERVER, "Profile %d imported successfully, activating...", target_id);
  }

  free(profile_json);
  cJSON_Delete(root);
  free(content);

  httpd_resp_set_type(req, "application/json");
  if (success) {
    // Activate the imported profile
    bool activated = config_manager_activate_profile((uint16_t)target_id);
    if (!activated) {
      ESP_LOGE(TAG_WEBSERVER, "Profile imported but activation failed!");
    } else {
      ESP_LOGI(TAG_WEBSERVER, "Profile %d activated successfully", target_id);
    }

    char response[64];
    snprintf(response, sizeof(response), "{\"st\":\"ok\",\"pid\":%d}", target_id);
    httpd_resp_sendstr(req, response);
  } else {
    httpd_resp_sendstr(req, "{\"st\":\"error\",\"msg\":\"Import failed\"}");
  }

  return ESP_OK;
}

// Handler to get OTA information
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

// Handler to upload OTA firmware
static esp_err_t ota_upload_handler(httpd_req_t *req) {
  char buf[BUFFER_SIZE_JSON];
  int remaining = req->content_len;
  int received;
  bool first_chunk = true;
  esp_err_t ret    = ESP_OK;

  ESP_LOGI(TAG_WEBSERVER, "Starting OTA upload, size: %d bytes", remaining);

  // Start OTA
  ret = ota_begin(req->content_len);
  if (ret != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
    return ESP_FAIL;
  }

  // Receive and write data
  while (remaining > 0) {
    // Read data from the socket
    received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        // Retry
        continue;
      }
      ESP_LOGE(TAG_WEBSERVER, "OTA data receive error");
      ota_abort();
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
      return ESP_FAIL;
    }

    // Write into the OTA partition
    ret = ota_write(buf, received);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG_WEBSERVER, "OTA write error");
      ota_abort();
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
      return ESP_FAIL;
    }

    remaining -= received;

    if (first_chunk) {
      ESP_LOGI(TAG_WEBSERVER, "First chunk received and written");
      first_chunk = false;
    }
  }

  // Finish OTA
  ret = ota_end();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_WEBSERVER, "OTA finalize error");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG_WEBSERVER, "OTA upload completed successfully");

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req,
                     "{\"status\":\"ok\",\"message\":\"Upload successful, "
                     "restart to apply\"}");

  return ESP_OK;
}

// Handler to restart the ESP32
static esp_err_t ota_restart_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Restarting...\"}");

  ESP_LOGI(TAG_WEBSERVER, "Restart requested via API");

  // Restart after a short delay
  vTaskDelay(pdMS_TO_TICKS(1000));
  ota_restart();

  return ESP_OK;
}

// ============================================================================
// GVRET TCP Server API Handlers
// ============================================================================

// ============================================================================
// Generic helpers for CAN-related servers (factorized)
// ============================================================================

typedef esp_err_t (*server_start_fn_t)(void);
typedef void (*server_stop_fn_t)(void);
typedef bool (*server_is_running_fn_t)(void);
typedef int (*server_get_client_count_fn_t)(void);
typedef bool (*server_get_autostart_fn_t)(void);
typedef esp_err_t (*server_set_autostart_fn_t)(bool);

// Generic helper for start
static esp_err_t handle_server_start(httpd_req_t *req, server_start_fn_t start_fn, const char *name) {
  httpd_resp_set_type(req, "application/json");

  esp_err_t ret = start_fn();
  if (ret == ESP_OK) {
    ESP_LOGI(TAG_WEBSERVER, "Server %s TCP started via API", name);
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"running\":true}");
  } else {
    ESP_LOGW(TAG_WEBSERVER, "Failed to start server %s: %s", name, esp_err_to_name(ret));
    httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Failed to start server\"}");
  }

  return ESP_OK;
}

// Generic helper for stop
static esp_err_t handle_server_stop(httpd_req_t *req, server_stop_fn_t stop_fn, const char *name) {
  httpd_resp_set_type(req, "application/json");

  stop_fn();
  ESP_LOGI(TAG_WEBSERVER, "Server %s TCP stopped via API", name);

  httpd_resp_sendstr(req, "{\"status\":\"ok\",\"running\":false}");
  return ESP_OK;
}

// Generic helper for status
static esp_err_t handle_server_status(httpd_req_t *req, server_is_running_fn_t is_running_fn, server_get_client_count_fn_t get_clients_fn, server_get_autostart_fn_t get_autostart_fn, int port) {
  httpd_resp_set_type(req, "application/json");

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "running", is_running_fn());
  cJSON_AddNumberToObject(root, "clients", get_clients_fn());
  cJSON_AddNumberToObject(root, "port", port);
  cJSON_AddBoolToObject(root, "autostart", get_autostart_fn());

  const char *json_str = cJSON_PrintUnformatted(root);
  httpd_resp_sendstr(req, json_str);

  cJSON_free((void *)json_str);
  cJSON_Delete(root);

  return ESP_OK;
}

// Generic helper for autostart
static esp_err_t handle_server_autostart(httpd_req_t *req, server_set_autostart_fn_t set_fn) {
  httpd_resp_set_type(req, "application/json");

  char content[100];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid request\"}");
    return ESP_OK;
  }
  content[ret] = '\0';

  cJSON *json  = cJSON_Parse(content);
  if (!json) {
    httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return ESP_OK;
  }

  cJSON *autostart_item = cJSON_GetObjectItem(json, "autostart");
  if (!autostart_item || !cJSON_IsBool(autostart_item)) {
    cJSON_Delete(json);
    httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Missing autostart field\"}");
    return ESP_OK;
  }

  bool autostart = cJSON_IsTrue(autostart_item);
  esp_err_t err  = set_fn(autostart);

  cJSON_Delete(json);

  if (err == ESP_OK) {
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  } else {
    httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Failed to save autostart\"}");
  }

  return ESP_OK;
}

// ============================================================================
// GVRET TCP Server API Handlers
// ============================================================================

// Handler to start the GVRET TCP server
static esp_err_t gvret_start_handler(httpd_req_t *req) {
  return handle_server_start(req, gvret_tcp_server_start, "GVRET");
}

// Handler to stop the GVRET TCP server
static esp_err_t gvret_stop_handler(httpd_req_t *req) {
  return handle_server_stop(req, gvret_tcp_server_stop, "GVRET");
}

// Handler to get GVRET TCP server status
static esp_err_t gvret_status_handler(httpd_req_t *req) {
  return handle_server_status(req, gvret_tcp_server_is_running, gvret_tcp_server_get_client_count, gvret_tcp_server_get_autostart, 23);
}

// Handler to set autostart for the GVRET TCP server
static esp_err_t gvret_autostart_handler(httpd_req_t *req) {
  return handle_server_autostart(req, gvret_tcp_server_set_autostart);
}

// ============================================================================
// CANServer UDP Server API Handlers
// ============================================================================

// Handler to start the CANServer UDP server
static esp_err_t canserver_start_handler(httpd_req_t *req) {
  return handle_server_start(req, canserver_udp_server_start, "CANServer");
}

// Handler to stop the CANServer UDP server
static esp_err_t canserver_stop_handler(httpd_req_t *req) {
  return handle_server_stop(req, canserver_udp_server_stop, "CANServer");
}

// Handler to get CANServer UDP server status
static esp_err_t canserver_status_handler(httpd_req_t *req) {
  return handle_server_status(req, canserver_udp_server_is_running, canserver_udp_server_get_client_count, canserver_udp_server_get_autostart, 1338);
}

// Handler to set autostart for the CANServer UDP server
static esp_err_t canserver_autostart_handler(httpd_req_t *req) {
  return handle_server_autostart(req, canserver_udp_server_set_autostart);
}

// ============================================================================
// Server-Sent Events (SSE) for Live Logs
// ============================================================================

// Dummy recv override - returns -1/EAGAIN to prevent httpd from reading
// This tells httpd: "no data available, keep the socket open and don't close it"
static int sse_recv_override(httpd_handle_t hd, int sockfd, char *buf, size_t buf_len, int flags) {
  (void)hd;
  (void)sockfd;
  (void)buf;
  (void)buf_len;
  (void)flags;
  errno = EAGAIN;
  return -1; // Tell httpd "no data to read, try again later"
}

// Handler for log streaming via Server-Sent Events (SSE)
static esp_err_t log_stream_handler(httpd_req_t *req) {
  int fd = httpd_req_to_sockfd(req);
  ESP_LOGI(TAG_WEBSERVER, "SSE client connecting (fd=%d)", fd);

  // Send HTTP headers and initial messages using raw socket
  esp_err_t ret = log_stream_send_headers(fd);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_WEBSERVER, "Failed to send SSE headers to fd=%d", fd);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to initialize SSE stream");
    return ESP_FAIL;
  }

  // Register this client for log streaming (watchdog will manage it)
  ret = log_stream_register_client(fd);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG_WEBSERVER, "Failed to register SSE client fd=%d (max clients reached)", fd);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Max SSE clients reached");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG_WEBSERVER, "SSE client registered (fd=%d), overriding recv", fd);

  // CRITICAL: Override recv to prevent httpd from closing this socket
  // This tells httpd "I'm handling this socket myself, don't touch it"
  // Source: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_server.html
  httpd_sess_set_recv_override(req->handle, fd, sse_recv_override);

  // Return immediately - httpd won't close the socket thanks to recv override
  // The watchdog task will monitor and manage this connection
  return ESP_OK;
}

static esp_err_t log_file_status_handler(httpd_req_t *req) {
  uint32_t max_index = log_stream_get_file_rotation_max();
  uint32_t current   = log_stream_get_current_file_index();
  uint32_t selected  = current;

  char query[32];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char idx_str[8];
    if (httpd_query_key_value(query, "idx", idx_str, sizeof(idx_str)) == ESP_OK) {
      int idx = atoi(idx_str);
      if (idx > 0) {
        selected = (uint32_t)idx;
      }
    }
  }

  if (selected < 1 || selected > max_index) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid log index");
    return ESP_FAIL;
  }

  char path[64];
  snprintf(path, sizeof(path), "/spiffs/logs/logs.%u.txt", (unsigned)selected);
  int file_size = spiffs_get_file_size(path);
  size_t size   = (file_size > 0) ? (size_t)file_size : 0;

  bool enabled  = log_stream_is_file_logging_enabled();

  char json[160];
  snprintf(json, sizeof(json), "{\"st\":\"ok\",\"en\":%s,\"size\":%u,\"idx\":%u,\"max\":%u,\"sel\":%u}",
           enabled ? "true" : "false", (unsigned)size, (unsigned)current, (unsigned)max_index, (unsigned)selected);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json);
  return ESP_OK;
}

static esp_err_t log_file_enable_handler(httpd_req_t *req) {
  char buffer[BUFFER_SIZE_SMALL];
  cJSON *json = NULL;

  if (parse_json_request(req, buffer, sizeof(buffer), &json) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *en_item = cJSON_GetObjectItemCaseSensitive(json, "en");
  if (!cJSON_IsBool(en_item)) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'en' field");
    return ESP_FAIL;
  }

  bool enable  = cJSON_IsTrue(en_item);
  esp_err_t ret = log_stream_enable_file_logging(enable);
  cJSON_Delete(json);

  if (ret != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to update log file state");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"st\":\"ok\"}");
  return ESP_OK;
}

static esp_err_t log_file_clear_handler(httpd_req_t *req) {
  esp_err_t ret = log_stream_clear_file_log();
  if (ret != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to clear log file");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"st\":\"ok\"}");
  return ESP_OK;
}

static esp_err_t log_file_download_handler(httpd_req_t *req) {
  uint32_t max_index = log_stream_get_file_rotation_max();
  uint32_t selected  = log_stream_get_current_file_index();

  char query[32];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char idx_str[8];
    if (httpd_query_key_value(query, "idx", idx_str, sizeof(idx_str)) == ESP_OK) {
      int idx = atoi(idx_str);
      if (idx > 0) {
        selected = (uint32_t)idx;
      }
    }
  }

  if (selected < 1 || selected > max_index) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid log index");
    return ESP_FAIL;
  }

  char path[64];
  char filename[32];
  snprintf(path, sizeof(path), "/spiffs/logs/logs.%u.txt", (unsigned)selected);
  snprintf(filename, sizeof(filename), "logs.%u.txt", (unsigned)selected);

  if (!spiffs_file_exists(path)) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Log file not found");
    return ESP_FAIL;
  }

  FILE *f = fopen(path, "r");
  if (f == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open log file");
    return ESP_FAIL;
  }

  char disposition[64];
  snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", filename);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Content-Disposition", disposition);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  char buffer[1024];
  size_t read_bytes = 0;
  esp_err_t err     = ESP_OK;

  while ((read_bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
    err = httpd_resp_send_chunk(req, buffer, read_bytes);
    if (err != ESP_OK) {
      break;
    }
  }

  fclose(f);
  httpd_resp_send_chunk(req, NULL, 0);
  return err;
}

// Handler to stop a CAN event
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

  // Stop the event
  config_manager_stop_event(event);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"st\":\"ok\"}");

  cJSON_Delete(root);
  return ESP_OK;
}

// Handler to get available effects list
static esp_err_t effects_list_handler(httpd_req_t *req) {
  cJSON *root    = cJSON_CreateObject();
  cJSON *effects = cJSON_CreateArray();

  // Iterate through all available effects
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

// Handler to get the list of CAN event types
static esp_err_t event_types_list_handler(httpd_req_t *req) {
  cJSON *root        = cJSON_CreateObject();
  cJSON *event_types = cJSON_CreateArray();

  // Iterate over all CAN event types
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

// Handler to simulate a CAN event
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

  // Validate the event
  if (event <= CAN_EVENT_NONE || event >= CAN_EVENT_MAX) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid event type");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  // Process the event
  bool success = config_manager_process_can_event(event);

  ESP_LOGI(TAG_WEBSERVER, "Simulating CAN event: %s (%d)", config_manager_enum_to_id(event), event);

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

// Handler to get all CAN events (GET)
static bool apply_event_update_from_json(config_profile_t *profile, int profile_id, cJSON *event_obj) {
  if (profile == NULL || event_obj == NULL) {
    return false;
  }

  const cJSON *event_json       = cJSON_GetObjectItem(event_obj, "ev");
  const cJSON *effect_json      = cJSON_GetObjectItem(event_obj, "fx");
  const cJSON *brightness_json  = cJSON_GetObjectItem(event_obj, "br");
  const cJSON *speed_json       = cJSON_GetObjectItem(event_obj, "sp");
  const cJSON *color_json       = cJSON_GetObjectItem(event_obj, "c1");
  const cJSON *color2_json      = cJSON_GetObjectItem(event_obj, "c2");
  const cJSON *color3_json      = cJSON_GetObjectItem(event_obj, "c3");
  const cJSON *reverse_json     = cJSON_GetObjectItem(event_obj, "rv");
  const cJSON *duration_json    = cJSON_GetObjectItem(event_obj, "dur");
  const cJSON *priority_json    = cJSON_GetObjectItem(event_obj, "pri");
  const cJSON *enabled_json     = cJSON_GetObjectItem(event_obj, "en");
  const cJSON *action_type_json = cJSON_GetObjectItem(event_obj, "at");
  const cJSON *profile_id_json  = cJSON_GetObjectItem(event_obj, "pid");
  const cJSON *segment_start    = cJSON_GetObjectItem(event_obj, "st");
  const cJSON *segment_length   = cJSON_GetObjectItem(event_obj, "ln");

  if (event_json == NULL || effect_json == NULL || !cJSON_IsString(event_json) || !cJSON_IsString(effect_json)) {
    ESP_LOGW(TAG_WEBSERVER, "Invalid payload for event");
    return false;
  }

  can_event_type_t event_type = config_manager_id_to_enum(event_json->valuestring);
  if (event_type <= CAN_EVENT_NONE || event_type >= CAN_EVENT_MAX) {
    ESP_LOGW(TAG_WEBSERVER, "Unknown event: %s", event_json->valuestring);
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
  if (color2_json && cJSON_IsNumber(color2_json)) {
    effect_config.color2 = color2_json->valueint;
  }
  if (color3_json && cJSON_IsNumber(color3_json)) {
    effect_config.color3 = color3_json->valueint;
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

  // Iterate across all CAN events (except CAN_EVENT_NONE)
  for (int i = CAN_EVENT_TURN_LEFT; i < CAN_EVENT_MAX; i++) {
    can_event_type_t event_type = (can_event_type_t)i;
    can_event_effect_t event_effect;

    // Get the event configuration
    if (config_manager_get_effect_for_event(event_type, &event_effect)) {
      cJSON *event_obj = cJSON_CreateObject();

      // Use alphanumeric IDs
      cJSON_AddStringToObject(event_obj, "ev", config_manager_enum_to_id(event_effect.event));
      cJSON_AddStringToObject(event_obj, "fx", led_effects_enum_to_id(event_effect.effect_config.effect));
      cJSON_AddNumberToObject(event_obj, "br", event_effect.effect_config.brightness);
      cJSON_AddNumberToObject(event_obj, "sp", event_effect.effect_config.speed);
      cJSON_AddNumberToObject(event_obj, "c1", event_effect.effect_config.color1);
      cJSON_AddNumberToObject(event_obj, "c2", event_effect.effect_config.color2);
      cJSON_AddNumberToObject(event_obj, "c3", event_effect.effect_config.color3);
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

// ============================================================================
// HANDLERS AUDIO
// ============================================================================

// Handler GET /api/audio/status - Status and configuration microphone
static esp_err_t audio_status_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  audio_config_t config;
  audio_input_get_config(&config);

  cJSON_AddBoolToObject(root, "en", audio_input_is_enabled());
  cJSON_AddNumberToObject(root, "sen", config.sensitivity);
  cJSON_AddNumberToObject(root, "gn", config.gain);
  cJSON_AddBoolToObject(root, "ag", config.auto_gain);
  cJSON_AddBoolToObject(root, "ffe", config.fft_enabled);

  // Informations de calibration micro
  audio_calibration_t calibration;
  audio_input_get_calibration(&calibration);
  cJSON *calibration_obj = cJSON_CreateObject();
  cJSON_AddBoolToObject(calibration_obj, "av", calibration.calibrated);
  if (calibration.calibrated) {
    cJSON_AddNumberToObject(calibration_obj, "nf", calibration.noise_floor);
    cJSON_AddNumberToObject(calibration_obj, "pk", calibration.peak_level);
  }
  cJSON_AddItemToObject(root, "cal", calibration_obj);

  const char *json_str = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);

  cJSON_free((void *)json_str);
  cJSON_Delete(root);
  return ESP_OK;
}

// Handler POST /api/audio/enable - Enable/disable microphone
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
      ESP_LOGI(TAG_WEBSERVER, "Microphone %s", enable ? "enabled" : "disabled");
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

// Handler POST /api/audio/config - Update audio configuration
static esp_err_t audio_config_handler(httpd_req_t *req) {
  char buf[BUFFER_SIZE_MEDIUM];
  cJSON *root = NULL;
  if (parse_json_request(req, buf, sizeof(buf), &root) != ESP_OK) {
    return ESP_FAIL;
  }

  audio_config_t config;
  audio_input_get_config(&config);

  // Update provided parameters
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

  // Save configuration
  audio_input_save_config();

  cJSON_Delete(root);

  cJSON *response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "ok", true);
  const char *json_str = cJSON_PrintUnformatted(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);
  cJSON_free((void *)json_str);
  cJSON_Delete(response);

  ESP_LOGI(TAG_WEBSERVER, "Audio configuration updated");
  return ESP_OK;
}

// Handler POST /api/audio/calibrate - Run manual calibration
static esp_err_t audio_calibrate_handler(httpd_req_t *req) {
  uint32_t duration_ms = 5000; // default window
  char buf[BUFFER_SIZE_SMALL];
  cJSON *root = NULL;

  if (req->content_len > 0) {
    if (parse_json_request(req, buf, sizeof(buf), &root) != ESP_OK) {
      return ESP_FAIL;
    }

    const cJSON *duration = cJSON_GetObjectItem(root, "dur");
    if (duration && cJSON_IsNumber(duration)) {
      duration_ms = (uint32_t)duration->valuedouble;
    }
  }

  if (!audio_input_is_enabled()) {
    if (root) {
      cJSON_Delete(root);
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Microphone disabled");
    return ESP_FAIL;
  }

  audio_calibration_t calibration_result;
  if (!audio_input_run_calibration(duration_ms, &calibration_result)) {
    if (root) {
      cJSON_Delete(root);
    }
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Calibration failed");
    return ESP_FAIL;
  }

  if (root) {
    cJSON_Delete(root);
  }

  cJSON *response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "ok", true);
  cJSON_AddNumberToObject(response, "dur", duration_ms);

  cJSON *cal_obj = cJSON_CreateObject();
  cJSON_AddBoolToObject(cal_obj, "av", calibration_result.calibrated);
  cJSON_AddNumberToObject(cal_obj, "nf", calibration_result.noise_floor);
  cJSON_AddNumberToObject(cal_obj, "pk", calibration_result.peak_level);
  cJSON_AddItemToObject(response, "cal", cal_obj);

  const char *json_str = cJSON_PrintUnformatted(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);

  cJSON_free((void *)json_str);
  cJSON_Delete(response);

  return ESP_OK;
}

// Handler GET /api/audio/data - Real-time audio data (with FFT if enabled)
static esp_err_t audio_data_handler(httpd_req_t *req) {
  audio_data_t audio_data;
  audio_fft_data_t fft_data;
  cJSON *root = cJSON_CreateObject();

  // Basic audio data
  if (audio_input_get_data(&audio_data)) {
    cJSON_AddNumberToObject(root, "amp", audio_data.amplitude);
    cJSON_AddNumberToObject(root, "ram", audio_data.raw_amplitude);
    cJSON_AddNumberToObject(root, "cam", audio_data.calibrated_amplitude);
    cJSON_AddNumberToObject(root, "agn", audio_data.auto_gain);
    cJSON_AddNumberToObject(root, "nf", audio_data.noise_floor);
    cJSON_AddNumberToObject(root, "pk", audio_data.peak_level);
    cJSON_AddNumberToObject(root, "ba", audio_data.bass);
    cJSON_AddNumberToObject(root, "md", audio_data.mid);
    cJSON_AddNumberToObject(root, "tr", audio_data.treble);
    cJSON_AddNumberToObject(root, "bpm", audio_data.bpm);
    cJSON_AddBoolToObject(root, "bd", audio_data.beat_detected);
    cJSON_AddBoolToObject(root, "av", true);
  } else {
    cJSON_AddBoolToObject(root, "av", false);
  }

  // FFT data (if enabled)
  if (audio_input_is_fft_enabled() && audio_input_get_fft_data(&fft_data)) {
    // Create an FFT object
    cJSON *fft_obj     = cJSON_CreateObject();

    // Add FFT bands
    cJSON *bands_array = cJSON_CreateArray();
    for (int i = 0; i < AUDIO_FFT_BANDS; i++) {
      cJSON_AddItemToArray(bands_array, cJSON_CreateNumber(fft_data.bands[i]));
    }
    cJSON_AddItemToObject(fft_obj, "bands", bands_array);

    // Add FFT metadata
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
    // FFT unavailable
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

esp_err_t web_server_init(void) {
  ESP_LOGI(TAG_WEBSERVER, "Web server initialized");
  return ESP_OK;
}

esp_err_t web_server_start(void) {
  httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
  config.server_port      = WEB_SERVER_PORT;
  config.max_uri_handlers = HTTP_MAX_URI_HANDLERS;
  config.max_open_sockets = HTTP_MAX_OPEN_SOCKETS;
  config.lru_purge_enable = true; 
  // config.core_id          = 1;    // Serveur web sur core 1 (WiFi sur core 0)

  ESP_LOGI(TAG_WEBSERVER, "Web server started on port %d", config.server_port);

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

    httpd_uri_t espnow_get_uri = {.uri = "/api/espnow/config", .method = HTTP_GET, .handler = espnow_config_get_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &espnow_get_uri);

    httpd_uri_t espnow_post_uri = {.uri = "/api/espnow/config", .method = HTTP_POST, .handler = espnow_config_post_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &espnow_post_uri);

    httpd_uri_t espnow_pairing_uri = {.uri = "/api/espnow/pairing", .method = HTTP_POST, .handler = espnow_pairing_post_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &espnow_pairing_uri);

    httpd_uri_t espnow_slave_pairing_uri = {.uri = "/api/espnow/slave-pair", .method = HTTP_POST, .handler = espnow_slave_pairing_post_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &espnow_slave_pairing_uri);

    httpd_uri_t espnow_slave_disconnect_uri = {.uri = "/api/espnow/disconnect", .method = HTTP_POST, .handler = espnow_slave_disconnect_post_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &espnow_slave_disconnect_uri);

    httpd_uri_t espnow_peers_uri = {.uri = "/api/espnow/peers", .method = HTTP_GET, .handler = espnow_peers_get_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &espnow_peers_uri);

    httpd_uri_t espnow_test_uri = {.uri = "/api/espnow/test-frame", .method = HTTP_POST, .handler = espnow_test_frame_post_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &espnow_test_uri);

    httpd_uri_t espnow_disconnect_peer_uri = {.uri = "/api/espnow/peer-disconnect", .method = HTTP_POST, .handler = espnow_disconnect_peer_post_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &espnow_disconnect_peer_uri);

    httpd_uri_t status_uri = {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &status_uri);

    // Routes for profiles
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

    httpd_uri_t simulate_event_uri = {.uri = "/api/simulate/event", .method = HTTP_POST, .handler = simulate_event_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &simulate_event_uri);

    httpd_uri_t stop_event_uri = {.uri = "/api/stop/event", .method = HTTP_POST, .handler = stop_event_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &stop_event_uri);

    // Routes to retrieve effect and event lists
    httpd_uri_t effects_list_uri = {.uri = "/api/effects", .method = HTTP_GET, .handler = effects_list_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &effects_list_uri);

    httpd_uri_t event_types_list_uri = {.uri = "/api/event-types", .method = HTTP_GET, .handler = event_types_list_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &event_types_list_uri);

    // Route POST /api/config to configure LED hardware
    httpd_uri_t config_post_uri = {.uri = "/api/config", .method = HTTP_POST, .handler = config_post_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &config_post_uri);

    httpd_uri_t event_single_post_uri = {.uri = "/api/events/update", .method = HTTP_POST, .handler = event_single_post_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &event_single_post_uri);

    // Route GET /api/events to fetch CAN events
    httpd_uri_t events_get_uri = {.uri = "/api/events", .method = HTTP_GET, .handler = events_get_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &events_get_uri);

    // Audio routes (INMP441 mic)
    httpd_uri_t audio_status_uri = {.uri = "/api/audio/status", .method = HTTP_GET, .handler = audio_status_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &audio_status_uri);

    httpd_uri_t audio_enable_uri = {.uri = "/api/audio/enable", .method = HTTP_POST, .handler = audio_enable_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &audio_enable_uri);

    httpd_uri_t audio_config_uri = {.uri = "/api/audio/config", .method = HTTP_POST, .handler = audio_config_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &audio_config_uri);

    httpd_uri_t audio_calibrate_uri = {.uri = "/api/audio/calibrate", .method = HTTP_POST, .handler = audio_calibrate_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &audio_calibrate_uri);

    httpd_uri_t audio_data_uri = {.uri = "/api/audio/data", .method = HTTP_GET, .handler = audio_data_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &audio_data_uri);

    // Aliases for new system endpoints
    httpd_uri_t system_restart_uri = {.uri = "/api/system/restart", .method = HTTP_POST, .handler = ota_restart_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &system_restart_uri);

    httpd_uri_t system_factory_reset_uri = {.uri = "/api/system/factory-reset", .method = HTTP_POST, .handler = factory_reset_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &system_factory_reset_uri);

    // GVRET TCP Server routes
    httpd_uri_t gvret_start_uri = {.uri = "/api/gvret/start", .method = HTTP_POST, .handler = gvret_start_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &gvret_start_uri);

    httpd_uri_t gvret_stop_uri = {.uri = "/api/gvret/stop", .method = HTTP_POST, .handler = gvret_stop_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &gvret_stop_uri);

    httpd_uri_t gvret_status_uri = {.uri = "/api/gvret/status", .method = HTTP_GET, .handler = gvret_status_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &gvret_status_uri);

    httpd_uri_t gvret_autostart_uri = {.uri = "/api/gvret/autostart", .method = HTTP_POST, .handler = gvret_autostart_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &gvret_autostart_uri);

    // CANServer TCP Server routes
    httpd_uri_t canserver_start_uri = {.uri = "/api/canserver/start", .method = HTTP_POST, .handler = canserver_start_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &canserver_start_uri);

    httpd_uri_t canserver_stop_uri = {.uri = "/api/canserver/stop", .method = HTTP_POST, .handler = canserver_stop_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &canserver_stop_uri);

    httpd_uri_t canserver_status_uri = {.uri = "/api/canserver/status", .method = HTTP_GET, .handler = canserver_status_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &canserver_status_uri);

    httpd_uri_t canserver_autostart_uri = {.uri = "/api/canserver/autostart", .method = HTTP_POST, .handler = canserver_autostart_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &canserver_autostart_uri);

    // Log Streaming route (Server-Sent Events)
    httpd_uri_t log_stream_uri = {.uri = "/api/logs/stream", .method = HTTP_GET, .handler = log_stream_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &log_stream_uri);

    httpd_uri_t log_file_status_uri = {.uri = "/api/logs/file/status", .method = HTTP_GET, .handler = log_file_status_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &log_file_status_uri);

    httpd_uri_t log_file_enable_uri = {.uri = "/api/logs/file/enable", .method = HTTP_POST, .handler = log_file_enable_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &log_file_enable_uri);

    httpd_uri_t log_file_clear_uri = {.uri = "/api/logs/file/clear", .method = HTTP_POST, .handler = log_file_clear_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &log_file_clear_uri);

    httpd_uri_t log_file_download_uri = {.uri = "/api/logs/file/download", .method = HTTP_GET, .handler = log_file_download_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &log_file_download_uri);

    // Register the 404 handler for the captive portal
    // All unknown URLs are redirected to the main page
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_portal_404_handler);

    ESP_LOGI(TAG_WEBSERVER, "Web server started with captive portal support (404 handler)");
    return ESP_OK;
  }

  ESP_LOGE(TAG_WEBSERVER, "Failed to start web server");
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
    ESP_LOGI(TAG_WEBSERVER, "Web server stopped");
  }
  return ESP_OK;
}

bool web_server_is_running(void) {
  return server != NULL;
}
