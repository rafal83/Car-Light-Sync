#include "audio_input.h"

#include "config.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spiffs_storage.h"

#include <float.h>
#include <math.h>
#include <string.h>

// Module state
static bool initialized                  = false;
static bool enabled                      = false;
// Note: fft_enabled now lives in current_config.fft_enabled
static i2s_chan_handle_t rx_handle       = NULL;
static TaskHandle_t audio_task_handle    = NULL;

// Configuration
static audio_config_t current_config     = {.enabled = false, .sensitivity = 128, .gain = 128, .auto_gain = true, .fft_enabled = false};
static audio_calibration_t calibration   = {.calibrated = false, .noise_floor = 0.0f, .peak_level = 1.0f};
// Pre-computed reciprocals for performance (avoid division in hot path)
static float sensitivity_reciprocal = 1.0f / 128.0f;
static float gain_reciprocal = 1.0f / 128.0f;
static audio_calibration_t calib_result  = {0};
static volatile bool calibration_running = false;
static volatile bool calibration_ready   = false;
static TickType_t calibration_end_tick   = 0;
static float calibration_min             = 0.0f;
static float calibration_max             = 0.0f;
static const float CALIB_MIN_RANGE       = 0.20f; // Minimum noise-to-peak gap to keep headroom
static const float NOISE_GATE_MARGIN     = 0.01f; // Margin above noise before reacting

// Simple auto-gain: keeps normalized amplitude around a target
static float auto_gain_multiplier        = 1.0f;
static const float AUTO_GAIN_TARGET      = 0.4f;
static const float AUTO_GAIN_ALPHA       = 0.02f;
static const float AUTO_GAIN_MIN         = 0.5f;
static const float AUTO_GAIN_MAX         = 5.0f;
// Audio data
static audio_data_t current_audio_data   = {0};
static audio_fft_data_t current_fft_data = {0};
static bool audio_data_ready             = false;
static bool fft_data_ready               = false;

// Buffers
static int32_t audio_buffer[AUDIO_BUFFER_SIZE];
static float fft_output[AUDIO_FFT_SIZE]; // To store FFT magnitudes

// Variables for BPM detection
static float bpm_history[AUDIO_BPM_HISTORY_SIZE] = {0};
static uint8_t bpm_history_index                 = 0;
static uint32_t last_beat_time_ms                = 0;
static float energy_history[8]                   = {0};
static uint8_t energy_index                      = 0;

// Forward declarations for FFT functions
static void compute_fft_magnitude(int32_t *audio_samples, int num_samples);
static void group_fft_into_bands(audio_fft_data_t *fft_data);
// Calibration helpers
static void calibration_reset_state(uint32_t duration_ms);
static void calibration_process_sample(float amplitude);
static float apply_calibration(float amplitude);
static bool audio_input_save_calibration(void);
static bool audio_input_load_calibration(void);

// Function to compute RMS amplitude
static float calculate_rms(int32_t *buffer, size_t size) {
  float sum = 0.0f;
  for (size_t i = 0; i < size; i++) {
    float sample = (float)buffer[i] / 2147483648.0f; // Normalize 32-bit
    sum += sample * sample;
  }
  return sqrtf(sum / size);
}

// Simplified function to compute energy per frequency band
static void calculate_frequency_bands(int32_t *buffer, size_t size, float *bass, float *mid, float *treble) {
  // Compute energy across different time ranges
  float low_energy    = 0.0f;
  float mid_energy    = 0.0f;
  float high_energy   = 0.0f;

  // Divide buffer into segments and analyze variations
  size_t segment_size = size / 8;

  for (size_t seg = 0; seg < 8; seg++) {
    float seg_energy = 0.0f;
    for (size_t i = 0; i < segment_size; i++) {
      size_t idx = seg * segment_size + i;
      if (idx < size) {
        float sample = (float)buffer[idx] / 2147483648.0f;
        seg_energy += fabsf(sample);
      }
    }

  // Distribute energy by segment (frequency approximation)
    if (seg < 2) {
      low_energy += seg_energy;
    } else if (seg < 5) {
      mid_energy += seg_energy;
    } else {
      high_energy += seg_energy;
    }
  }

  // Normalize
  float total = low_energy + mid_energy + high_energy;
  if (total > 0.0f) {
    *bass   = low_energy / total;
    *mid    = mid_energy / total;
    *treble = high_energy / total;
  } else {
    *bass   = 0.0f;
    *mid    = 0.0f;
    *treble = 0.0f;
  }
}

