/**
 * @file main.c
 * @brief Point d'entrée principal du firmware Car Light Sync
 *
 * Gère:
 * - Initialisation de tous les sous-systèmes (NVS, WiFi, CAN, LED, Audio, BLE)
 * - Tâche principale de rendu LED (60 FPS)
 * - Détection et traitement événements CAN
 * - Mode nuit automatique basé sur l'heure
 * - Gestion du bouton reset pour retour usine
 */

#include "audio_input.h"
#include "ble_api_service.h"
#include "can_bus.h"
#include "canserver_udp_server.h" // Serveur UDP CANServer optionnel
#include "captive_portal.h"
#include "config.h"
#include "config_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "espnow_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gvret_tcp_server.h" // Serveur TCP GVRET optionnel
#include "led_effects.h"
#include "led_strip_encoder.h"
#include "log_stream.h"
#include "nvs_flash.h"
#include "ota_update.h"
#include "reset_button.h"
#include "sdkconfig.h"
#include "status_led.h"
#include "status_manager.h"
#include "task_core_utils.h"
#include "vehicle_can_mapping.h"
#include "vehicle_can_unified.h"
#include "version_info.h"
#include "web_server.h"
#include "wifi_credentials.h" // Configuration WiFi optionnelle
#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

// La config est générée par generate_vehicle_can_config.py
#include "vehicle_can_unified_config.h"

#ifdef CONFIG_HAS_PSRAM
#include "cJSON.h"
#include "esp_heap_caps.h"
#endif

#define TAG_MAIN "Main"

// ESP-NOW init (rôle et type d'esclave via Kconfig ou build flags PlatformIO)
// Configurable via Kconfig ou build flags PlatformIO :
//   -DESP_NOW_ROLE_STR=\"master\"|\"slave\"
//   -DESP_NOW_SLAVE_TYPE_STR=\"blindspot_left\"|\"blindspot_right\"|\"speedometer\"
#ifndef ESP_NOW_ROLE_STR
#ifdef CONFIG_ESP_NOW_ROLE
#define ESP_NOW_ROLE_STR CONFIG_ESP_NOW_ROLE
#else
#define ESP_NOW_ROLE_STR "master"
#endif
#endif

#ifndef ESP_NOW_SLAVE_TYPE_STR
#ifdef CONFIG_ESP_NOW_SLAVE_TYPE
#define ESP_NOW_SLAVE_TYPE_STR CONFIG_ESP_NOW_SLAVE_TYPE
#else
#define ESP_NOW_SLAVE_TYPE_STR "none"
#endif
#endif

// Si les macros sont passées sans guillemets (-DESP_NOW_ROLE_STR=slave), les convertir en chaînes
#ifndef ESPNOW_STR
#define ESPNOW_STR_HELPER(x) #x
#define ESPNOW_STR(x) ESPNOW_STR_HELPER(x)
#endif
#if !defined(ESP_NOW_ROLE_STR_LIT) && !defined(__cplusplus)
#define ESP_NOW_ROLE_STR_LIT ESPNOW_STR(ESP_NOW_ROLE_STR)
#undef ESP_NOW_ROLE_STR
#define ESP_NOW_ROLE_STR ESP_NOW_ROLE_STR_LIT
#endif
#if !defined(ESP_NOW_SLAVE_TYPE_STR_LIT) && !defined(__cplusplus)
#define ESP_NOW_SLAVE_TYPE_STR_LIT ESPNOW_STR(ESP_NOW_SLAVE_TYPE_STR)
#undef ESP_NOW_SLAVE_TYPE_STR
#define ESP_NOW_SLAVE_TYPE_STR ESP_NOW_SLAVE_TYPE_STR_LIT
#endif

static vehicle_state_t last_vehicle_state = {0};

// Callback pour les événements de scroll wheel (appelé depuis vehicle_state_apply_signal)
static void on_wheel_scroll_event(float scroll_value, const vehicle_state_t *state) {
  // Opt-in global
  if (!config_manager_get_wheel_control_enabled()) {
    return;
  }

  // Bloquer si autopilot/régulateur pas totalement désactivé
  if (state->autopilot != 0) {
    return;
  }

  // Bloquer au-dessus du seuil de vitesse
  if (state->speed_kph > config_manager_get_wheel_control_speed_limit()) {
    return;
  }

  // scroll_value > 0 = scroll up, scroll_value < 0 = scroll down
  if (scroll_value > 0) {
    config_manager_cycle_active_profile(+1);
  } else if (scroll_value < 0) {
    config_manager_cycle_active_profile(-1);
  }
}

