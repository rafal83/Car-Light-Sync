#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "sdkconfig.h"
#include "config.h"
#include "wifi_manager.h"
#include "captive_portal.h"
#include "wifi_credentials.h"  // Configuration WiFi optionnelle
#include "can_bus.h"
#include "led_effects.h"
#include "web_server.h"
#include "vehicle_can_unified.h"
#include "config_manager.h"
#include "ota_update.h"
#include "version_info.h"
#include "task_core_utils.h"
#include "ble_api_service.h"


// La config est générée par generate_vehicle_can_config.py
#include "vehicle_can_unified_config.h"

#ifdef CONFIG_HAS_PSRAM
#include "cJSON.h"
#include "esp_heap_caps.h"
#endif

static const char *TAG = "Main";

static vehicle_state_t last_vehicle_state = {0};


// Callback pour les frames CAN
static void vehicle_can_callback(const can_frame_t* frame, void* user_data) {
    vehicle_can_process_frame_static(frame, &last_vehicle_state);
    led_effects_update_vehicle_state(&last_vehicle_state);
    web_server_update_vehicle_state(&last_vehicle_state);
}

// Tâche de mise à jour des LEDs
static void led_task(void *pvParameters) {
    ESP_LOGI(TAG, "Tâche LED démarrée");
    
    while (1) {
        led_effects_update();
        config_manager_update();  // Gérer les effets temporaires
        vTaskDelay(pdMS_TO_TICKS(20)); // 50 FPS
    }
}

