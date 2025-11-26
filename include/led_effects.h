#ifndef LED_EFFECTS_H
#define LED_EFFECTS_H

#include "vehicle_can_unified.h"
#include <stdbool.h>
#include <stdint.h>

// IDs alphanumériques stables pour les effets (ne changent jamais)
#define EFFECT_ID_OFF "OFF"
#define EFFECT_ID_SOLID "SOLID"
#define EFFECT_ID_BREATHING "BREATHING"
#define EFFECT_ID_RAINBOW "RAINBOW"
#define EFFECT_ID_RAINBOW_CYCLE "RAINBOW_CYCLE"
#define EFFECT_ID_THEATER_CHASE "THEATER_CHASE"
#define EFFECT_ID_RUNNING_LIGHTS "RUNNING_LIGHTS"
#define EFFECT_ID_TWINKLE "TWINKLE"
#define EFFECT_ID_FIRE "FIRE"
#define EFFECT_ID_SCAN "SCAN"
#define EFFECT_ID_KNIGHT_RIDER "KNIGHT_RIDER"
#define EFFECT_ID_FADE "FADE"
#define EFFECT_ID_STROBE "STROBE"
#define EFFECT_ID_VEHICLE_SYNC "VEHICLE_SYNC"
#define EFFECT_ID_TURN_SIGNAL "TURN_SIGNAL"
#define EFFECT_ID_BRAKE_LIGHT "BRAKE_LIGHT"
#define EFFECT_ID_CHARGE_STATUS "CHARGE_STATUS"
#define EFFECT_ID_HAZARD "HAZARD"
#define EFFECT_ID_BLINDSPOT_FLASH "BLINDSPOT_FLASH"
#define EFFECT_ID_AUDIO_REACTIVE "AUDIO_REACTIVE"
#define EFFECT_ID_AUDIO_BPM "AUDIO_BPM"
#define EFFECT_ID_FFT_SPECTRUM "FFT_SPECTRUM"
#define EFFECT_ID_FFT_BASS_PULSE "FFT_BASS_PULSE"
#define EFFECT_ID_FFT_VOCAL_WAVE "FFT_VOCAL_WAVE"
#define EFFECT_ID_FFT_ENERGY_BAR "FFT_ENERGY_BAR"

#define EFFECT_ID_MAX_LEN 32 // Longueur max d'un ID d'effet

// Types d'effets (enum interne, peut changer)
typedef enum {
  EFFECT_OFF = 0,
  EFFECT_SOLID,
  EFFECT_BREATHING,
  EFFECT_RAINBOW,
  EFFECT_RAINBOW_CYCLE,
  EFFECT_THEATER_CHASE,
  EFFECT_RUNNING_LIGHTS,
  EFFECT_TWINKLE,
  EFFECT_FIRE,
  EFFECT_SCAN,
  EFFECT_KNIGHT_RIDER, // K2000 - traînée nette sans fade
  EFFECT_FADE,
  EFFECT_STROBE,
  EFFECT_VEHICLE_SYNC,    // Synchronisé avec l'état du véhicule
  EFFECT_TURN_SIGNAL,     // Clignotants
  EFFECT_BRAKE_LIGHT,     // Feux de stop
  EFFECT_CHARGE_STATUS,   // Indicateur de charge
  EFFECT_HAZARD,          // Warnings (les deux côtés)
  EFFECT_BLINDSPOT_FLASH, // Flash directionnel pour angle mort
  EFFECT_AUDIO_REACTIVE,  // Effet réactif au son
  EFFECT_AUDIO_BPM,       // Effet synchronisé au BPM
  EFFECT_FFT_SPECTRUM,    // Spectre FFT en temps réel (égaliseur)
  EFFECT_FFT_BASS_PULSE,  // Pulse sur les basses (kick)
  EFFECT_FFT_VOCAL_WAVE,  // Vague réactive aux voix
  EFFECT_FFT_ENERGY_BAR,  // Barre d'énergie spectrale
  EFFECT_MAX
} led_effect_t;

// Modes de synchronisation véhicule
typedef enum {
  SYNC_OFF = 0,
  SYNC_DOORS,        // Réagit à l'ouverture des portes
  SYNC_SPEED,        // Change selon la vitesse
  SYNC_TURN_SIGNALS, // Suit les clignotants
  SYNC_BRAKE,        // Feux de stop
  SYNC_CHARGE,       // État de charge
  SYNC_LOCKED,       // État verrouillage
  SYNC_ALL           // Tous les événements
} sync_mode_t;

// Zone d'application de l'effet
typedef enum {
  LED_ZONE_FULL = 0, // Tout le strip (0 à NUM_LEDS-1)
  LED_ZONE_LEFT,     // Moitié gauche (0 à NUM_LEDS/2-1)
  LED_ZONE_RIGHT     // Moitié droite (NUM_LEDS/2 à NUM_LEDS-1)
} led_zone_t;

// Configuration d'un effet
typedef struct {
  led_effect_t effect;
  uint8_t brightness;
  uint8_t speed;
  uint32_t color1; // RGB au format 0xRRGGBB
  uint32_t color2;
  uint32_t color3;
  sync_mode_t sync_mode;
  bool reverse;    // Direction de l'animation (pour compatibilité)
  led_zone_t zone; // Zone d'application (full, left, right)
  bool audio_reactive; // L'effet réagit au micro si activé
} effect_config_t;

