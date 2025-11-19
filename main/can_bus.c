// can_bus.c
#include "can_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Driver CAN ESP-IDF : selon version, c'est "twai" ou alias "can"
#include "driver/twai.h"

static const char *TAG = "CAN_BUS";

#ifndef CONFIG_CAN_TX_GPIO
// À adapter à ton câblage
#define CONFIG_CAN_TX_GPIO  GPIO_NUM_38
#define CONFIG_CAN_RX_GPIO  GPIO_NUM_39
#endif

static can_bus_callback_t s_callback = NULL;
static void* s_cb_user_data = NULL;
static TaskHandle_t s_rx_task_handle = NULL;

static volatile uint32_t s_rx_count = 0;
static volatile uint32_t s_tx_count = 0;
static volatile uint32_t s_errors   = 0;
static volatile bool     s_running  = false;

// ---- Tâche de réception ----
static void can_rx_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Tâche CAN RX démarrée");

    while (s_running) {
        twai_message_t msg;
        // Bloque jusqu'à réception
        esp_err_t ret = twai_receive(&msg, pdMS_TO_TICKS(1000));

        if (ret == ESP_OK) {
            s_rx_count++;

            if (s_callback) {
                can_frame_t frame = {0};
                frame.id  = msg.identifier;
                frame.dlc = msg.data_length_code;
                if (frame.dlc > 8) frame.dlc = 8;
                for (int i = 0; i < frame.dlc; i++) {
                    frame.data[i] = msg.data[i];
                }
                frame.timestamp_ms = xTaskGetTickCount();

                s_callback(&frame, s_cb_user_data);
            }
        } else if (ret == ESP_ERR_TIMEOUT) {
            // rien reçu, on boucle
            continue;
        } else {
            s_errors++;
            ESP_LOGW(TAG, "Erreur twai_receive: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    ESP_LOGI(TAG, "Tâche CAN RX terminée");
    s_rx_task_handle = NULL;
    vTaskDelete(NULL);
}

// ---- API publique ----

esp_err_t can_bus_init(void)
{
    // Config générale : mode normal, pins, pas d’auto-recovery exotique
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        CONFIG_CAN_TX_GPIO,
        CONFIG_CAN_RX_GPIO,
        TWAI_MODE_NORMAL
    );

    // Vitesse 500 kbit/s (chassis Tesla)
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();

    // Filtre : accepte toutes les trames
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t ret = twai_driver_install(&g_config, &t_config, &f_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "twai_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Driver CAN installé (TX=%d, RX=%d)",
             CONFIG_CAN_TX_GPIO, CONFIG_CAN_RX_GPIO);

    s_rx_count = s_tx_count = s_errors = 0;
    s_running = false;

    return ESP_OK;
}

esp_err_t can_bus_start(void)
{
    esp_err_t ret = twai_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "twai_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_running = true;

    // Tâche de réception
    if (s_rx_task_handle == NULL) {
        xTaskCreatePinnedToCore(
            can_rx_task,
            "can_rx_task",
            4096,
            NULL,
            10,
            &s_rx_task_handle,
            0             // core général
        );
    }

    ESP_LOGI(TAG, "Bus CAN démarré");

    return ESP_OK;
}

esp_err_t can_bus_stop(void)
{
    s_running = false;

    esp_err_t ret = twai_stop();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "twai_stop: %s", esp_err_to_name(ret));
    }

    ret = twai_driver_uninstall();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "twai_driver_uninstall: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

esp_err_t can_bus_register_callback(can_bus_callback_t cb, void* user_data)
{
    s_callback = cb;
    s_cb_user_data = user_data;
    return ESP_OK;
}

esp_err_t can_bus_send(const can_frame_t* frame)
{
    if (!frame) return ESP_ERR_INVALID_ARG;

    twai_message_t msg = {0};
    msg.identifier = frame->id;
    msg.data_length_code = frame->dlc;
    if (msg.data_length_code > 8) msg.data_length_code = 8;

    msg.extd = 0;  // Tesla = ID standard 11 bits
    msg.rtr  = 0;  // pas de Remote Transmission Request

    for (int i = 0; i < msg.data_length_code; i++) {
        msg.data[i] = frame->data[i];
    }

    esp_err_t ret = twai_transmit(&msg, pdMS_TO_TICKS(10));
    if (ret != ESP_OK) {
        s_errors++;
        ESP_LOGW(TAG, "Erreur twai_transmit: %s", esp_err_to_name(ret));
        return ret;
    }

    s_tx_count++;
    return ESP_OK;
}

esp_err_t can_bus_get_status(can_bus_status_t* out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    out->rx_count = s_rx_count;
    out->tx_count = s_tx_count;
    out->errors   = s_errors;
    out->running  = s_running;
    return ESP_OK;
}