// Reset calibration state and start a new window
static void calibration_reset_state(uint32_t duration_ms) {
  calibration_running  = true;
  calibration_ready    = false;
  calibration_min      = FLT_MAX;
  calibration_max      = 0.0f;
  calib_result         = (audio_calibration_t){0};
  calibration_end_tick = xTaskGetTickCount() + pdMS_TO_TICKS(duration_ms > 0 ? duration_ms : 3000);
}

// Capture min/max amplitude during calibration and finalize when the window ends
static void calibration_process_sample(float amplitude) {
  if (!calibration_running) {
    return;
  }

  if (amplitude < calibration_min) {
    calibration_min = amplitude;
  }
  if (amplitude > calibration_max) {
    calibration_max = amplitude;
  }

  if (xTaskGetTickCount() >= calibration_end_tick) {
    calibration_running      = false;
    calibration_ready        = true;
    calib_result.calibrated  = true;
    calib_result.noise_floor = (calibration_min == FLT_MAX) ? 0.0f : calibration_min;
    calib_result.peak_level  = calibration_max;

    // Ensure minimum range for normalization
    if (calib_result.peak_level < calib_result.noise_floor + CALIB_MIN_RANGE) {
      calib_result.peak_level = calib_result.noise_floor + CALIB_MIN_RANGE;
    }
  }
}

// Apply calibration (noise floor removal and peak normalization)
static float apply_calibration(float amplitude) {
  if (!calibration.calibrated) {
    return amplitude;
  }

  float range = calibration.peak_level - calibration.noise_floor;
  if (range < CALIB_MIN_RANGE) {
    range = CALIB_MIN_RANGE;
  }

  // Apply soft gate to filter micro-variations around noise floor
  float gate     = calibration.noise_floor + NOISE_GATE_MARGIN;
  float adjusted = amplitude - gate;
  if (adjusted < 0.0f) {
    adjusted = 0.0f;
  }

  float normalized = adjusted / range;
  if (normalized > 1.0f) {
    normalized = 1.0f;
  }

  return normalized;
}

// Simple beat detection based on energy
static bool detect_beat(float amplitude) {
  // Compute current energy
  float current_energy = amplitude * amplitude;

  // Compute average energy over history
  float avg_energy     = 0.0f;
  for (int i = 0; i < 8; i++) {
    avg_energy += energy_history[i];
  }
  avg_energy /= 8.0f;

  // Store current energy
  energy_history[energy_index] = current_energy;
  energy_index                 = (energy_index + 1) % 8;

  // Peak detection: current energy > 1.5x average
  bool beat                    = (current_energy > avg_energy * 1.5f) && (avg_energy > 0.01f);

  return beat;
}

// BPM calculation based on interval between beats
static float calculate_bpm(uint32_t current_time_ms) {
  if (last_beat_time_ms == 0) {
    last_beat_time_ms = current_time_ms;
    return 0.0f;
  }

  uint32_t interval_ms = current_time_ms - last_beat_time_ms;
  last_beat_time_ms    = current_time_ms;

  // Ignore intervals that are too short or too long
  if (interval_ms < 333 || interval_ms > 1000) { // 60-180 BPM
    return current_audio_data.bpm;               // Keep old value
  }

  // Calculate raw BPM
  float raw_bpm                  = 60000.0f / (float)interval_ms;

  // Smooth with history
  bpm_history[bpm_history_index] = raw_bpm;
  bpm_history_index              = (bpm_history_index + 1) % AUDIO_BPM_HISTORY_SIZE;

  float avg_bpm                  = 0.0f;
  int count                      = 0;
  for (int i = 0; i < AUDIO_BPM_HISTORY_SIZE; i++) {
    if (bpm_history[i] > 0.0f) {
      avg_bpm += bpm_history[i];
      count++;
    }
  }

  if (count > 0) {
    avg_bpm /= count;
  }

  return avg_bpm;
}

