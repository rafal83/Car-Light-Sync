#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "led_effects.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TAG_CONFIG "ConfigMgr"
#define MAX_PROFILE_SCAN_LIMIT 1000  // Limite de scan pour éviter une boucle infinie
#define PROFILE_NAME_MAX_LEN 32

// IDs alphanumériques stables pour les événements CAN (ne changent jamais)
#define EVENT_ID_NONE "NONE"
#define EVENT_ID_TURN_LEFT "TURN_LEFT"
#define EVENT_ID_TURN_RIGHT "TURN_RIGHT"
#define EVENT_ID_TURN_HAZARD "TURN_HAZARD"
#define EVENT_ID_CHARGING "CHARGING"
#define EVENT_ID_CHARGE_COMPLETE "CHARGE_COMPLETE"
#define EVENT_ID_CHARGING_STARTED "CHARGING_STARTED"
#define EVENT_ID_CHARGING_STOPPED "CHARGING_STOPPED"
#define EVENT_ID_CHARGING_CABLE_CONNECTED "CHARGING_CABLE_CONNECTED"
#define EVENT_ID_CHARGING_CABLE_DISCONNECTED "CHARGING_CABLE_DISCONNECTED"
#define EVENT_ID_CHARGING_PORT_OPENED "CHARGING_PORT_OPENED"
#define EVENT_ID_DOOR_OPEN "DOOR_OPEN"
#define EVENT_ID_DOOR_CLOSE "DOOR_CLOSE"
#define EVENT_ID_LOCKED "LOCKED"
#define EVENT_ID_UNLOCKED "UNLOCKED"
#define EVENT_ID_BRAKE_ON "BRAKE_ON"
#define EVENT_ID_BLINDSPOT_LEFT_LV1 "BLINDSPOT_LEFT_LV1"
#define EVENT_ID_BLINDSPOT_LEFT_LV2 "BLINDSPOT_LEFT_LV2"
#define EVENT_ID_BLINDSPOT_RIGHT_LV1 "BLINDSPOT_RIGHT_LV1"
#define EVENT_ID_BLINDSPOT_RIGHT_LV2 "BLINDSPOT_RIGHT_LV2"
#define EVENT_ID_FORWARD_COLLISION "FORWARD_COLLISION"
#define EVENT_ID_LANE_DEPARTURE_LEFT_LV1 "LANE_DEPARTURE_LEFT_LV1"
#define EVENT_ID_LANE_DEPARTURE_LEFT_LV2 "LANE_DEPARTURE_LEFT_LV2"
#define EVENT_ID_LANE_DEPARTURE_RIGHT_LV1 "LANE_DEPARTURE_RIGHT_LV1"
#define EVENT_ID_LANE_DEPARTURE_RIGHT_LV2 "LANE_DEPARTURE_RIGHT_LV2"
#define EVENT_ID_SPEED_THRESHOLD "SPEED_THRESHOLD"
#define EVENT_ID_AUTOPILOT_ENGAGED "AUTOPILOT_ENGAGED"
#define EVENT_ID_AUTOPILOT_DISENGAGED "AUTOPILOT_DISENGAGED"
#define EVENT_ID_AUTOPILOT_ABORTING "AUTOPILOT_ABORTING"
#define EVENT_ID_GEAR_DRIVE "GEAR_DRIVE"
#define EVENT_ID_GEAR_REVERSE "GEAR_REVERSE"
#define EVENT_ID_GEAR_PARK "GEAR_PARK"
#define EVENT_ID_SENTRY_MODE_ON "SENTRY_MODE_ON"
#define EVENT_ID_SENTRY_MODE_OFF "SENTRY_MODE_OFF"
#define EVENT_ID_SENTRY_ALERT "SENTRY_ALERT"