// Tâche de traitement des événements CAN
static void can_event_task(void *pvParameters) {
    ESP_LOGI(TAG, "Tâche événements CAN démarrée");

    // Utiliser static pour éviter de surcharger la stack
    static vehicle_state_t current_state;
    static vehicle_state_t previous_state = {0};
    // static config_profile_t profile;
    // bool has_profile = false;

    while (1) {
        // Copier l'état actuel
        memcpy(&current_state, &last_vehicle_state, sizeof(vehicle_state_t));

        // has_profile = config_manager_get_active_profile(&profile);

        // Détecter les changements d'état et générer des événements
        // Clignotants - IMPORTANT: if séparés pour détecter chaque changement indépendamment
        if (previous_state.hazard != current_state.hazard) {
          ESP_LOGI(TAG, "Hazard changé: %d -> %d", previous_state.hazard, current_state.hazard);
          if(current_state.hazard) {
            config_manager_process_can_event(CAN_EVENT_TURN_HAZARD);
          } else {
            config_manager_stop_event(CAN_EVENT_TURN_HAZARD);
          }
        }

        if (previous_state.turn_left != current_state.turn_left) {
          ESP_LOGI(TAG, "Turn left changé: %d -> %d", previous_state.turn_left, current_state.turn_left);
          if(current_state.turn_left) {
            config_manager_process_can_event(CAN_EVENT_TURN_LEFT);
          } else {
            config_manager_stop_event(CAN_EVENT_TURN_LEFT);
          }
        }

        if (previous_state.turn_right != current_state.turn_right) {
          ESP_LOGI(TAG, "Turn right changé: %d -> %d", previous_state.turn_right, current_state.turn_right);
          if(current_state.turn_right) {
            config_manager_process_can_event(CAN_EVENT_TURN_RIGHT);
          } else {
            config_manager_stop_event(CAN_EVENT_TURN_RIGHT);
          }
        }        
                
        // Portes
        bool doors_open_now = current_state.doors_open_count > 0;
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
            if (current_state.gear==1) {
              config_manager_process_can_event(CAN_EVENT_GEAR_PARK);
            } else if (current_state.gear==2) {
              config_manager_process_can_event(CAN_EVENT_GEAR_REVERSE);
            } else if (current_state.gear==3) {
            } else if (current_state.gear==4) {
              config_manager_process_can_event(CAN_EVENT_GEAR_DRIVE);
            }
        }
        
        // Freins
        if (current_state.brake_pressed != previous_state.brake_pressed) {
            if (current_state.brake_pressed) {
                config_manager_process_can_event(CAN_EVENT_BRAKE_ON);
            } else {
                config_manager_process_can_event(CAN_EVENT_BRAKE_OFF);
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
        if (current_state.blindspot_warning != previous_state.blindspot_warning) {
          if (current_state.blindspot_warning) {
            config_manager_process_can_event(CAN_EVENT_BLINDSPOT_WARNING);
          } else {
            config_manager_stop_event(CAN_EVENT_BLINDSPOT_WARNING);
          }
        }
        if (current_state.forward_colission != previous_state.forward_colission) {
          if (current_state.forward_colission) {
            config_manager_process_can_event(CAN_EVENT_FORWARD_COLISSION);
          } else {
            config_manager_stop_event(CAN_EVENT_FORWARD_COLISSION);
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
        // 9 "ABORTED" 
        // 8 "ABORTING" 
        // 5 "ACTIVE_NAV" 
        // 3 "ACTIVE_NOMINAL" 
        // 4 "ACTIVE_RESTRICTED" 
        // 2 "AVAILABLE" 
        // 0 "DISABLED" 
        // 14 "FAULT" 
        // 15 "SNA" 
        // 1 "UNAVAILABLE"
        if (current_state.autopilot != previous_state.autopilot) {
          if (current_state.autopilot >= 3 && current_state.autopilot <= 5) {
            config_manager_process_can_event(CAN_EVENT_AUTOPILOT_ENGAGED);
          } else if (current_state.autopilot == 9) {
            config_manager_stop_event(CAN_EVENT_AUTOPILOT_DISENGAGED);
          } else if (current_state.autopilot == 8) {
            config_manager_stop_event(CAN_EVENT_AUTOPILOT_ABORTING);
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

        // Mode nuit
        // if (current_state.night_mode != previous_state.night_mode) {
        //     if (current_state.night_mode) {
        //         config_manager_process_can_event(CAN_EVENT_NIGHT_MODE_ON);
                
        //         // Appliquer automatiquement l'effet mode nuit si configuré
        //         config_profile_t profile;
        //         if (config_manager_get_active_profile(&profile) && profile.auto_night_mode) {
        //             led_effects_set_config(&profile.night_mode_effect);
        //             ESP_LOGI(TAG, "Mode nuit activé automatiquement");
        //         }
        //     } else {
        //         config_manager_process_can_event(CAN_EVENT_NIGHT_MODE_OFF);
                
        //         // Retour à l'effet par défaut
        //         config_profile_t profile;
        //         if (config_manager_get_active_profile(&profile) && profile.auto_night_mode) {
        //             led_effects_set_config(&profile.default_effect);
        //             ESP_LOGI(TAG, "Mode nuit désactivé");
        //         }
        //     }
        // }
        
        // Seuil de vitesse - Utiliser static pour éviter surcharge de la stack
        if (current_state.speed_kph > current_state.speed_threshold) {
          config_manager_process_can_event(CAN_EVENT_SPEED_THRESHOLD);
        } else {
          config_manager_stop_event(CAN_EVENT_SPEED_THRESHOLD);
        }
        
        // Sauvegarder l'état précédent
        memcpy(&previous_state, &current_state, sizeof(vehicle_state_t));

        vTaskDelay(pdMS_TO_TICKS(50)); // Vérifier toutes les 50ms
    }
}


// Tâche de monitoring
static void monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Tâche de monitoring démarrée");
    
    TickType_t last_print = 0;
    
    while (1) {
        TickType_t now = xTaskGetTickCount();
        
        // Afficher les stats toutes les 30 secondes
        if (now - last_print > pdMS_TO_TICKS(30000)) {
            wifi_status_t wifi_status;
            wifi_manager_get_status(&wifi_status);
            
            can_bus_status_t can_status;
            can_bus_get_status(&can_status);
            
            ESP_LOGI(TAG, "=== Statut ===");
            ESP_LOGI(TAG, "WiFi AP: %s (IP: %s, Clients: %d)", 
                    wifi_status.ap_started ? "Actif" : "Inactif",
                    wifi_status.ap_ip,
                    wifi_status.connected_clients);
            
            if (wifi_status.sta_connected) {
                ESP_LOGI(TAG, "WiFi STA: Connecté à %s (IP: %s)", 
                        wifi_status.sta_ssid, wifi_status.sta_ip);
            }
            
            if (can_status.running) {
              ESP_LOGI(TAG, "CAN: RX=%lu, TX=%lu, Err=%lu, Running=%s",
                      can_status.rx_count,
                      can_status.tx_count,
                      can_status.errors,
                      can_status.running ? "oui" : "non");
            } else {
                ESP_LOGI(TAG, "CAN Bus: Déconnecté");
            }
            
            ESP_LOGI(TAG, "Mémoire libre: %lu bytes", esp_get_free_heap_size());
#ifdef CONFIG_HAS_PSRAM
            ESP_LOGI(TAG, "PSRAM libre: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
            ESP_LOGI(TAG, "==============");
            
            last_print = now;
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

#ifdef CONFIG_HAS_PSRAM
// Fonctions d'allocation mémoire pour cJSON utilisant la PSRAM
static void* psram_malloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void psram_free(void* ptr) {
    heap_caps_free(ptr);
}
#endif

void app_main(void) {
    // Désactivé temporairement : cJSON utilise la RAM normale au lieu de PSRAM
    // pour éviter les conflits avec la tâche LED/RMT
    #ifdef CONFIG_HAS_PSRAM
        cJSON_Hooks hooks = {
            .malloc_fn = psram_malloc,
            .free_fn = psram_free
        };
        cJSON_InitHooks(&hooks);
    #endif

    // Réduire le niveau de log de tous les composants ESP-IDF
    esp_log_level_set("*", ESP_LOG_WARN);           // Par défaut : warnings seulement
    esp_log_level_set("wifi", ESP_LOG_ERROR);       // WiFi : erreurs seulement
    esp_log_level_set("esp_netif_handlers", ESP_LOG_ERROR);
    
    // Activer VOS logs
    esp_log_level_set("Main", ESP_LOG_INFO);
    esp_log_level_set("CAN_BUS", ESP_LOG_INFO);
    esp_log_level_set("WiFi", ESP_LOG_INFO);        // Votre module WiFi
    esp_log_level_set("WebServer", ESP_LOG_INFO);
    esp_log_level_set("LEDStrip", ESP_LOG_INFO);
    esp_log_level_set("ConfigMgr", ESP_LOG_INFO);
    esp_log_level_set("OTA", ESP_LOG_INFO);
    esp_log_level_set("BLE_API", ESP_LOG_INFO);

    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "        Car Light Sync           ");
    ESP_LOGI(TAG, "       Version %s            ", APP_VERSION_STRING);
    ESP_LOGI(TAG, "=================================");

    // Initialiser les noms de périphérique avec suffixe MAC
    config_init_device_names();
    ESP_LOGI(TAG, "WiFi AP SSID: %s", g_wifi_ssid_with_suffix);
    ESP_LOGI(TAG, "BLE Device Name: %s", g_device_name_with_suffix);
    
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
    ESP_LOGI(TAG, "✓ OTA initialisé, version: %s", ota_get_current_version());

    // Initialiser les modules
    ESP_LOGI(TAG, "Initialisation des modules...");
        
    // CAN direct
    ESP_ERROR_CHECK(can_bus_init());
    ESP_ERROR_CHECK(can_bus_register_callback(vehicle_can_callback, NULL));
    ESP_ERROR_CHECK(can_bus_start());
    ESP_LOGI(TAG, "✓ CAN direct initialisé");
    
    // LEDs
    if (!led_effects_init()) {
        ESP_LOGE(TAG, "✗ Erreur initialisation LEDs");
        return;
    }
    ESP_LOGI(TAG, "✓ LEDs initialisées");
    
    // Gestionnaire de configuration
    if (!config_manager_init()) {
        ESP_LOGE(TAG, "✗ Erreur initialisation config manager");
        return;
    }
    ESP_LOGI(TAG, "✓ Gestionnaire de configuration initialisé");

#if CONFIG_BT_ENABLED
    esp_err_t ble_init_status = ble_api_service_init();
    if (ble_init_status == ESP_OK) {
        esp_err_t ble_start_status = ble_api_service_start();
        if (ble_start_status != ESP_OK) {
            ESP_LOGW(TAG, "Impossible de démarrer le service BLE: %s",
                     esp_err_to_name(ble_start_status));
        }
    } else {
        ESP_LOGW(TAG, "Service BLE non disponible: %s", esp_err_to_name(ble_init_status));
    }
#else
    ESP_LOGW(TAG, "BLE désactivé dans la configuration, Web Bluetooth indisponible");
#endif
    
    // Créer les tâches
    create_task_on_led_core(led_task, "led_task", 4096, NULL, 5, NULL);
    create_task_on_general_core(can_event_task, "can_event_task", 8192, NULL, 4, NULL);  // Augmenté à 8KB à cause de config_profile_t
    create_task_on_general_core(monitor_task, "monitor_task", 4096, NULL, 2, NULL);
        
    // WiFi
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(captive_portal_init());
    ESP_ERROR_CHECK(wifi_manager_start_ap());

#ifdef WIFI_AUTO_CONNECT
    // Connexion automatique au WiFi domestique si configuré
    ESP_LOGI(TAG, "Tentative de connexion à %s...", WIFI_HOME_SSID);
    wifi_manager_connect_sta(WIFI_HOME_SSID, WIFI_HOME_PASSWORD);
    vTaskDelay(pdMS_TO_TICKS(5000)); // Attendre 5s pour la connexion
#endif

    ESP_LOGI(TAG, "✓ WiFi initialisé");

    // Serveur web
    ESP_ERROR_CHECK(web_server_init());
    ESP_ERROR_CHECK(web_server_start());
    ESP_LOGI(TAG, "✓ Serveur web démarré");
    
    // Afficher les informations de connexion
    wifi_status_t wifi_status;
    wifi_manager_get_status(&wifi_status);
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "  Interface Web Disponible");
    ESP_LOGI(TAG, "  SSID: %s", WIFI_AP_SSID);
    ESP_LOGI(TAG, "  Password: %s", WIFI_AP_PASSWORD);
    ESP_LOGI(TAG, "  URL: http://%s", wifi_status.ap_ip);
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "");

    ESP_LOGI(TAG, "Système démarré avec succès !");

    // Animation de démarrage désactivée pour éviter les conflits RMT
    // L'animation sera gérée par la tâche LED

    // La boucle principale se termine, les tâches FreeRTOS continuent
    ESP_LOGI(TAG, "app_main terminé, tâches en cours d'exécution");
}
