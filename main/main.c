#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "config.h"
#include "wifi_manager.h"
#include "wifi_credentials.h"  // Configuration WiFi optionnelle
#include "commander.h"
#include "led_effects.h"
#include "web_server.h"
#include "tesla_can.h"
#include "config_manager.h"
#include "ota_update.h"
#include "version_info.h"

#ifdef CONFIG_HAS_PSRAM
#include "cJSON.h"
#include "esp_heap_caps.h"
#endif

static const char *TAG = "Main";

static vehicle_state_t last_vehicle_state = {0};


// Callback pour les frames CAN
static void vehicle_can_callback(const can_frame_t* frame, void* user_data) {
    process_can_frame(frame, &last_vehicle_state);
    led_effects_update_vehicle_state(&last_vehicle_state);
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
    
    vehicle_state_t current_state;
    vehicle_state_t previous_state = {0};
    
    while (1) {
        // Copier l'état actuel
        memcpy(&current_state, &last_vehicle_state, sizeof(vehicle_state_t));
        
        // Détecter les changements d'état et générer des événements
        
        // Clignotants
        if (current_state.turn_signal != previous_state.turn_signal) {
            if (current_state.turn_signal == 1) {
                config_manager_process_can_event(CAN_EVENT_TURN_LEFT);
            } else if (current_state.turn_signal == 2) {
                config_manager_process_can_event(CAN_EVENT_TURN_RIGHT);
            } else if (current_state.turn_signal == 3) {
                config_manager_process_can_event(CAN_EVENT_TURN_HAZARD);
            } else if (current_state.turn_signal == 0) {
                // Clignotants désactivés - retour à l'effet par défaut
                config_profile_t profile;
                if (config_manager_get_active_profile(&profile)) {
                    led_effects_set_config(&profile.default_effect);
                    ESP_LOGI(TAG, "Clignotants désactivés, retour à l'effet par défaut");
                }
            }
        }
        
        // Charge
        if (current_state.charging != previous_state.charging) {
            if (current_state.charging) {
                config_manager_process_can_event(CAN_EVENT_CHARGING);
            } else if (current_state.charge_percent >= 80) {
                config_manager_process_can_event(CAN_EVENT_CHARGE_COMPLETE);
            }
        }
        
        // Portes
        bool doors_open_now = current_state.door_fl || current_state.door_fr || 
                             current_state.door_rl || current_state.door_rr;
        bool doors_open_before = previous_state.door_fl || previous_state.door_fr || 
                                previous_state.door_rl || previous_state.door_rr;
        
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
                // Angle mort gauche désactivé - retour à l'effet par défaut
                config_profile_t profile;
                if (config_manager_get_active_profile(&profile)) {
                    led_effects_set_config(&profile.default_effect);
                    ESP_LOGI(TAG, "Angle mort gauche désactivé, retour à l'effet par défaut");
                }
            }
        }
        if (current_state.blindspot_right != previous_state.blindspot_right) {
            if (current_state.blindspot_right) {
                config_manager_process_can_event(CAN_EVENT_BLINDSPOT_RIGHT);
            } else {
                // Angle mort droit désactivé - retour à l'effet par défaut
                config_profile_t profile;
                if (config_manager_get_active_profile(&profile)) {
                    led_effects_set_config(&profile.default_effect);
                    ESP_LOGI(TAG, "Angle mort droit désactivé, retour à l'effet par défaut");
                }
            }
        }
        if (current_state.blindspot_warning != previous_state.blindspot_warning) {
            if (current_state.blindspot_warning) {
                config_manager_process_can_event(CAN_EVENT_BLINDSPOT_WARNING);
            } else {
                // Angle mort warning - retour à l'effet par défaut
                config_profile_t profile;
                if (config_manager_get_active_profile(&profile)) {
                    led_effects_set_config(&profile.default_effect);
                    ESP_LOGI(TAG, "Angle mort warning désactivé, retour à l'effet par défaut");
                }
            }
        }
        
        // Mode nuit
        if (current_state.night_mode != previous_state.night_mode) {
            if (current_state.night_mode) {
                config_manager_process_can_event(CAN_EVENT_NIGHT_MODE_ON);
                
                // Appliquer automatiquement l'effet mode nuit si configuré
                config_profile_t profile;
                if (config_manager_get_active_profile(&profile) && profile.auto_night_mode) {
                    led_effects_set_config(&profile.night_mode_effect);
                    ESP_LOGI(TAG, "Mode nuit activé automatiquement");
                }
            } else {
                config_manager_process_can_event(CAN_EVENT_NIGHT_MODE_OFF);
                
                // Retour à l'effet par défaut
                config_profile_t profile;
                if (config_manager_get_active_profile(&profile) && profile.auto_night_mode) {
                    led_effects_set_config(&profile.default_effect);
                    ESP_LOGI(TAG, "Mode nuit désactivé");
                }
            }
        }
        
        // Seuil de vitesse
        config_profile_t profile;
        if (config_manager_get_active_profile(&profile)) {
            bool above_threshold_now = current_state.speed_kmh > profile.speed_threshold;
            bool above_threshold_before = previous_state.speed_kmh > profile.speed_threshold;
            
            if (above_threshold_now && !above_threshold_before) {
                config_manager_process_can_event(CAN_EVENT_SPEED_THRESHOLD);
            }
        }
        
        // Sauvegarder l'état précédent
        memcpy(&previous_state, &current_state, sizeof(vehicle_state_t));
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Vérifier toutes les 100ms
    }
}