// Types d'événements CAN qui peuvent déclencher des effets (enum interne, peut
// changer)
typedef enum {
  CAN_EVENT_NONE = 0,
  CAN_EVENT_TURN_LEFT,
  CAN_EVENT_TURN_RIGHT,
  CAN_EVENT_TURN_HAZARD,
  CAN_EVENT_CHARGING,
  CAN_EVENT_CHARGE_COMPLETE,
  CAN_EVENT_CHARGING_STARTED,
  CAN_EVENT_CHARGING_STOPPED,
  CAN_EVENT_CHARGING_CABLE_CONNECTED,
  CAN_EVENT_CHARGING_CABLE_DISCONNECTED,
  CAN_EVENT_CHARGING_PORT_OPENED,
  CAN_EVENT_DOOR_OPEN,
  CAN_EVENT_DOOR_CLOSE,
  CAN_EVENT_LOCKED,
  CAN_EVENT_UNLOCKED,
  CAN_EVENT_BRAKE_ON,
  CAN_EVENT_BLINDSPOT_LEFT_LV1,
  CAN_EVENT_BLINDSPOT_LEFT_LV2,
  CAN_EVENT_BLINDSPOT_RIGHT_LV1,
  CAN_EVENT_BLINDSPOT_RIGHT_LV2,
  CAN_EVENT_FORWARD_COLLISION,
  CAN_EVENT_LANE_DEPARTURE_LEFT_LV1,
  CAN_EVENT_LANE_DEPARTURE_LEFT_LV2,
  CAN_EVENT_LANE_DEPARTURE_RIGHT_LV1,
  CAN_EVENT_LANE_DEPARTURE_RIGHT_LV2,
  CAN_EVENT_SPEED_THRESHOLD, // Déclenché quand vitesse > seuil
  CAN_EVENT_AUTOPILOT_ENGAGED,
  CAN_EVENT_AUTOPILOT_DISENGAGED,
  CAN_EVENT_AUTOPILOT_ABORTING,
  CAN_EVENT_GEAR_DRIVE,      // Passage en mode Drive
  CAN_EVENT_GEAR_REVERSE,    // Passage en marche arrière
  CAN_EVENT_GEAR_PARK,       // Passage en mode Park
  CAN_EVENT_SENTRY_MODE_ON,  // Mode sentry armé
  CAN_EVENT_SENTRY_MODE_OFF, // Mode sentry désarmé
  CAN_EVENT_SENTRY_ALERT,    // Détection/alarme sentry
  CAN_EVENT_MAX
} can_event_type_t;

// Type d'action pour un événement CAN
typedef enum {
  EVENT_ACTION_APPLY_EFFECT = 0, // Applique un effet LED
  EVENT_ACTION_SWITCH_PROFILE    // Change de profil
} event_action_type_t;

/**
 * @brief Configuration d'un effet pour un événement CAN spécifique
 *
 * Exemple d'usage:
 * @code
 * // Configurer clignotant gauche sur événement CAN
 * effect_config_t turn_config = {
 *   .effect = EFFECT_TURN_SIGNAL,
 *   .brightness = 255,
 *   .speed = 80,
 *   .color1 = 0xFF8000,  // Orange
 *   .reverse = true,
 *   .segment_start = 0,
 *   .segment_length = 61
 * };
 *
 * config_manager_set_event_effect(
 *   0,                         // profile_id
 *   CAN_EVENT_TURN_LEFT,      // event
 *   &turn_config,              // effect_config
 *   500,                       // duration_ms (500ms)
 *   200                        // priority (haute priorité)
 * );
 * config_manager_set_event_enabled(0, CAN_EVENT_TURN_LEFT, true);
 * @endcode
 */
typedef struct {
  can_event_type_t event;
  event_action_type_t action_type; // Type d'action à effectuer
  effect_config_t effect_config;
  uint16_t duration_ms; // Durée de l'effet (0 = permanent jusqu'à nouvel événement)
  uint8_t priority;     // Priorité (0-255, plus haut = prioritaire)
  int8_t profile_id;    // ID du profil à activer (-1 = aucun)
  bool enabled;         // Actif ou non
} can_event_effect_t;

// Profil de configuration complet
typedef struct {
  char name[PROFILE_NAME_MAX_LEN];
  effect_config_t default_effect; // Effet par défaut

  can_event_effect_t event_effects[CAN_EVENT_MAX]; // Effets par événement

  // Paramètres généraux - Luminosité dynamique
  bool dynamic_brightness_enabled; // Active la luminosité dynamique liée au brightness de la voiture
  uint8_t dynamic_brightness_rate; // Taux d'application du brightness voiture (0-100%)

  // Métadonnées
  bool active; // Profil actif
  uint32_t created_timestamp;
  uint32_t modified_timestamp;
} config_profile_t;

/**
 * @brief Initialise le gestionnaire de configuration
 * @return true si succès
 */
bool config_manager_init(void);

/**
 * @brief Sauvegarde un profil en NVS
 * @param profile_id ID du profil (0-999)
 * @param profile Profil à sauvegarder
 * @return true si succès
 */
bool config_manager_save_profile(uint16_t profile_id, const config_profile_t *profile);

/**
 * @brief Charge un profil depuis NVS
 * @param profile_id ID du profil
 * @param profile Pointeur vers le profil
 * @return true si succès
 */
bool config_manager_load_profile(uint16_t profile_id, config_profile_t *profile);

/**
 * @brief Supprime un profil
 * @param profile_id ID du profil
 * @return true si succès
 */
bool config_manager_delete_profile(uint16_t profile_id);

/**
 * @brief Active un profil
 * @param profile_id ID du profil
 * @return true si succès
 */
bool config_manager_activate_profile(uint16_t profile_id);

/**
 * @brief Renomme un profil
 * @param profile_id ID du profil
 * @param new_name Nouveau nom du profil
 * @return true si succès
 */
bool config_manager_rename_profile(uint16_t profile_id, const char *new_name);

/**
 * @brief Obtient le profil actif
 * @param profile Pointeur vers le profil
 * @return true si un profil est actif
 */
bool config_manager_get_active_profile(config_profile_t *profile);

