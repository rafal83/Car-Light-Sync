#ifndef AUDIO_INPUT_H
#define AUDIO_INPUT_H

#include <stdbool.h>
#include <stdint.h>

// Configuration par défaut du micro INMP441
#define AUDIO_I2S_SCK_PIN 10      // Serial Clock
#define AUDIO_I2S_WS_PIN 11       // Word Select (LRCK)
#define AUDIO_I2S_SD_PIN 9        // Serial Data
#define AUDIO_SAMPLE_RATE 44100   // Taux d'échantillonnage
#define AUDIO_BUFFER_SIZE 1024    // Taille du buffer audio
#define AUDIO_DMA_BUFFER_COUNT 2  // Nombre de buffers DMA

// Paramètres de traitement audio
#define AUDIO_FFT_SIZE 512
#define AUDIO_BPM_MIN 60
#define AUDIO_BPM_MAX 180
#define AUDIO_BPM_HISTORY_SIZE 4

// Structure pour les données audio analysées
typedef struct {
    float amplitude;        // Amplitude normalisée (0.0 - 1.0)
    float bass;            // Niveau des basses (0.0 - 1.0)
    float mid;             // Niveau des médiums (0.0 - 1.0)
    float treble;          // Niveau des aigus (0.0 - 1.0)
    float bpm;             // BPM détecté
    bool beat_detected;    // Battement détecté
    uint32_t last_beat_ms; // Timestamp du dernier battement
} audio_data_t;

// Configuration du micro
typedef struct {
    bool enabled;           // Micro activé/désactivé
    uint8_t sensitivity;    // Sensibilité (0-255)
    uint8_t gain;          // Gain (0-255)
    bool auto_gain;        // Gain automatique
    uint8_t i2s_sck_pin;   // GPIO pour SCK
    uint8_t i2s_ws_pin;    // GPIO pour WS
    uint8_t i2s_sd_pin;    // GPIO pour SD
} audio_config_t;

/**
 * @brief Initialise le module audio I2S
 * @return true si succès
 */
bool audio_input_init(void);

/**
 * @brief Désinitialise le module audio
 */
void audio_input_deinit(void);

/**
 * @brief Active ou désactive le micro
 * @param enable true pour activer
 * @return true si succès
 */
bool audio_input_set_enabled(bool enable);

/**
 * @brief Vérifie si le micro est activé
 * @return true si activé
 */
bool audio_input_is_enabled(void);

/**
 * @brief Configure le module audio
 * @param config Configuration
 */
void audio_input_set_config(const audio_config_t *config);

/**
 * @brief Obtient la configuration actuelle
 * @param config Pointeur vers la configuration
 */
void audio_input_get_config(audio_config_t *config);

/**
 * @brief Obtient les dernières données audio analysées
 * @param data Pointeur vers les données
 * @return true si données disponibles
 */
bool audio_input_get_data(audio_data_t *data);

/**
 * @brief Définit la sensibilité du micro
 * @param sensitivity Sensibilité (0-255)
 */
void audio_input_set_sensitivity(uint8_t sensitivity);

/**
 * @brief Définit le gain du micro
 * @param gain Gain (0-255)
 */
void audio_input_set_gain(uint8_t gain);

/**
 * @brief Active/désactive le gain automatique
 * @param enable true pour activer
 */
void audio_input_set_auto_gain(bool enable);

/**
 * @brief Sauvegarde la configuration audio
 * @return true si succès
 */
bool audio_input_save_config(void);

/**
 * @brief Charge la configuration audio
 * @return true si succès
 */
bool audio_input_load_config(void);

/**
 * @brief Réinitialise la configuration par défaut
 */
void audio_input_reset_config(void);

#endif // AUDIO_INPUT_H