// Audio processing task
static void audio_task(void *pvParameters) {
  ESP_LOGI(TAG_AUDIO, "Audio task started");

  size_t bytes_read = 0;

  while (1) {
    if (!enabled || rx_handle == NULL) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // Read I2S data
    esp_err_t ret = i2s_channel_read(rx_handle, audio_buffer, sizeof(audio_buffer), &bytes_read, pdMS_TO_TICKS(100));

    if (ret != ESP_OK || bytes_read == 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    size_t samples           = bytes_read / sizeof(int32_t);

    // Compute RMS amplitude
    float rms                = calculate_rms(audio_buffer, samples);

    // Apply sensitivity and gain (use pre-computed reciprocals for speed)
    float amplitude_raw      = rms * ((float)current_config.sensitivity * sensitivity_reciprocal) *
                                     ((float)current_config.gain * gain_reciprocal) * 10.0f;

    // Clamp to 0.0-1.0
    if (amplitude_raw > 1.0f)
      amplitude_raw = 1.0f;

    // Collect min/max bounds during calibration
    calibration_process_sample(amplitude_raw);

    // Apply calibration (remove noise floor)
    float amplitude_calibrated = apply_calibration(amplitude_raw);
    float amplitude            = amplitude_calibrated;

    // Optional auto-gain to boost weak signal
    if (current_config.auto_gain) {
      float error = AUTO_GAIN_TARGET - amplitude;
      auto_gain_multiplier += error * AUTO_GAIN_ALPHA;
      if (auto_gain_multiplier < AUTO_GAIN_MIN)
        auto_gain_multiplier = AUTO_GAIN_MIN;
      if (auto_gain_multiplier > AUTO_GAIN_MAX)
        auto_gain_multiplier = AUTO_GAIN_MAX;
      amplitude *= auto_gain_multiplier;
      if (amplitude > 1.0f)
        amplitude = 1.0f;
    } else {
      auto_gain_multiplier = 1.0f;
    }

    // Compute frequency bands
    float bass, mid, treble;
    calculate_frequency_bands(audio_buffer, samples, &bass, &mid, &treble);

    // Beat detection
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool beat             = detect_beat(amplitude);

    float bpm             = current_audio_data.bpm;
    if (beat) {
      bpm                             = calculate_bpm(current_time);
      current_audio_data.last_beat_ms = current_time;
    }

    // Update data
    current_audio_data.amplitude            = amplitude;
    current_audio_data.raw_amplitude        = amplitude_raw;
    current_audio_data.calibrated_amplitude = amplitude_calibrated;
    current_audio_data.auto_gain            = auto_gain_multiplier;
    current_audio_data.noise_floor          = calibration.calibrated ? calibration.noise_floor : 0.0f;
    current_audio_data.peak_level           = calibration.calibrated ? calibration.peak_level : 0.0f;
    current_audio_data.bass                 = bass * amplitude;
    current_audio_data.mid                  = mid * amplitude;
    current_audio_data.treble               = treble * amplitude;
    current_audio_data.bpm                  = bpm;
    current_audio_data.beat_detected        = beat;

    audio_data_ready                        = true;

    // If FFT enabled, compute the FFT spectrum
    if (current_config.fft_enabled) {
      compute_fft_magnitude(audio_buffer, samples);
      group_fft_into_bands(&current_fft_data);
      fft_data_ready = true;
    }

    // Small delay to avoid saturating the CPU
    vTaskDelay(pdMS_TO_TICKS(20)); // ~50Hz update
  }
}

bool audio_input_init(void) {
  if (initialized) {
    ESP_LOGW(TAG_AUDIO, "Audio module already initialized");
    return true;
  }

  // Load saved configuration
  audio_input_load_config();
  audio_input_load_calibration();

  // Create the I2S RX channel
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num      = AUDIO_DMA_BUFFER_COUNT;
  chan_cfg.dma_frame_num     = AUDIO_BUFFER_SIZE / 2;

  esp_err_t ret              = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_AUDIO, "Error creating I2S channel: %s", esp_err_to_name(ret));
    return false;
  }

  // Standard I2S configuration for INMP441
  i2s_std_config_t std_cfg = {
      .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = AUDIO_I2S_SCK_PIN,
              .ws   = AUDIO_I2S_WS_PIN,
              .dout = I2S_GPIO_UNUSED,
              .din  = AUDIO_I2S_SD_PIN,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv   = false,
                  },
          },
  };

  ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_AUDIO, "Standard I2S mode init error: %s", esp_err_to_name(ret));
    i2s_del_channel(rx_handle);
    rx_handle = NULL;
    return false;
  }

  // Create the audio processing task
  BaseType_t task_ret = xTaskCreate(audio_task, "audio_task", 4096, NULL, 5, &audio_task_handle);
  if (task_ret != pdPASS) {
    ESP_LOGE(TAG_AUDIO, "Error creating audio task");
    i2s_del_channel(rx_handle);
    rx_handle = NULL;
    return false;
  }

  initialized = true;
  ESP_LOGI(TAG_AUDIO, "Audio module initialized (GPIO: SCK=%d, WS=%d, SD=%d)", AUDIO_I2S_SCK_PIN, AUDIO_I2S_WS_PIN, AUDIO_I2S_SD_PIN);

  // Enable if configured
  if (current_config.enabled) {
    audio_input_set_enabled(true);
  }

  return true;
}