// Callback pour les frames CAN (des deux bus)
static void vehicle_can_callback(const can_frame_t *frame, can_bus_type_t bus_type, void *user_data) {
  vehicle_can_process_frame_static(frame, &last_vehicle_state);
  led_effects_update_vehicle_state(&last_vehicle_state);
  web_server_update_vehicle_state(&last_vehicle_state);
}

// Callback pour les frames ESP-NOW reçues côté esclave : on réutilise le pipeline CAN existant
static void espnow_can_rx_handler(const espnow_can_frame_t *frame) {
  if (!frame) {
    return;
  }
  can_frame_t can = {0};
  can.id          = frame->can_id;
  can.dlc         = (frame->dlc > 8) ? 8 : frame->dlc;
  if (can.dlc) {
    memcpy(can.data, frame->data, can.dlc);
  }
  can_bus_type_t bus = CAN_BUS_BODY;
  if (frame->bus == CAN_BUS_CHASSIS) {
    bus = CAN_BUS_CHASSIS;
  }
  vehicle_can_callback(&can, bus, NULL);
}

// Tâche de mise à jour des LEDs
static void led_task(void *pvParameters) {
  ESP_LOGI(TAG_MAIN, "Tâche LED démarrée");

  while (1) {
    led_effects_update();
    config_manager_update();       // Gérer les effets temporaires
    vTaskDelay(pdMS_TO_TICKS(20)); // 50 FPS
  }
}