/**
 * @brief Initialise le système LED
 * @return true si succès
 */
bool led_effects_init(void);

/**
 * @brief Désinitialise le système LED et libère les ressources
 */
void led_effects_deinit(void);

/**
 * @brief Configure un effet
 * @param config Configuration de l'effet
 */
void led_effects_set_config(const effect_config_t *config);

/**
 * @brief Configure deux effets directionnels simultanés (gauche et droite)
 * @param left_config Configuration pour le côté gauche (peut être NULL)
 * @param right_config Configuration pour le côté droit (peut être NULL)
 */
void led_effects_set_dual_directional(const effect_config_t *left_config,
                                      const effect_config_t *right_config);

/**
 * @brief Obtient la configuration actuelle
 * @param config Pointeur vers la structure de configuration
 */
void led_effects_get_config(effect_config_t *config);

/**
 * @brief Met à jour les LEDs (à appeler régulièrement)
 */
void led_effects_update(void);

/**
 * @brief Met à jour avec l'état du véhicule
 * @param state État du véhicule
 */
void led_effects_update_vehicle_state(const vehicle_state_t *state);

/**
 * @brief Active/désactive les LEDs
 * @param enabled true pour activer
 */
void led_effects_set_enabled(bool enabled);

/**
 * @brief Définit la luminosité globale
 * @param brightness Luminosité (0-255)
 */
void led_effects_set_brightness(uint8_t brightness);

/**
 * @brief Définit la vitesse de l'effet
 * @param speed Vitesse (0-255)
 */
void led_effects_set_speed(uint8_t speed);

/**
 * @brief Définit le sens de la strip LED
 * @param reverse true pour inverser, false pour normal
 */
void led_effects_set_reverse(bool reverse);

/**
 * @brief Obtient le sens actuel de la strip LED
 * @return true si inversé, false sinon
 */
bool led_effects_get_reverse(void);

/**
 * @brief Définit une couleur unie
 * @param color Couleur au format 0xRRGGBB
 */
void led_effects_set_solid_color(uint32_t color);

/**
 * @brief Obtient le nom d'un effet
 * @param effect Type d'effet
 * @return Nom de l'effet
 */
const char *led_effects_get_name(led_effect_t effect);

/**
 * @brief Sauvegarde la configuration en mémoire non-volatile
 * @return true si succès
 */
bool led_effects_save_config(void);

/**
 * @brief Charge la configuration depuis la mémoire non-volatile
 * @return true si succès
 */
bool led_effects_load_config(void);

/**
 * @brief Réinitialise la configuration par défaut
 */
void led_effects_reset_config(void);

/**
 * @brief Active/désactive le mode nuit avec luminosité globale
 * @param active true pour activer le mode nuit
 * @param brightness Luminosité du mode nuit (0-255, appliqué comme
 * multiplicateur)
 */
void led_effects_set_night_mode(bool active, uint8_t brightness);

/**
 * @brief Obtient l'état du mode nuit
 * @return true si le mode nuit est actif
 */
bool led_effects_get_night_mode(void);

/**
 * @brief Obtient la luminosité du mode nuit
 * @return Luminosité (0-255)
 */
uint8_t led_effects_get_night_brightness(void);

/**
 * @brief Convertit un enum d'effet en ID alphanumérique
 * @param effect Type d'effet
 * @return ID alphanumérique (constante statique)
 */
const char *led_effects_enum_to_id(led_effect_t effect);

/**
 * @brief Convertit un ID alphanumérique en enum d'effet
 * @param id ID alphanumérique
 * @return Type d'effet (EFFECT_OFF si ID inconnu)
 */
led_effect_t led_effects_id_to_enum(const char *id);

/**
 * @brief Vérifie si un effet nécessite des données CAN pour fonctionner
 * @param effect Type d'effet
 * @return true si l'effet nécessite le CAN bus, false sinon
 */
bool led_effects_requires_can(led_effect_t effect);

/**
 * @brief Active l'affichage de progression OTA sur la strip
 */
void led_effects_start_progress_display(void);

/**
 * @brief Met à jour le pourcentage de progression OTA affiché
 * @param percent Pourcentage 0-100
 */
void led_effects_update_progress(uint8_t percent);

/**
 * @brief Désactive l'affichage de progression OTA
 */
void led_effects_stop_progress_display(void);

/**
 * @brief Affiche un effet indiquant que l'appareil est prêt à redémarrer
 *        après une mise à jour OTA réussie.
 */
void led_effects_show_upgrade_ready(void);

/**
 * @brief Affiche un effet indiquant qu'une mise à jour OTA a échoué
 *        mais que l'appareil redémarrera automatiquement.
 */
void led_effects_show_upgrade_error(void);

/**
 * @brief Applique une nouvelle configuration matérielle LED à chaud
 * @param led_count Nombre de LEDs
 * @param data_pin GPIO pour les données LED
 * @return true si la reconfiguration a réussi
 */
bool led_effects_apply_hardware_config(uint16_t led_count, uint8_t data_pin);

#endif // LED_EFFECTS_H