void audio_input_deinit(void) {
  if (!initialized) {
    return;
  }

  audio_input_set_enabled(false);

  if (audio_task_handle != NULL) {
    vTaskDelete(audio_task_handle);
    audio_task_handle = NULL;
  }

  if (rx_handle != NULL) {
    i2s_del_channel(rx_handle);
    rx_handle = NULL;
  }

  initialized = false;
  ESP_LOGI(TAG_AUDIO, "Audio module deinitialized");
}

bool audio_input_set_enabled(bool enable) {
  if (!initialized) {
    ESP_LOGE(TAG_AUDIO, "Audio module not initialized");
    return false;
  }

  if (enable == enabled) {
    return true;
  }

  if (enable) {
    esp_err_t ret = i2s_channel_enable(rx_handle);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG_AUDIO, "I2S channel activation error: %s", esp_err_to_name(ret));
      return false;
    }
    enabled                = true;
    current_config.enabled = true;
    ESP_LOGI(TAG_AUDIO, "Microphone enabled");
  } else {
    esp_err_t ret = i2s_channel_disable(rx_handle);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG_AUDIO, "Error disabling I2S channel: %s", esp_err_to_name(ret));
      return false;
    }
    enabled                = false;
    current_config.enabled = false;
    audio_data_ready       = false;
    memset(&current_audio_data, 0, sizeof(audio_data_t));
    ESP_LOGI(TAG_AUDIO, "Microphone disabled");
  }

  return true;
}

bool audio_input_is_enabled(void) {
  return enabled;
}

void audio_input_set_config(const audio_config_t *config) {
  if (config != NULL) {
    memcpy(&current_config, config, sizeof(audio_config_t));
  }
}

void audio_input_get_config(audio_config_t *config) {
  if (config != NULL) {
    memcpy(config, &current_config, sizeof(audio_config_t));
  }
}

bool audio_input_get_data(audio_data_t *data) {
  if (data == NULL || !audio_data_ready) {
    return false;
  }

  memcpy(data, &current_audio_data, sizeof(audio_data_t));
  return true;
}

