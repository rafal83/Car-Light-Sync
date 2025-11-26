#include "audio_input.h"
#include "config.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <math.h>
#include <string.h>

static const char *TAG = "Audio";

// État du module
static bool initialized = false;
static bool enabled = false;
static i2s_chan_handle_t rx_handle = NULL;
static TaskHandle_t audio_task_handle = NULL;

// Configuration
static audio_config_t current_config = {
    .enabled = false,
    .sensitivity = 128,
    .gain = 128,
    .auto_gain = true,
    .i2s_sck_pin = AUDIO_I2S_SCK_PIN,
    .i2s_ws_pin = AUDIO_I2S_WS_PIN,
    .i2s_sd_pin = AUDIO_I2S_SD_PIN
};

// Données audio
static audio_data_t current_audio_data = {0};
static bool audio_data_ready = false;

// Buffers
static int32_t audio_buffer[AUDIO_BUFFER_SIZE];
static float fft_input[AUDIO_FFT_SIZE];

// Variables pour la détection de BPM
static float bpm_history[AUDIO_BPM_HISTORY_SIZE] = {0};
static uint8_t bpm_history_index = 0;
static uint32_t last_beat_time_ms = 0;
static float energy_history[8] = {0};
static uint8_t energy_index = 0;

// Fonction pour calculer l'amplitude RMS
static float calculate_rms(int32_t *buffer, size_t size) {
    float sum = 0.0f;
    for (size_t i = 0; i < size; i++) {
        float sample = (float)buffer[i] / 2147483648.0f; // Normaliser 32-bit
        sum += sample * sample;
    }
    return sqrtf(sum / size);
}

// Fonction simplifiée pour calculer l'énergie par bande de fréquence
static void calculate_frequency_bands(int32_t *buffer, size_t size, float *bass, float *mid, float *treble) {
    // Calculer l'énergie dans différentes plages du signal temporel
    float low_energy = 0.0f;
    float mid_energy = 0.0f;
    float high_energy = 0.0f;

    // Diviser le buffer en segments et analyser les variations
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

        // Répartir l'énergie selon les segments (approximation des fréquences)
        if (seg < 2) {
            low_energy += seg_energy;
        } else if (seg < 5) {
            mid_energy += seg_energy;
        } else {
            high_energy += seg_energy;
        }
    }

    // Normaliser
    float total = low_energy + mid_energy + high_energy;
    if (total > 0.0f) {
        *bass = low_energy / total;
        *mid = mid_energy / total;
        *treble = high_energy / total;
    } else {
        *bass = 0.0f;
        *mid = 0.0f;
        *treble = 0.0f;
    }
}

// Détection de battement simple basée sur l'énergie
static bool detect_beat(float amplitude) {
    // Calculer l'énergie actuelle
    float current_energy = amplitude * amplitude;

    // Calculer l'énergie moyenne sur l'historique
    float avg_energy = 0.0f;
    for (int i = 0; i < 8; i++) {
        avg_energy += energy_history[i];
    }
    avg_energy /= 8.0f;

    // Stocker l'énergie actuelle
    energy_history[energy_index] = current_energy;
    energy_index = (energy_index + 1) % 8;

    // Détection de pic: énergie actuelle > 1.5x la moyenne
    bool beat = (current_energy > avg_energy * 1.5f) && (avg_energy > 0.01f);

    return beat;
}