// Tâche de heartbeat Commander
static void heartbeat_task(void *pvParameters) {
    ESP_LOGI(TAG, "Tâche heartbeat démarrée");
    
    while (1) {
        if (commander_is_connected()) {
            commander_send_heartbeat();
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Toutes les secondes
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
            
            commander_status_t cmd_status;
            commander_get_status(&cmd_status);
            
            ESP_LOGI(TAG, "=== Statut ===");
            ESP_LOGI(TAG, "WiFi AP: %s (IP: %s, Clients: %d)", 
                    wifi_status.ap_started ? "Actif" : "Inactif",
                    wifi_status.ap_ip,
                    wifi_status.connected_clients);
            
            if (wifi_status.sta_connected) {
                ESP_LOGI(TAG, "WiFi STA: Connecté à %s (IP: %s)", 
                        wifi_status.sta_ssid, wifi_status.sta_ip);
            }
            
            if (cmd_status.connected) {
                ESP_LOGI(TAG, "Commander: Connecté à %s", cmd_status.ip_address);
                ESP_LOGI(TAG, "  Messages RX: %lu, TX: %lu, Erreurs: %lu",
                        cmd_status.messages_received,
                        cmd_status.messages_sent,
                        cmd_status.errors);
            } else {
                ESP_LOGI(TAG, "Commander: Déconnecté");
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
#ifdef CONFIG_HAS_PSRAM
    // Configurer cJSON pour utiliser la PSRAM pour les allocations
    cJSON_Hooks hooks = {
        .malloc_fn = psram_malloc,
        .free_fn = psram_free
    };
    cJSON_InitHooks(&hooks);
#endif

    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "    Tesla Strip Controller      ");
    ESP_LOGI(TAG, "       Version %s            ", APP_VERSION_STRING);
    ESP_LOGI(TAG, "=================================");
    
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
    
    // WiFi
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(wifi_manager_start_ap());

#ifdef WIFI_AUTO_CONNECT
    // Connexion automatique au WiFi domestique si configuré
    ESP_LOGI(TAG, "Tentative de connexion à %s...", WIFI_HOME_SSID);
    wifi_manager_connect_sta(WIFI_HOME_SSID, WIFI_HOME_PASSWORD);
    vTaskDelay(pdMS_TO_TICKS(5000)); // Attendre 5s pour la connexion
#endif

    ESP_LOGI(TAG, "✓ WiFi initialisé");
    
    // Commander
    ESP_ERROR_CHECK(commander_init());
    commander_register_callback(vehicle_can_callback, NULL);
    ESP_LOGI(TAG, "✓ Commander initialisé");
    
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
    
    // Créer les tâches
    xTaskCreate(led_task, "led_task", 4096, NULL, 5, NULL);
    xTaskCreate(can_event_task, "can_event_task", 4096, NULL, 4, NULL);
    xTaskCreate(heartbeat_task, "heartbeat_task", 2048, NULL, 3, NULL);
    xTaskCreate(monitor_task, "monitor_task", 4096, NULL, 2, NULL);
    
    ESP_LOGI(TAG, "Système démarré avec succès !");
    
    // Animation de démarrage
    effect_config_t startup_effect;
    led_effects_get_config(&startup_effect);
    
    // Flash rapide pour indiquer le démarrage
    for (int i = 0; i < 3; i++) {
        led_effects_set_solid_color(0x00FF00); // Vert
        led_effects_update();
        vTaskDelay(pdMS_TO_TICKS(100));
        
        led_effects_set_solid_color(0x000000); // Noir
        led_effects_update();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Restaurer la configuration
    led_effects_set_config(&startup_effect);
    
    // La boucle principale se termine, les tâches FreeRTOS continuent
    ESP_LOGI(TAG, "app_main terminé, tâches en cours d'exécution");
}