void audio_input_set_sensitivity(uint8_t sensitivity) {
  current_config.sensitivity = sensitivity;
  // Pre-compute reciprocal is constant, no need to update
  ESP_LOGI(TAG_AUDIO, "Sensitivity: %d", sensitivity);
}

void audio_input_set_gain(uint8_t gain) {
  current_config.gain = gain;
  // Pre-compute reciprocal is constant, no need to update
  ESP_LOGI(TAG_AUDIO, "Gain: %d", gain);
}

void audio_input_set_auto_gain(bool enable) {
  current_config.auto_gain = enable;
  ESP_LOGI(TAG_AUDIO, "Gain auto: %s", enable ? "ON" : "OFF");
}

bool audio_input_save_config(void) {
  esp_err_t ret = spiffs_save_blob("/spiffs/audio/config.bin", &current_config, sizeof(audio_config_t));

  if (ret == ESP_OK) {
    ESP_LOGI(TAG_AUDIO, "Configuration saved");
    return true;
  }

  return false;
}

static bool audio_input_save_calibration(void) {
  esp_err_t ret = spiffs_save_blob("/spiffs/audio/calibration.bin", &calibration, sizeof(audio_calibration_t));

  if (ret == ESP_OK) {
    ESP_LOGI(TAG_AUDIO, "Calibration saved (noise=%.3f, peak=%.3f)", calibration.noise_floor, calibration.peak_level);
    return true;
  }

  return false;
}

bool audio_input_load_config(void) {
  size_t required_size = sizeof(audio_config_t);
  esp_err_t ret        = spiffs_load_blob("/spiffs/audio/config.bin", &current_config, &required_size);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG_AUDIO, "Configuration loaded");
    return true;
  }

  if (ret == ESP_ERR_NOT_FOUND) {
    ESP_LOGW(TAG_AUDIO, "No saved audio configuration");
  }
  return false;
}

static bool audio_input_load_calibration(void) {
  size_t required_size = sizeof(audio_calibration_t);
  esp_err_t ret        = spiffs_load_blob("/spiffs/audio/calibration.bin", &calibration, &required_size);

  if (ret != ESP_OK) {
    if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGW(TAG_AUDIO, "No saved calibration");
    } else {
      ESP_LOGW(TAG_AUDIO, "Calibration missing or invalid (%s)", esp_err_to_name(ret));
    }
    return false;
  }

  ESP_LOGI(TAG_AUDIO, "Calibration loaded (noise=%.3f, peak=%.3f)", calibration.noise_floor, calibration.peak_level);
  return calibration.calibrated;
}

void audio_input_reset_config(void) {
  current_config.enabled     = false;
  current_config.sensitivity = 128;
  current_config.gain        = 128;
  current_config.auto_gain   = true;

  calibration.calibrated     = false;
  calibration.noise_floor    = 0.0f;
  calibration.peak_level     = 1.0f;

  ESP_LOGI(TAG_AUDIO, "Configuration reset");
}

bool audio_input_run_calibration(uint32_t duration_ms, audio_calibration_t *result) {
  if (!initialized) {
    ESP_LOGE(TAG_AUDIO, "Audio module not initialized");
    return false;
  }

  if (!enabled) {
    ESP_LOGE(TAG_AUDIO, "Microphone disabled: enable before calibration");
    return false;
  }

  if (calibration_running) {
    ESP_LOGW(TAG_AUDIO, "Calibration already running");
    return false;
  }

  uint32_t window_ms = duration_ms > 0 ? duration_ms : 5000;
  calibration_reset_state(window_ms);

  // Wait for the calibration window to finish (short loop)
  TickType_t guard_tick = calibration_end_tick + pdMS_TO_TICKS(1000);
  while (calibration_running) {
    vTaskDelay(pdMS_TO_TICKS(50));
    if (xTaskGetTickCount() > guard_tick) {
      calibration_running = false;
      break;
    }
  }

  if (!calibration_ready || !calib_result.calibrated) {
    ESP_LOGE(TAG_AUDIO, "Calibration failed (no data)");
    return false;
  }

  // Update active calibration and save
  calibration = calib_result;
  audio_input_save_calibration();

  if (result) {
    memcpy(result, &calibration, sizeof(audio_calibration_t));
  }

  ESP_LOGI(TAG_AUDIO, "Calibration successful (noise=%.3f, peak=%.3f)", calibration.noise_floor, calibration.peak_level);
  return true;
}