// Calcul du BPM basé sur l'intervalle entre battements
static float calculate_bpm(uint32_t current_time_ms) {
    if (last_beat_time_ms == 0) {
        last_beat_time_ms = current_time_ms;
        return 0.0f;
    }

    uint32_t interval_ms = current_time_ms - last_beat_time_ms;
    last_beat_time_ms = current_time_ms;

    // Ignorer les intervalles trop courts ou trop longs
    if (interval_ms < 333 || interval_ms > 1000) { // 60-180 BPM
        return current_audio_data.bpm; // Garder l'ancienne valeur
    }

    // Calculer le BPM brut
    float raw_bpm = 60000.0f / (float)interval_ms;

    // Lisser avec l'historique
    bpm_history[bpm_history_index] = raw_bpm;
    bpm_history_index = (bpm_history_index + 1) % AUDIO_BPM_HISTORY_SIZE;

    float avg_bpm = 0.0f;
    int count = 0;
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

// Tâche de traitement audio
static void audio_task(void *pvParameters) {
    ESP_LOGI(TAG, "Tâche audio démarrée");

    size_t bytes_read = 0;

    while (1) {
        if (!enabled || rx_handle == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Lire les données I2S
        esp_err_t ret = i2s_channel_read(rx_handle, audio_buffer,
                                         sizeof(audio_buffer), &bytes_read,
                                         pdMS_TO_TICKS(100));

        if (ret != ESP_OK || bytes_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t samples = bytes_read / sizeof(int32_t);

        // Calculer l'amplitude RMS
        float rms = calculate_rms(audio_buffer, samples);

        // Appliquer la sensibilité et le gain
        float sensitivity_factor = (float)current_config.sensitivity / 128.0f;
        float gain_factor = (float)current_config.gain / 128.0f;
        float amplitude = rms * sensitivity_factor * gain_factor * 10.0f;

        // Limiter à 0.0-1.0
        if (amplitude > 1.0f) amplitude = 1.0f;

        // Calculer les bandes de fréquence
        float bass, mid, treble;
        calculate_frequency_bands(audio_buffer, samples, &bass, &mid, &treble);

        // Détection de battement
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        bool beat = detect_beat(amplitude);

        float bpm = current_audio_data.bpm;
        if (beat) {
            bpm = calculate_bpm(current_time);
            current_audio_data.last_beat_ms = current_time;
        }

        // Mettre à jour les données
        current_audio_data.amplitude = amplitude;
        current_audio_data.bass = bass * amplitude;
        current_audio_data.mid = mid * amplitude;
        current_audio_data.treble = treble * amplitude;
        current_audio_data.bpm = bpm;
        current_audio_data.beat_detected = beat;

        audio_data_ready = true;

        // Petit délai pour ne pas saturer le CPU
        vTaskDelay(pdMS_TO_TICKS(20)); // ~50Hz de mise à jour
    }
}

bool audio_input_init(void) {
    if (initialized) {
        ESP_LOGW(TAG, "Module audio déjà initialisé");
        return true;
    }

    // Charger la configuration sauvegardée
    audio_input_load_config();

    // Créer le canal I2S RX
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = AUDIO_DMA_BUFFER_COUNT;
    chan_cfg.dma_frame_num = AUDIO_BUFFER_SIZE / 2;

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur création canal I2S: %s", esp_err_to_name(ret));
        return false;
    }

    // Configuration standard I2S pour INMP441
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = current_config.i2s_sck_pin,
            .ws = current_config.i2s_ws_pin,
            .dout = I2S_GPIO_UNUSED,
            .din = current_config.i2s_sd_pin,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur init mode standard I2S: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
        return false;
    }

    // Créer la tâche de traitement audio
    BaseType_t task_ret = xTaskCreate(audio_task, "audio_task", 4096, NULL, 5, &audio_task_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Erreur création tâche audio");
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
        return false;
    }

    initialized = true;
    ESP_LOGI(TAG, "Module audio initialisé (GPIO: SCK=%d, WS=%d, SD=%d)",
             current_config.i2s_sck_pin, current_config.i2s_ws_pin, current_config.i2s_sd_pin);

    // Activer si configuré
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
    ESP_LOGI(TAG, "Module audio désinitalisé");
}

bool audio_input_set_enabled(bool enable) {
    if (!initialized) {
        ESP_LOGE(TAG, "Module audio non initialisé");
        return false;
    }

    if (enable == enabled) {
        return true;
    }

    if (enable) {
        esp_err_t ret = i2s_channel_enable(rx_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Erreur activation canal I2S: %s", esp_err_to_name(ret));
            return false;
        }
        enabled = true;
        current_config.enabled = true;
        ESP_LOGI(TAG, "Micro activé");
    } else {
        esp_err_t ret = i2s_channel_disable(rx_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Erreur désactivation canal I2S: %s", esp_err_to_name(ret));
            return false;
        }
        enabled = false;
        current_config.enabled = false;
        audio_data_ready = false;
        memset(&current_audio_data, 0, sizeof(audio_data_t));
        ESP_LOGI(TAG, "Micro désactivé");
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
    ESP_LOGI(TAG, "Sensibilité: %d", sensitivity);
}

void audio_input_set_gain(uint8_t gain) {
    current_config.gain = gain;
    ESP_LOGI(TAG, "Gain: %d", gain);
}

void audio_input_set_auto_gain(bool enable) {
    current_config.auto_gain = enable;
    ESP_LOGI(TAG, "Gain auto: %s", enable ? "ON" : "OFF");
}

bool audio_input_save_config(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("audio_config", NVS_READWRITE, &nvs_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur ouverture NVS: %s", esp_err_to_name(ret));
        return false;
    }

    ret = nvs_set_blob(nvs_handle, "config", &current_config, sizeof(audio_config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur sauvegarde config: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return false;
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Configuration sauvegardée");
    return true;
}

bool audio_input_load_config(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("audio_config", NVS_READONLY, &nvs_handle);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Pas de config audio sauvegardée");
        return false;
    }

    size_t required_size = sizeof(audio_config_t);
    ret = nvs_get_blob(nvs_handle, "config", &current_config, &required_size);
    nvs_close(nvs_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur lecture config: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Configuration chargée");
    return true;
}

void audio_input_reset_config(void) {
    current_config.enabled = false;
    current_config.sensitivity = 128;
    current_config.gain = 128;
    current_config.auto_gain = true;
    current_config.i2s_sck_pin = AUDIO_I2S_SCK_PIN;
    current_config.i2s_ws_pin = AUDIO_I2S_WS_PIN;
    current_config.i2s_sd_pin = AUDIO_I2S_SD_PIN;

    ESP_LOGI(TAG, "Configuration réinitialisée");
}
