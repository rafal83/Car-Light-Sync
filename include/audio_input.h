#ifndef AUDIO_INPUT_H
#define AUDIO_INPUT_H

#include "config.h"

#include <stdbool.h>
#include <stdint.h>

#define TAG_AUDIO "Audio"

// Default configuration for INMP441 microphone (ESP32-C6)
#define AUDIO_I2S_SCK_PIN I2S_SCK_PIN // Serial Clock
#define AUDIO_I2S_WS_PIN I2S_WS_PIN   // Word Select (LRCK)
#define AUDIO_I2S_SD_PIN I2S_SD_PIN   // Serial Data
#define AUDIO_SAMPLE_RATE 44100       // Sampling rate
#define AUDIO_BUFFER_SIZE 1024        // Audio buffer size
#define AUDIO_DMA_BUFFER_COUNT 2      // Number of DMA buffers

// Audio processing parameters
#define AUDIO_FFT_SIZE 256 // Reduced from 512 for LED animations (sufficient for 16 bands)
#define AUDIO_FFT_BANDS 16 // Number of FFT bands (power of 2)
#define AUDIO_BPM_MIN 60
#define AUDIO_BPM_MAX 180
#define AUDIO_BPM_HISTORY_SIZE 4

// Frequency ranges (Hz) for classification
#define FREQ_BASS_LOW 20
#define FREQ_BASS_HIGH 250
#define FREQ_MID_LOW 250
#define FREQ_MID_HIGH 2000
#define FREQ_TREBLE_LOW 2000
#define FREQ_TREBLE_HIGH 8000

// Structure for analyzed audio data (simple)
typedef struct {
  float amplitude;            // Normalized amplitude (0.0 - 1.0)
  float bass;                 // Bass level (0.0 - 1.0)
  float mid;                  // Mid-range level (0.0 - 1.0)
  float treble;               // Treble level (0.0 - 1.0)
  float bpm;                  // Detected BPM
  bool beat_detected;         // Beat detected
  uint32_t last_beat_ms;      // Timestamp of last beat
  float raw_amplitude;        // Amplitude before calibration/gate
  float calibrated_amplitude; // Amplitude after calibration/gate
  float auto_gain;            // Applied auto-gain multiplier
  float noise_floor;          // Background noise from calibration
  float peak_level;           // Calibration peak
} audio_data_t;

// Structure for advanced FFT data
typedef struct {
  float bands[AUDIO_FFT_BANDS]; // Level of each band (0.0 - 1.0)
  float peak_freq;              // Dominant frequency (Hz)
  float spectral_centroid;      // "Center of mass" of spectrum (Hz)
  uint8_t dominant_band;        // Index of strongest band
  float bass_energy;            // Total energy in bass
  float mid_energy;             // Total energy in mid-range
  float treble_energy;          // Total energy in treble
  bool kick_detected;           // Kick drum detected (20-120 Hz)
  bool snare_detected;          // Snare detected (150-250 Hz)
  bool vocal_detected;          // Voice detected (500-2000 Hz)
} audio_fft_data_t;

// Microphone configuration
typedef struct {
  bool enabled;        // Microphone enabled/disabled
  uint8_t sensitivity; // Sensitivity (0-255)
  uint8_t gain;        // Gain (0-255)
  bool auto_gain;      // Automatic gain
  bool fft_enabled;    // FFT enabled/disabled
} audio_config_t;

// Microphone calibration data (background noise / measured peak)
typedef struct {
  bool calibrated;   // true if calibration is available
  float noise_floor; // RMS level observed in silence
  float peak_level;  // Peak observed during calibration
} audio_calibration_t;

/**
 * @brief Initializes the I2S audio module
 * @return true if successful
 */
bool audio_input_init(void);

/**
 * @brief Deinitializes the audio module
 */
void audio_input_deinit(void);

/**
 * @brief Enables or disables the microphone
 * @param enable true to enable
 * @return true if successful
 */
bool audio_input_set_enabled(bool enable);

/**
 * @brief Checks if the microphone is enabled
 * @return true if enabled
 */
bool audio_input_is_enabled(void);

/**
 * @brief Configure le module audio
 * @param config Configuration
 */
void audio_input_set_config(const audio_config_t *config);

/**
 * @brief Gets the current configuration
 * @param config Pointer to configuration
 */
void audio_input_get_config(audio_config_t *config);

/**
 * @brief Gets the latest analyzed audio data
 * @param data Pointer to data
 * @return true if data available
 */
bool audio_input_get_data(audio_data_t *data);

/**
 * @brief Sets the microphone sensitivity
 * @param sensitivity Sensitivity (0-255)
 */
void audio_input_set_sensitivity(uint8_t sensitivity);

/**
 * @brief Sets the microphone gain
 * @param gain Gain (0-255)
 */
void audio_input_set_gain(uint8_t gain);

/**
 * @brief Enables/disables automatic gain
 * @param enable true to enable
 */
void audio_input_set_auto_gain(bool enable);

/**
 * @brief Saves the audio configuration
 * @return true if successful
 */
bool audio_input_save_config(void);

/**
 * @brief Loads the audio configuration
 * @return true if successful
 */
bool audio_input_load_config(void);

/**
 * @brief Resets to default configuration
 */
void audio_input_reset_config(void);

/**
 * @brief Gets advanced FFT data
 * @param fft_data Pointer to FFT data
 * @return true if data available
 */
bool audio_input_get_fft_data(audio_fft_data_t *fft_data);

/**
 * @brief Enables/disables advanced FFT mode
 * @param enable true to enable FFT
 * @note FFT optimized for LED animations: 256-point FFT @ 25Hz
 *       Consumes ~10% CPU and ~3KB RAM (reduced from 512pt @ 50Hz)
 */
void audio_input_set_fft_enabled(bool enable);

/**
 * @brief Checks if FFT mode is enabled
 * @return true if FFT enabled
 */
bool audio_input_is_fft_enabled(void);

/**
 * @brief Runs microphone calibration (measures background noise + peak)
 * @param duration_ms Sampling duration in milliseconds
 * @param result Calibration result (optional)
 * @return true if successful (calibration saved to NVS)
 */
bool audio_input_run_calibration(uint32_t duration_ms, audio_calibration_t *result);

/**
 * @brief Resets the audio calibration to default uncalibrated state
 * @return true if successful (reset calibration saved to NVS)
 */
bool audio_input_reset_calibration(void);

/**
 * @brief Gets the current calibration state
 * @param calibration Pointer to receive calibration
 */
void audio_input_get_calibration(audio_calibration_t *calibration);

#endif // AUDIO_INPUT_H