bool audio_input_reset_calibration(void) {
  if (!initialized) {
    ESP_LOGE(TAG_AUDIO, "Audio module not initialized");
    return false;
  }

  // Reset calibration to default uncalibrated state
  calibration.calibrated  = false;
  calibration.noise_floor = 0.0f;
  calibration.peak_level  = 0.0f;

  // Save the reset calibration
  if (!audio_input_save_calibration()) {
    ESP_LOGE(TAG_AUDIO, "Failed to save reset calibration");
    return false;
  }

  ESP_LOGI(TAG_AUDIO, "Calibration reset successfully");
  return true;
}

void audio_input_get_calibration(audio_calibration_t *out_calibration) {
  if (out_calibration) {
    memcpy(out_calibration, &calibration, sizeof(audio_calibration_t));
  }
}

// ============================================================================
// FFT IMPLEMENTATION (Cooley-Tukey radix-2 DIT)
// ============================================================================

// FFT radix-2 in-place (Cooley-Tukey algorithm)
// data contains [real0, imag0, real1, imag1, ...]
static void fft_radix2(float *data, int size, bool inverse) {
  if (size <= 1)
    return;

  // Bit reversal permutation
  int j = 0;
  for (int i = 0; i < size - 1; i++) {
    if (i < j) {
      // Swap real parts
      float temp      = data[i * 2];
      data[i * 2]     = data[j * 2];
      data[j * 2]     = temp;
      // Swap imaginary parts
      temp            = data[i * 2 + 1];
      data[i * 2 + 1] = data[j * 2 + 1];
      data[j * 2 + 1] = temp;
    }
    int k = size / 2;
    while (k <= j) {
      j -= k;
      k /= 2;
    }
    j += k;
  }

  // FFT computation
  float direction = inverse ? 1.0f : -1.0f;
  for (int len = 2; len <= size; len *= 2) {
    float angle     = direction * 2.0f * M_PI / len;
    float wlen_real = cosf(angle);
    float wlen_imag = sinf(angle);

    for (int i = 0; i < size; i += len) {
      float w_real = 1.0f;
      float w_imag = 0.0f;

      for (int j = 0; j < len / 2; j++) {
        int idx_even       = (i + j) * 2;
        int idx_odd        = (i + j + len / 2) * 2;

        float even_real    = data[idx_even];
        float even_imag    = data[idx_even + 1];
        float odd_real     = data[idx_odd];
        float odd_imag     = data[idx_odd + 1];

        // Multiply odd by twiddle factor
        float t_real       = w_real * odd_real - w_imag * odd_imag;
        float t_imag       = w_real * odd_imag + w_imag * odd_real;

        // Butterfly operation
        data[idx_even]     = even_real + t_real;
        data[idx_even + 1] = even_imag + t_imag;
        data[idx_odd]      = even_real - t_real;
        data[idx_odd + 1]  = even_imag - t_imag;

        // Update twiddle factor
        float w_temp       = w_real;
        w_real             = w_real * wlen_real - w_imag * wlen_imag;
        w_imag             = w_temp * wlen_imag + w_imag * wlen_real;
      }
    }
  }

  // Normalize if inverse FFT
  if (inverse) {
    for (int i = 0; i < size * 2; i++) {
      data[i] /= size;
    }
  }
}

