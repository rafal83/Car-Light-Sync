#ifndef AUDIO_INPUT_H
#define AUDIO_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#define TAG_AUDIO "Audio"

// Configuration par défaut du micro INMP441
#define AUDIO_I2S_SCK_PIN 12      // Serial Clock
#define AUDIO_I2S_WS_PIN 13       // Word Select (LRCK)
#define AUDIO_I2S_SD_PIN 11        // Serial Data
#define AUDIO_SAMPLE_RATE 44100   // Taux d'échantillonnage
#define AUDIO_BUFFER_SIZE 1024    // Taille du buffer audio
#define AUDIO_DMA_BUFFER_COUNT 2  // Nombre de buffers DMA

// Paramètres de traitement audio
#define AUDIO_FFT_SIZE 512
#define AUDIO_FFT_BANDS 32            // Nombre de bandes FFT (puissance de 2)
#define AUDIO_BPM_MIN 60
#define AUDIO_BPM_MAX 180
#define AUDIO_BPM_HISTORY_SIZE 4

// Plages de fréquences (Hz) pour la classification
#define FREQ_BASS_LOW 20
#define FREQ_BASS_HIGH 250
#define FREQ_MID_LOW 250
#define FREQ_MID_HIGH 2000
#define FREQ_TREBLE_LOW 2000
#define FREQ_TREBLE_HIGH 8000

// Structure pour les données audio analysées (simple)
typedef struct {
    float amplitude;        // Amplitude normalisée (0.0 - 1.0)
    float bass;            // Niveau des basses (0.0 - 1.0)
    float mid;             // Niveau des médiums (0.0 - 1.0)
    float treble;          // Niveau des aigus (0.0 - 1.0)
    float bpm;             // BPM détecté
    bool beat_detected;    // Battement détecté
    uint32_t last_beat_ms; // Timestamp du dernier battement
} audio_data_t;

// Structure pour les données FFT avancées
typedef struct {
    float bands[AUDIO_FFT_BANDS];  // Niveau de chaque bande (0.0 - 1.0)
    float peak_freq;               // Fréquence dominante (Hz)
    float spectral_centroid;       // "Centre de masse" du spectre (Hz)
    uint8_t dominant_band;         // Index de la bande la plus forte
    float bass_energy;             // Énergie totale dans les basses
    float mid_energy;              // Énergie totale dans les médiums
    float treble_energy;           // Énergie totale dans les aigus
    bool kick_detected;            // Kick drum détecté (20-120 Hz)
    bool snare_detected;           // Snare détecté (150-250 Hz)
    bool vocal_detected;           // Voix détectée (500-2000 Hz)
} audio_fft_data_t;

// Configuration du micro
typedef struct {
    bool enabled;           // Micro activé/désactivé
    uint8_t sensitivity;    // Sensibilité (0-255)
    uint8_t gain;          // Gain (0-255)
    bool auto_gain;        // Gain automatique
    bool fft_enabled;      // FFT activée/désactivée
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

/**
 * @brief Obtient les données FFT avancées
 * @param fft_data Pointeur vers les données FFT
 * @return true si données disponibles
 */
bool audio_input_get_fft_data(audio_fft_data_t *fft_data);

/**
 * @brief Active/désactive le mode FFT avancé
 * @param enable true pour activer la FFT
 * @note La FFT consomme plus de CPU (~20%) et RAM (~20KB)
 */
void audio_input_set_fft_enabled(bool enable);

/**
 * @brief Vérifie si le mode FFT est activé
 * @return true si FFT activée
 */
bool audio_input_is_fft_enabled(void);

#endif // AUDIO_INPUT_H