// Tâche de traitement des événements CAN
static void can_event_task(void *pvParameters) {
  ESP_LOGI(TAG_MAIN, "Tâche événements CAN démarrée");

  // Utiliser static pour éviter de surcharger la stack
  static vehicle_state_t current_state;
  static vehicle_state_t previous_state = {0};

  while (1) {
    // Copier l'état actuel
    memcpy(&current_state, &last_vehicle_state, sizeof(vehicle_state_t));

    // Note: handle_wheel_profile_control est maintenant appelé directement dans vehicle_can_callback
    // pour ne pas rater les événements de scroll qui reviennent vite à 0

    // Détecter les changements d'état et générer des événements
    // Clignotants - IMPORTANT: if séparés pour détecter chaque changement indépendamment

    if (previous_state.hazard != current_state.hazard) {
      ESP_LOGI(TAG_MAIN, "Hazard changé: %d -> %d", previous_state.hazard, current_state.hazard);
      if (current_state.hazard) {
        config_manager_process_can_event(CAN_EVENT_TURN_HAZARD);
      } else {
        config_manager_stop_event(CAN_EVENT_TURN_HAZARD);
      }
    }

    if (previous_state.turn_left != current_state.turn_left) {
      ESP_LOGI(TAG_MAIN, "Turn left changé: %d -> %d", previous_state.turn_left, current_state.turn_left);
      if (current_state.turn_left) {
        config_manager_process_can_event(CAN_EVENT_TURN_LEFT);
      } else {
        config_manager_stop_event(CAN_EVENT_TURN_LEFT);
      }
    }

    if (previous_state.turn_right != current_state.turn_right) {
      ESP_LOGI(TAG_MAIN, "Turn right changé: %d -> %d", previous_state.turn_right, current_state.turn_right);
      if (current_state.turn_right) {
        config_manager_process_can_event(CAN_EVENT_TURN_RIGHT);
      } else {
        config_manager_stop_event(CAN_EVENT_TURN_RIGHT);
      }
    }

    // Portes
    bool doors_open_now    = current_state.doors_open_count > 0;
    bool doors_open_before = previous_state.doors_open_count > 0;

    if (doors_open_now != doors_open_before) {
      if (doors_open_now) {
        config_manager_process_can_event(CAN_EVENT_DOOR_OPEN);
      } else {
        config_manager_process_can_event(CAN_EVENT_DOOR_CLOSE);
      }
    }

    // Verrouillage
    if (current_state.locked != previous_state.locked) {
      if (current_state.locked) {
        config_manager_process_can_event(CAN_EVENT_LOCKED);
      } else {
        config_manager_process_can_event(CAN_EVENT_UNLOCKED);
      }
    }

    // Transmission
    if (current_state.gear != previous_state.gear) {
      if (current_state.gear == 1) {
        config_manager_process_can_event(CAN_EVENT_GEAR_PARK);
        config_manager_stop_event(CAN_EVENT_GEAR_REVERSE);
        config_manager_stop_event(CAN_EVENT_GEAR_DRIVE);
      } else if (current_state.gear == 2) {
        config_manager_process_can_event(CAN_EVENT_GEAR_REVERSE);
        config_manager_stop_event(CAN_EVENT_GEAR_PARK);
        config_manager_stop_event(CAN_EVENT_GEAR_DRIVE);
      } else if (current_state.gear == 3) {
      } else if (current_state.gear == 4) {
        config_manager_process_can_event(CAN_EVENT_GEAR_DRIVE);
        config_manager_stop_event(CAN_EVENT_GEAR_PARK);
        config_manager_stop_event(CAN_EVENT_GEAR_REVERSE);
      }
    }

    // Freins
    if (current_state.brake_pressed != previous_state.brake_pressed) {
      ESP_LOGI(TAG_MAIN, "Brake changé: %d -> %d", previous_state.brake_pressed, current_state.brake_pressed);
      if (current_state.brake_pressed) {
        config_manager_process_can_event(CAN_EVENT_BRAKE_ON);
      } else {
        config_manager_stop_event(CAN_EVENT_BRAKE_ON);
      }
    }

    // Blindspot
    if (current_state.blindspot_left != previous_state.blindspot_left) {
      if (current_state.blindspot_left) {
        config_manager_process_can_event(CAN_EVENT_BLINDSPOT_LEFT);
      } else {
        config_manager_stop_event(CAN_EVENT_BLINDSPOT_LEFT);
      }
    }
    if (current_state.blindspot_right != previous_state.blindspot_right) {
      if (current_state.blindspot_right) {
        config_manager_process_can_event(CAN_EVENT_BLINDSPOT_RIGHT);
      } else {
        config_manager_stop_event(CAN_EVENT_BLINDSPOT_RIGHT);
      }
    }
    if (current_state.side_collision_left != previous_state.side_collision_left) {
      if (current_state.side_collision_left) {
        config_manager_process_can_event(CAN_EVENT_SIDE_COLLISION_LEFT);
      } else {
        config_manager_stop_event(CAN_EVENT_SIDE_COLLISION_LEFT);
      }
    }
    if (current_state.side_collision_right != previous_state.side_collision_right) {
      if (current_state.side_collision_right) {
        config_manager_process_can_event(CAN_EVENT_SIDE_COLLISION_RIGHT);
      } else {
        config_manager_stop_event(CAN_EVENT_SIDE_COLLISION_RIGHT);
      }
    }
    if (current_state.forward_collision != previous_state.forward_collision) {
      if (current_state.forward_collision) {
        config_manager_process_can_event(CAN_EVENT_FORWARD_COLLISION);
      } else {
        config_manager_stop_event(CAN_EVENT_FORWARD_COLLISION);
      }
    }
    if (current_state.lane_departure_left_lv1 != previous_state.lane_departure_left_lv1) {
      if (current_state.lane_departure_left_lv1) {
        config_manager_process_can_event(CAN_EVENT_LANE_DEPARTURE_LEFT_LV1);
      } else {
        config_manager_stop_event(CAN_EVENT_LANE_DEPARTURE_LEFT_LV1);
      }
    }
    if (current_state.lane_departure_left_lv2 != previous_state.lane_departure_left_lv2) {
      if (current_state.lane_departure_left_lv2) {
        config_manager_process_can_event(CAN_EVENT_LANE_DEPARTURE_LEFT_LV2);
      } else {
        config_manager_stop_event(CAN_EVENT_LANE_DEPARTURE_LEFT_LV2);
      }
    }
    if (current_state.lane_departure_right_lv1 != previous_state.lane_departure_right_lv1) {
      if (current_state.lane_departure_right_lv1) {
        config_manager_process_can_event(CAN_EVENT_LANE_DEPARTURE_RIGHT_LV1);
      } else {
        config_manager_stop_event(CAN_EVENT_LANE_DEPARTURE_RIGHT_LV1);
      }
    }
    if (current_state.lane_departure_right_lv2 != previous_state.lane_departure_right_lv2) {
      if (current_state.lane_departure_right_lv2) {
        config_manager_process_can_event(CAN_EVENT_LANE_DEPARTURE_RIGHT_LV2);
      } else {
        config_manager_stop_event(CAN_EVENT_LANE_DEPARTURE_RIGHT_LV2);
      }
    }

    if (current_state.sentry_mode != previous_state.sentry_mode) {
      if (current_state.sentry_mode) {
        config_manager_process_can_event(CAN_EVENT_SENTRY_MODE_ON);
      } else {
        config_manager_stop_event(CAN_EVENT_SENTRY_MODE_OFF);
      }
    }
    if (current_state.sentry_alert != previous_state.sentry_alert) {
      if (current_state.sentry_alert) {
        config_manager_process_can_event(CAN_EVENT_SENTRY_ALERT);
      }
    }
    // Autopilot
    // 0 "DISABLED"
    // 1 "UNAVAILABLE"
    // 2 "AVAILABLE"
    // 3 "ACTIVE_NOMINAL"
    // 4 "ACTIVE_RESTRICTED"
    // 5 "ACTIVE_NAV"
    // 8 "ABORTING"
    // 9 "ABORTED"
    // 14 "FAULT"
    // 15 "SNA"
    if (current_state.autopilot != previous_state.autopilot) {
      if (current_state.autopilot >= 3 && current_state.autopilot <= 5) {
        config_manager_process_can_event(CAN_EVENT_AUTOPILOT_ENGAGED);
        config_manager_stop_event(CAN_EVENT_AUTOPILOT_DISENGAGED);
        config_manager_stop_event(CAN_EVENT_AUTOPILOT_ABORTING);
      } else if (current_state.autopilot == 9) {
        config_manager_process_can_event(CAN_EVENT_AUTOPILOT_DISENGAGED);
        config_manager_stop_event(CAN_EVENT_AUTOPILOT_ENGAGED);
        config_manager_stop_event(CAN_EVENT_AUTOPILOT_ABORTING);
      } else if (current_state.autopilot == 8) {
        config_manager_process_can_event(CAN_EVENT_AUTOPILOT_ABORTING);
        config_manager_stop_event(CAN_EVENT_AUTOPILOT_ENGAGED);
        config_manager_stop_event(CAN_EVENT_AUTOPILOT_DISENGAGED);
      }
    }

    // Charge
    if (current_state.charging != previous_state.charging) {
      if (current_state.charging) {
        config_manager_process_can_event(CAN_EVENT_CHARGING);
      } else {
        config_manager_stop_event(CAN_EVENT_CHARGING);
      }
    }
    if (current_state.charging_cable != previous_state.charging_cable) {
      if (current_state.charging_cable) {
        config_manager_process_can_event(CAN_EVENT_CHARGING_CABLE_CONNECTED);
      } else {
        config_manager_process_can_event(CAN_EVENT_CHARGING_CABLE_DISCONNECTED);
      }
    }
    if (current_state.charging_port != previous_state.charging_port) {
      if (current_state.charging_port) {
        config_manager_process_can_event(CAN_EVENT_CHARGING_PORT_OPENED);
      } else {
        config_manager_stop_event(CAN_EVENT_CHARGING_PORT_OPENED);
      }
    }

    if (current_state.charge_status != previous_state.charge_status) {
      if (current_state.charge_status == 3) {
        // config_manager_process_can_event(CAN_EVENT_CHARGING);
      } else if (current_state.charge_status == 4) {
        config_manager_process_can_event(CAN_EVENT_CHARGE_COMPLETE);
      } else if (current_state.charge_status == 5) {
        config_manager_process_can_event(CAN_EVENT_CHARGING_STARTED);
      } else if (current_state.charge_status == 1) {
        config_manager_process_can_event(CAN_EVENT_CHARGING_STOPPED);
      } else {
        config_manager_stop_event(CAN_EVENT_CHARGING);
        config_manager_stop_event(CAN_EVENT_CHARGE_COMPLETE);
        config_manager_stop_event(CAN_EVENT_CHARGING_STARTED);
        config_manager_stop_event(CAN_EVENT_CHARGING_STOPPED);
      }
    }

    // Seuil de vitesse
    if (current_state.speed_kph != previous_state.speed_kph) {
      if (current_state.speed_kph > current_state.speed_threshold) {
        config_manager_process_can_event(CAN_EVENT_SPEED_THRESHOLD);
      } else {
        config_manager_stop_event(CAN_EVENT_SPEED_THRESHOLD);
      }
    }

    // Sauvegarder l'état précédent
    memcpy(&previous_state, &current_state, sizeof(vehicle_state_t));

    vTaskDelay(pdMS_TO_TICKS(50)); // Vérifier toutes les 50ms
  }
}