// Compute FFT spectrum magnitude and store in fft_output
static void compute_fft_magnitude(int32_t *audio_samples, int num_samples) {
  // Prepare data for FFT (format [real, imag, real, imag, ...])
  static float fft_complex[AUDIO_FFT_SIZE * 2]; // 512 * 2 = 1024 floats

  // Copy audio samples into real part, imag = 0
  for (int i = 0; i < AUDIO_FFT_SIZE && i < num_samples; i++) {
    fft_complex[i * 2]     = (float)audio_samples[i] / 2147483648.0f; // Normalize
    fft_complex[i * 2 + 1] = 0.0f;                                    // Imaginary part = 0
  }

  // Zero-pad if needed
  for (int i = num_samples; i < AUDIO_FFT_SIZE; i++) {
    fft_complex[i * 2]     = 0.0f;
    fft_complex[i * 2 + 1] = 0.0f;
  }

  // Apply Hann window to reduce spectral leakage
  for (int i = 0; i < AUDIO_FFT_SIZE; i++) {
    float multiplier = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (AUDIO_FFT_SIZE - 1)));
    fft_complex[i * 2] *= multiplier;
  }

  // Compute the FFT
  fft_radix2(fft_complex, AUDIO_FFT_SIZE, false);

  // Compute magnitudes (first half only, symmetric)
  for (int i = 0; i < AUDIO_FFT_SIZE / 2; i++) {
    float real    = fft_complex[i * 2];
    float imag    = fft_complex[i * 2 + 1];
    fft_output[i] = sqrtf(real * real + imag * imag);
  }
}