/**
 * @brief Obtient l'ID du profil actif
 * @return ID du profil actif (-1 si aucun)
 */
int config_manager_get_active_profile_id(void);

/**
 * @brief Liste tous les profils disponibles
 * @param profiles Array pour stocker les profils
 * @param max_profiles Taille max du array
 * @return Nombre de profils trouvés
 */
int config_manager_list_profiles(config_profile_t *profiles, int max_profiles);

/**
 * @brief Crée un profil par défaut
 * @param profile Pointeur vers le profil
 * @param name Nom du profil
 */
void config_manager_create_default_profile(config_profile_t *profile, const char *name);

/**
 * @brief Associe un effet à un événement CAN
 * @param profile_id ID du profil
 * @param event Type d'événement
 * @param effect_config Configuration de l'effet
 * @param duration_ms Durée de l'effet
 * @param priority Priorité
 * @return true si succès
 */
bool config_manager_set_event_effect(uint16_t profile_id, can_event_type_t event, const effect_config_t *effect_config, uint16_t duration_ms, uint8_t priority);

/**
 * @brief Active ou désactive un événement
 * @param profile_id ID du profil
 * @param event Type d'événement
 * @param enabled true pour activer, false pour désactiver
 * @return true si succès
 */
bool config_manager_set_event_enabled(uint16_t profile_id, can_event_type_t event, bool enabled);

/**
 * @brief Traite un événement CAN et applique l'effet correspondant
 * @param event Type d'événement
 * @return true si un effet a été appliqué
 */
bool config_manager_process_can_event(can_event_type_t event);

/**
 * @brief Arrête manuellement un événement actif
 * @param event Type d'événement à arrêter
 */
void config_manager_stop_event(can_event_type_t event);
void config_manager_stop_all_events(void);

/**
 * @brief Met à jour les effets en fonction du temps
 * Gère les effets temporaires et retourne à l'effet par défaut
 */
void config_manager_update(void);

/**
 * @brief Indique si des événements actifs overrident l'effet par défaut
 * @return true si des événements sont actifs
 */
bool config_manager_has_active_events(void);

/**
 * @brief Exporte un profil en JSON
 * @param profile_id ID du profil
 * @param json_buffer Buffer pour le JSON
 * @param buffer_size Taille du buffer
 * @return true si succès
 */
bool config_manager_export_profile(uint16_t profile_id, char *json_buffer, size_t buffer_size);

/**
 * @brief Importe un profil depuis JSON
 * @param profile_id ID du profil
 * @param json_string JSON du profil
 * @return true si succès
 */
bool config_manager_import_profile(uint16_t profile_id, const char *json_string);

/**
 * @brief Obtient la configuration d'effet pour un événement spécifique
 * @param event Type d'événement
 * @param event_effect Pointeur vers la structure d'effet
 * @return true si succès (profil actif existe)
 */
bool config_manager_get_effect_for_event(can_event_type_t event, can_event_effect_t *event_effect);

/**
 * @brief Obtient le nombre de LEDs configuré
 * @return Nombre de LEDs
 */
uint16_t config_manager_get_led_count(void);

/**
 * @brief Définit le nombre de LEDs
 * @param led_count Nombre de LEDs (1-200)
 * @return true si succès et sauvegardé en NVS
 */
bool config_manager_set_led_count(uint16_t led_count);

/**
 * @brief Convertit un enum d'événement en ID alphanumérique
 * @param event Type d'événement
 * @return ID alphanumérique (constante statique)
 */
const char *config_manager_enum_to_id(can_event_type_t event);

/**
 * @brief Convertit un ID alphanumérique en enum d'événement
 * @param id ID alphanumérique
 * @return Type d'événement (CAN_EVENT_NONE si ID inconnu)
 */
can_event_type_t config_manager_id_to_enum(const char *id);

/**
 * @brief Vérifie si un événement peut déclencher un changement de profil
 * @param event Type d'événement
 * @return true si l'événement peut changer de profil
 */
bool config_manager_event_can_switch_profile(can_event_type_t event);

/**
 * @brief Réinitialise tous les paramètres aux valeurs d'usine
 * Supprime tous les profils et crée un profil par défaut
 * @return true si succès
 */
bool config_manager_factory_reset(void);

/**
 * @brief Réapplique l'effet par défaut du profil actif
 * Utile après l'initialisation du module audio pour activer les effets audio
 */
void config_manager_reapply_default_effect(void);

/**
 * @brief Vérifie si l'espace NVS permet de créer un nouveau profil
 * @return true si suffisamment d'espace disponible
 */
bool config_manager_can_create_profile(void);

/**
 * @brief Importe un profil depuis une chaîne JSON dans une structure config_profile_t
 * @param json_string String JSON du preset
 * @param profile Pointeur vers la structure à remplir
 * @return true si succès, false sinon
 */
bool config_manager_import_profile_from_json(const char *json_string, config_profile_t *profile);

#endif // CONFIG_MANAGER_H
