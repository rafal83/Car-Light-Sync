#include "vehicle_can_config.h"
#include "config_manager.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <math.h>
#include "tesla_can.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "VehicleCAN";

// ============================================================================
// Fonctions pour configurations statiques (ROM)
// ============================================================================

float vehicle_can_extract_signal_value_static(
    const can_signal_config_static_t* signal,
    const uint8_t* data,
    uint8_t data_len
) {
    if (signal == NULL || data == NULL) return 0.0f;

    uint64_t raw_value = 0;
    uint8_t byte_pos = signal->start_bit / 8;
    uint8_t bit_pos = signal->start_bit % 8;

    // Extraction des bits selon l'ordre des bytes
    if (signal->byte_order == BYTE_ORDER_LITTLE_ENDIAN) {
        // Little endian (Intel)
        for (uint8_t i = 0; i < signal->length && byte_pos < data_len; i++) {
            uint8_t bit_in_byte = (bit_pos + i) % 8;
            uint8_t current_byte = byte_pos + (bit_pos + i) / 8;

            if (current_byte >= data_len) break;

            if (data[current_byte] & (1 << bit_in_byte)) {
                raw_value |= (1ULL << i);
            }
        }
    } else {
        // Big endian (Motorola)
        for (uint8_t i = 0; i < signal->length; i++) {
            uint8_t bit_index = signal->start_bit - i;
            uint8_t current_byte = bit_index / 8;
            uint8_t bit_in_byte = bit_index % 8;

            if (current_byte >= data_len) break;

            if (data[current_byte] & (1 << bit_in_byte)) {
                raw_value |= (1ULL << (signal->length - 1 - i));
            }
        }
    }

    // Conversion en signé si nécessaire
    float value = (float)raw_value;
    if (signal->value_type == SIGNAL_TYPE_SIGNED && signal->length < 64) {
        uint64_t sign_bit = 1ULL << (signal->length - 1);
        if (raw_value & sign_bit) {
            // Extension de signe
            uint64_t mask = ~((1ULL << signal->length) - 1);
            int64_t signed_value = (int64_t)(raw_value | mask);
            value = (float)signed_value;
        }
    }

    // Appliquer facteur et offset
    value = value * signal->factor + signal->offset;

    return value;
}

bool vehicle_can_decode_message_static(
    const vehicle_can_config_static_t* config,
    uint32_t message_id,
    const uint8_t* data,
    uint8_t data_len,
    uint8_t bus,
    vehicle_state_t* out_state
) {
    if (config == NULL || data == NULL || out_state == NULL) {
        return false;
    }

    // Trouver le message correspondant
    const can_message_config_static_t* message = NULL;
    for (uint8_t i = 0; i < config->message_count; i++) {
        if (config->messages[i].message_id == message_id) {
            message = &config->messages[i];
            break;
        }
    }

    if (message == NULL) {
        return false; // Message non trouvé dans la config
    }

    // Traiter chaque signal
    bool decoded = false;
    for (uint8_t i = 0; i < message->signal_count; i++) {
        const can_signal_config_static_t* signal = &message->signals[i];

        // Extraire la valeur du signal
        float value = vehicle_can_extract_signal_value_static(signal, data, data_len);

        // Vérifier les événements associés
        for (uint8_t j = 0; j < signal->event_count; j++) {
            const can_event_config_t* event = &signal->events[j];
            bool trigger_event = false;

            switch (event->condition) {
                case EVENT_CONDITION_VALUE_EQUALS:
                    trigger_event = (fabsf(value - event->value) < 0.001f);
                    break;

                case EVENT_CONDITION_GREATER_THAN:
                    trigger_event = (value > event->value);
                    break;

                case EVENT_CONDITION_LESS_THAN:
                    trigger_event = (value < event->value);
                    break;

                // Rising/falling edge nécessitent état précédent (non géré en statique)
                default:
                    break;
            }

            if (trigger_event) {
                // Déclencher l'événement CAN
                can_event_trigger(event->trigger, out_state);
                decoded = true;
            }
        }
    }

    return decoded;
}

/**
 * @brief Déclenche un événement CAN et applique l'effet correspondant
 * Cette fonction est appelée automatiquement par le décodeur CAN statique
 * quand un événement est détecté sur le bus CAN.
 */
void can_event_trigger(can_event_type_t event, vehicle_state_t* state) {
    // Traiter l'événement via le gestionnaire de profils
    // qui appliquera automatiquement l'effet LED configuré
    if (config_manager_process_can_event(event)) {
        ESP_LOGD(TAG, "Event %d -> effet LED appliqué", event);
    }
}