// Helper interne pour mettre à jour la LED de statut selon l'activité
static void update_status_led_internal(void) {
  wifi_status_t wifi_status;
  wifi_manager_get_status(&wifi_status);

  can_bus_status_t can_body_status, can_chassis_status;
  can_bus_get_status(CAN_BUS_BODY, &can_body_status);
  can_bus_get_status(CAN_BUS_CHASSIS, &can_chassis_status);

  // Priorité des états (du plus prioritaire au moins prioritaire)
  if (ble_api_service_is_connected()) {
    status_led_set_state(STATUS_LED_BLE_CONNECTED);
  } else if (can_body_status.running || can_body_status.running) {
    // CAN actif (au moins un bus)
    status_led_set_state(STATUS_LED_CAN_ACTIVE);
  } else if (wifi_status.sta_connected) {
    // WiFi connecté en mode station
    status_led_set_state(STATUS_LED_WIFI_STATION);
  } else if (wifi_status.ap_started && wifi_status.connected_clients > 0) {
    // Mode AP actif (clients connectés)
    status_led_set_state(STATUS_LED_WIFI_AP);
  } else {
    // Mode idle par défaut (aucune connexion)
    status_led_set_state(STATUS_LED_IDLE);
  }
}

// Fonction publique pour forcer la mise à jour (appelée depuis reset_button.c)
// FORCE le changement même si la LED est en mode FACTORY_RESET
void status_manager_update_led_now(void) {
  update_status_led_internal();
}