// Group FFT bins into logarithmic frequency bands
static void group_fft_into_bands(audio_fft_data_t *fft_data) {
  // Frequency resolution: freq_resolution = SAMPLE_RATE / FFT_SIZE
  // For 44100 Hz and FFT 512: 44100 / 512 = 86.13 Hz per bin
  float freq_resolution = (float)AUDIO_SAMPLE_RATE / (float)AUDIO_FFT_SIZE;

  // Group into AUDIO_FFT_BANDS logarithmic bands
  // Useful range: 20 Hz - 20000 Hz (human limit)
  float min_freq        = 20.0f;
  float max_freq        = 20000.0f;

  // Logarithmic factor
  float log_min         = logf(min_freq);
  float log_max         = logf(max_freq);
  float log_step        = (log_max - log_min) / AUDIO_FFT_BANDS;

  float max_band_value  = 0.0f;
  int max_band_index    = 0;

  for (int band = 0; band < AUDIO_FFT_BANDS; band++) {
    float freq_low  = expf(log_min + band * log_step);
    float freq_high = expf(log_min + (band + 1) * log_step);

    int bin_low     = (int)(freq_low / freq_resolution);
    int bin_high    = (int)(freq_high / freq_resolution);

    if (bin_low < 0)
      bin_low = 0;
    if (bin_high >= AUDIO_FFT_SIZE / 2)
      bin_high = AUDIO_FFT_SIZE / 2 - 1;

    // Average energy in this band
    float band_energy = 0.0f;
    int bin_count     = bin_high - bin_low + 1;
    if (bin_count > 0) {
      for (int bin = bin_low; bin <= bin_high; bin++) {
        band_energy += fft_output[bin];
      }
      band_energy /= bin_count;
    }

    fft_data->bands[band] = band_energy;

    if (band_energy > max_band_value) {
      max_band_value = band_energy;
      max_band_index = band;
    }
  }

  // Normalize bands (0.0 - 1.0)
  if (max_band_value > 0.0f) {
    for (int i = 0; i < AUDIO_FFT_BANDS; i++) {
      fft_data->bands[i] /= max_band_value;
    }
  }

  fft_data->dominant_band = max_band_index;

  // Compute peak frequency
  float max_magnitude     = 0.0f;
  int max_bin             = 0;
  for (int i = 1; i < AUDIO_FFT_SIZE / 2; i++) {
    if (fft_output[i] > max_magnitude) {
      max_magnitude = fft_output[i];
      max_bin       = i;
    }
  }
  fft_data->peak_freq = max_bin * freq_resolution;

  // Compute spectral centroid (spectrum center of mass)
  float weighted_sum  = 0.0f;
  float magnitude_sum = 0.0f;
  for (int i = 0; i < AUDIO_FFT_SIZE / 2; i++) {
    float freq = i * freq_resolution;
    weighted_sum += freq * fft_output[i];
    magnitude_sum += fft_output[i];
  }
  fft_data->spectral_centroid = (magnitude_sum > 0.0f) ? (weighted_sum / magnitude_sum) : 0.0f;

  // Compute energy per frequency range
  fft_data->bass_energy       = 0.0f;
  fft_data->mid_energy        = 0.0f;
  fft_data->treble_energy     = 0.0f;

  for (int i = 0; i < AUDIO_FFT_SIZE / 2; i++) {
    float freq = i * freq_resolution;
    if (freq >= FREQ_BASS_LOW && freq < FREQ_BASS_HIGH) {
      fft_data->bass_energy += fft_output[i];
    } else if (freq >= FREQ_MID_LOW && freq < FREQ_MID_HIGH) {
      fft_data->mid_energy += fft_output[i];
    } else if (freq >= FREQ_TREBLE_LOW && freq < FREQ_TREBLE_HIGH) {
      fft_data->treble_energy += fft_output[i];
    }
  }

  // Normalize energies
  float total_energy = fft_data->bass_energy + fft_data->mid_energy + fft_data->treble_energy;
  if (total_energy > 0.0f) {
    fft_data->bass_energy /= total_energy;
    fft_data->mid_energy /= total_energy;
    fft_data->treble_energy /= total_energy;
  }

  // Kick detection (20-120 Hz, sudden high energy)
  float kick_energy = 0.0f;
  for (int i = 0; i < AUDIO_FFT_SIZE / 2; i++) {
    float freq = i * freq_resolution;
    if (freq >= 20.0f && freq < 120.0f) {
      kick_energy += fft_output[i];
    }
  }
  fft_data->kick_detected = (kick_energy > 0.5f); // Arbitrary threshold

  // Snare detection (150-250 Hz)
  float snare_energy      = 0.0f;
  for (int i = 0; i < AUDIO_FFT_SIZE / 2; i++) {
    float freq = i * freq_resolution;
    if (freq >= 150.0f && freq < 250.0f) {
      snare_energy += fft_output[i];
    }
  }
  fft_data->snare_detected = (snare_energy > 0.3f); // Arbitrary threshold

  // Vocal detection (500-2000 Hz, concentrated energy)
  float vocal_energy       = 0.0f;
  for (int i = 0; i < AUDIO_FFT_SIZE / 2; i++) {
    float freq = i * freq_resolution;
    if (freq >= 500.0f && freq < 2000.0f) {
      vocal_energy += fft_output[i];
    }
  }
  fft_data->vocal_detected = (vocal_energy > 0.4f); // Arbitrary threshold
}

// ============================================================================
// FFT API Functions
// ============================================================================

void audio_input_set_fft_enabled(bool enable) {
  if (current_config.fft_enabled == enable) {
    return;
  }

  current_config.fft_enabled = enable;

  if (enable) {
    ESP_LOGI(TAG_AUDIO, "FFT mode enabled (cost: ~20KB RAM, ~20%% CPU)");
  } else {
    ESP_LOGI(TAG_AUDIO, "FFT mode disabled");
    fft_data_ready = false;
    memset(&current_fft_data, 0, sizeof(audio_fft_data_t));
  }
}

bool audio_input_is_fft_enabled(void) {
  return current_config.fft_enabled;
}

bool audio_input_get_fft_data(audio_fft_data_t *fft_data) {
  if (fft_data == NULL || !fft_data_ready || !current_config.fft_enabled) {
    return false;
  }

  memcpy(fft_data, &current_fft_data, sizeof(audio_fft_data_t));
  return true;
}