// Tâche de monitoring
static void monitor_task(void *pvParameters) {
  TickType_t last_print          = 0;
  TickType_t last_activity_check = 0;

  while (1) {
    TickType_t now = xTaskGetTickCount();

    // Mettre à jour la LED de statut toutes les 5 secondes
    if (now - last_activity_check > pdMS_TO_TICKS(5000)) {
      // Ne pas changer la LED si elle est en mode FACTORY_RESET (reset en
      // cours)
      if (status_led_get_state() != STATUS_LED_FACTORY_RESET) {
        update_status_led_internal();
      }
      last_activity_check = now;
    }

    // Afficher les stats toutes les 30 secondes
    if (now - last_print > pdMS_TO_TICKS(30000)) {
      wifi_status_t wifi_status;
      wifi_manager_get_status(&wifi_status);

      can_bus_status_t can_chassis_status, can_body_status;
      can_bus_get_status(CAN_BUS_CHASSIS, &can_chassis_status);
      can_bus_get_status(CAN_BUS_BODY, &can_body_status);

      ESP_LOGI(TAG_MAIN, "=== Statut ===");
      ESP_LOGI(TAG_MAIN, "WiFi AP: %s (IP: %s, Clients: %d)", wifi_status.ap_started ? "Actif" : "Inactif", wifi_status.ap_ip, wifi_status.connected_clients);

      if (wifi_status.sta_connected) {
        ESP_LOGI(TAG_MAIN, "WiFi STA: Connecté à %s (IP: %s)", wifi_status.sta_ssid, wifi_status.sta_ip);
      }

      if (can_body_status.running) {
        ESP_LOGI(TAG_MAIN, "CAN BODY: RX=%lu, TX=%lu, Err=%lu", can_body_status.rx_count, can_body_status.tx_count, can_body_status.errors);
      } else {
        ESP_LOGI(TAG_MAIN, "CAN BODY: Déconnecté");
      }

      if (can_chassis_status.running) {
        ESP_LOGI(TAG_MAIN, "CAN CHASSIS: RX=%lu, TX=%lu, Err=%lu", can_chassis_status.rx_count, can_chassis_status.tx_count, can_chassis_status.errors);
      } else {
        ESP_LOGI(TAG_MAIN, "CAN CHASSIS: Déconnecté");
      }
      ESP_LOGI(TAG_MAIN, "Mémoire libre: %lu bytes", esp_get_free_heap_size());
#ifdef CONFIG_HAS_PSRAM
      ESP_LOGI(TAG_MAIN, "PSRAM libre: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
      ESP_LOGI(TAG_MAIN, "==============");

      last_print = now;
    }

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

/**
 * Stratégie d'allocation mémoire dans le projet
 * =============================================
 *
 * Ce projet utilise deux méthodes d'allocation selon le contexte :
 *
 * 1. malloc() / free() - RAM interne (SRAM)
 *    - UTILISATION : Structures de configuration, buffers temporaires
 *    - AVANTAGES : Rapide, accessible par tous les périphériques (RMT, SPI, etc.)
 *    - LIMITES : ~200-300KB disponible sur ESP32-S3
 *    - EXEMPLES : config_profile_t, buffers JSON courts
 *
 * 2. heap_caps_malloc(size, MALLOC_CAP_SPIRAM) - PSRAM externe
 *    - UTILISATION : Grands buffers, données cJSON (si CONFIG_HAS_PSRAM)
 *    - AVANTAGES : ~8MB disponible, réduit la pression sur la SRAM
 *    - LIMITES : Plus lent, incompatible avec DMA/RMT
 *    - EXEMPLES : Buffers BLE, réponses HTTP volumineuses
 *
 * 3. heap_caps_malloc(size, MALLOC_CAP_DEFAULT) - RAM par défaut
 *    - UTILISATION : Allocation générique compatible BLE/WiFi
 *    - AVANTAGES : Compatible avec tous les contextes
 *    - LIMITES : Utilise la SRAM interne
 *    - EXEMPLES : Buffers BLE temporaires
 *
 * RÈGLES DE CHOIX :
 * - LED/RMT buffers : toujours malloc() (DMA incompatible avec PSRAM)
 * - Config profiles : malloc() (accès fréquent, petite taille)
 * - Buffers JSON > 4KB : heap_caps_malloc(PSRAM) si disponible
 * - Buffers BLE : heap_caps_malloc(DEFAULT) pour compatibilité
 * - Buffers temporaires < 1KB : stack (variables locales)
 */

#ifdef CONFIG_HAS_PSRAM
// Fonctions d'allocation mémoire pour cJSON utilisant la PSRAM
static void *psram_malloc(size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void psram_free(void *ptr) {
  heap_caps_free(ptr);
}
#endif

void app_main(void) {
// Désactivé temporairement : cJSON utilise la RAM normale au lieu de PSRAM
// pour éviter les conflits avec la tâche LED/RMT
#ifdef CONFIG_HAS_PSRAM
  cJSON_Hooks hooks = {.malloc_fn = psram_malloc, .free_fn = psram_free};
  cJSON_InitHooks(&hooks);
#endif

  espnow_role_t espnow_role       = (strcmp(ESP_NOW_ROLE_STR, "slave") == 0) ? ESP_NOW_ROLE_SLAVE : ESP_NOW_ROLE_MASTER;
  espnow_slave_type_t espnow_type = ESP_NOW_SLAVE_NONE;
  if (strcmp(ESP_NOW_SLAVE_TYPE_STR, "blindspot_left") == 0) {
    espnow_type = ESP_NOW_SLAVE_BLINDSPOT_LEFT;
  } else if (strcmp(ESP_NOW_SLAVE_TYPE_STR, "blindspot_right") == 0) {
    espnow_type = ESP_NOW_SLAVE_BLINDSPOT_RIGHT;
  } else if (strcmp(ESP_NOW_SLAVE_TYPE_STR, "speedometer") == 0) {
    espnow_type = ESP_NOW_SLAVE_SPEEDOMETER;
  }

  // Réduire le niveau de log de tous les composants ESP-IDF
  esp_log_level_set("*", ESP_LOG_WARN);     // Par défaut : warnings seulement
  esp_log_level_set("wifi", ESP_LOG_ERROR); // WiFi : erreurs seulement
  esp_log_level_set("esp_netif_handlers", ESP_LOG_ERROR);

  // Activer VOS logs
  esp_log_level_set(TAG_MAIN, ESP_LOG_INFO);
  esp_log_level_set(TAG_CAN_BUS, ESP_LOG_INFO);
  esp_log_level_set(TAG_CAN, ESP_LOG_INFO);
  esp_log_level_set(TAG_WIFI, ESP_LOG_INFO); // nos logs du module WiFi
  esp_log_level_set(TAG_WEBSERVER, ESP_LOG_INFO);
  // esp_log_level_set(TAG_LED_ENCODER, ESP_LOG_INFO);
  // esp_log_level_set(TAG_LED, ESP_LOG_INFO);
  esp_log_level_set(TAG_CONFIG, ESP_LOG_INFO);
  // esp_log_level_set(TAG_OTA, ESP_LOG_INFO);
  // esp_log_level_set(TAG_AUDIO, ESP_LOG_INFO);
  // esp_log_level_set(TAG_BLE_API, ESP_LOG_INFO);

  ESP_LOGI(TAG_MAIN, "=================================");
  ESP_LOGI(TAG_MAIN, "        Car Light Sync           ");
  ESP_LOGI(TAG_MAIN, "       Version %s            ", APP_VERSION_STRING);
  ESP_LOGI(TAG_MAIN, "=================================");

  // Initialiser les noms de périphérique avec suffixe MAC
  config_init_device_names();
  ESP_LOGI(TAG_MAIN, "WiFi AP SSID: %s", g_wifi_ssid_with_suffix);
  ESP_LOGI(TAG_MAIN, "BLE Device Name: %s", g_device_name_with_suffix);

  // Initialiser NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Valider la partition OTA actuelle (après un update)
  ESP_ERROR_CHECK(ota_validate_current_partition());

  // Initialiser OTA
  ESP_ERROR_CHECK(ota_init());
  ESP_LOGI(TAG_MAIN, "✓ OTA initialisé, version: %s", ota_get_current_version());

  // Initialiser les modules
  ESP_LOGI(TAG_MAIN, "Initialisation des modules...");

  // LED de statut (WS2812 intégrée)
  esp_err_t status_led_err = status_led_init();
  if (status_led_err == ESP_OK) {
    ESP_LOGI(TAG_MAIN, "✓ LED de statut initialisée");
    status_led_set_state(STATUS_LED_BOOT);
  } else if (status_led_err != ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGW(TAG_MAIN, "Erreur init LED de statut");
  }

  // Bouton reset
  esp_err_t reset_btn_err = reset_button_init();
  if (reset_btn_err == ESP_OK) {
    ESP_LOGI(TAG_MAIN, "✓ Bouton reset initialisé (GPIO 4, maintenir 5s = factory reset)");
  } else {
    ESP_LOGW(TAG_MAIN, "Erreur init bouton reset");
  }

  ESP_ERROR_CHECK(espnow_link_init(espnow_role, espnow_type));
  ESP_LOGI(TAG_MAIN, "✓ ESPNow initialisé");

  // CAN : uniquement pour le maître (les esclaves ESP-NOW n'ont pas de contrôleur CAN)
  if (espnow_role == ESP_NOW_ROLE_MASTER) {
    // CAN bus - Body
    ESP_ERROR_CHECK(can_bus_init(CAN_BUS_BODY, CAN_TX_BODY_PIN, CAN_RX_BODY_PIN));
    ESP_LOGI(TAG_MAIN, "✓ CAN bus BODY initialisé (GPIO TX=%d, RX=%d)", CAN_TX_BODY_PIN, CAN_RX_BODY_PIN);

    // CAN bus - Chassis
    ESP_ERROR_CHECK(can_bus_init(CAN_BUS_CHASSIS, CAN_TX_CHASSIS_PIN, CAN_RX_CHASSIS_PIN));
    ESP_LOGI(TAG_MAIN, "✓ CAN bus CHASSIS initialisé (GPIO TX=%d, RX=%d)", CAN_TX_CHASSIS_PIN, CAN_RX_CHASSIS_PIN);

    // Enregistrer le callback partagé pour les deux bus
    ESP_ERROR_CHECK(can_bus_register_callback(vehicle_can_callback, NULL));

    // Enregistrer le callback pour les événements de scroll wheel
    vehicle_can_set_wheel_scroll_callback(on_wheel_scroll_event);

    // Démarrer les deux bus CAN
    ESP_ERROR_CHECK(can_bus_start(CAN_BUS_CHASSIS));
    ESP_ERROR_CHECK(can_bus_start(CAN_BUS_BODY));
    ESP_LOGI(TAG_MAIN, "✓ Les deux bus CAN sont démarrés");
  } else {
    ESP_LOGI(TAG_MAIN, "Mode esclave ESP-NOW : CAN désactivé");
    // Les frames CAN reçues via ESP-NOW sont injectées dans le pipeline de décodage
    espnow_link_register_rx_callback(espnow_can_rx_handler);
  }

  // LEDs
  if (!led_effects_init()) {
    ESP_LOGE(TAG_MAIN, "✗ Erreur initialisation LEDs");
    return;
  }
  ESP_LOGI(TAG_MAIN, "✓ LEDs initialisées");

  // Gestionnaire de configuration
  if (!config_manager_init()) {
    ESP_LOGE(TAG_MAIN, "✗ Erreur initialisation config manager");
    return;
  }
  ESP_LOGI(TAG_MAIN, "✓ Gestionnaire de configuration initialisé");

  if (espnow_role == ESP_NOW_ROLE_MASTER) {
    // Module audio (micro INMP441)
    if (!audio_input_init()) {
      ESP_LOGW(TAG_MAIN, "Module audio non disponible (optionnel)");
    } else {
      ESP_LOGI(TAG_MAIN, "✓ Module audio initialisé");
      // Réappliquer l'effet par défaut pour activer l'audio si nécessaire
      // (car l'effet a été appliqué avant l'init audio)
      config_manager_reapply_default_effect();
    }
  }

#if CONFIG_BT_ENABLED
  esp_err_t ble_init_status = ble_api_service_init();
  if (ble_init_status == ESP_OK) {
    esp_err_t ble_start_status = ble_api_service_start();
    if (ble_start_status != ESP_OK) {
      ESP_LOGW(TAG_MAIN, "Impossible de démarrer le service BLE: %s", esp_err_to_name(ble_start_status));
    }
  } else {
    ESP_LOGW(TAG_MAIN, "Service BLE non disponible: %s", esp_err_to_name(ble_init_status));
  }
#else
  ESP_LOGW(TAG_MAIN, "BLE désactivé dans la configuration, Web Bluetooth indisponible");
#endif

  // Créer les tâches
  create_task_on_led_core(led_task, "led_task", 4096, NULL, 5, NULL);
  create_task_on_general_core(can_event_task, "can_event_task", 8192, NULL, 4,
                              NULL); // Augmenté à 8KB à cause de config_profile_t
  create_task_on_general_core(monitor_task, "monitor_task", 4096, NULL, 2, NULL);

  // WiFi
  status_led_set_state(STATUS_LED_WIFI_CONNECTING);
  ESP_ERROR_CHECK(wifi_manager_init());
  ESP_ERROR_CHECK(captive_portal_init());
  ESP_ERROR_CHECK(wifi_manager_start_ap());

#ifdef WIFI_AUTO_CONNECT
  // Connexion automatique au WiFi domestique si configuré
  ESP_LOGI(TAG_MAIN, "Tentative de connexion à %s...", WIFI_HOME_SSID);
  wifi_manager_connect_sta(WIFI_HOME_SSID, WIFI_HOME_PASSWORD);
  vTaskDelay(pdMS_TO_TICKS(5000)); // Attendre 5s pour la connexion
#endif

  ESP_LOGI(TAG_MAIN, "✓ WiFi initialisé");
  status_led_set_state(STATUS_LED_WIFI_AP);

  // Serveur web
  ESP_ERROR_CHECK(web_server_init());
  ESP_ERROR_CHECK(web_server_start());
  ESP_LOGI(TAG_MAIN, "✓ Serveur web démarré");

  // Log streaming (Server-Sent Events pour logs temps réel)
  ESP_ERROR_CHECK(log_stream_init());
  ESP_LOGI(TAG_MAIN, "✓ Log streaming initialisé");

  if (espnow_role == ESP_NOW_ROLE_MASTER) {
    // Serveur GVRET TCP (initialisé mais pas démarré - contrôlé via interface web)
    ESP_ERROR_CHECK(gvret_tcp_server_init());
    ESP_LOGI(TAG_MAIN, "✓ Serveur GVRET TCP initialise (port 23, activable via interface web)");
    if (gvret_tcp_server_get_autostart()) {
      ESP_ERROR_CHECK(gvret_tcp_server_start());
      ESP_LOGI(TAG_MAIN, "  → Démarrage automatique activé (GVRET)");
    }

    // Serveur CANServer UDP (initialisé mais pas démarré - contrôlé via interface web)
    ESP_ERROR_CHECK(canserver_udp_server_init());
    ESP_LOGI(TAG_MAIN, "✓ Serveur CANServer UDP initialise (port 1338, activable via interface web)");
    if (canserver_udp_server_get_autostart()) {
      ESP_ERROR_CHECK(canserver_udp_server_start());
      ESP_LOGI(TAG_MAIN, "  → Démarrage automatique activé (CANServer)");
    }

  }

  // Afficher les informations de connexion
  wifi_status_t wifi_status;
  wifi_manager_get_status(&wifi_status);

  ESP_LOGI(TAG_MAIN, "");
  ESP_LOGI(TAG_MAIN, "=================================");
  ESP_LOGI(TAG_MAIN, "  Interface Web Disponible");
  ESP_LOGI(TAG_MAIN, "  SSID: %s", g_wifi_ssid_with_suffix);
  ESP_LOGI(TAG_MAIN, "  Password: %s", WIFI_AP_PASSWORD);
  ESP_LOGI(TAG_MAIN, "  URL: http://%s", wifi_status.ap_ip);
  ESP_LOGI(TAG_MAIN, "=================================");
  ESP_LOGI(TAG_MAIN, "");

  ESP_LOGI(TAG_MAIN, "Système démarré avec succès !");

  // Animation de démarrage désactivée pour éviter les conflits RMT
  // L'animation sera gérée par la tâche LED

  // La boucle principale se termine, les tâches FreeRTOS continuent
  ESP_LOGI(TAG_MAIN, "app_main terminé, tâches en cours d'exécution");
}
