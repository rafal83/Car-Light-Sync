#ifndef VEHICLE_CAN_CONFIG_H
#define VEHICLE_CAN_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// Taille maximale des buffers
#define MAX_SIGNAL_NAME_LEN 32
#define MAX_MESSAGE_NAME_LEN 32
#define MAX_UNIT_LEN 16
#define MAX_SIGNALS_PER_MESSAGE 16
#define MAX_EVENTS_PER_SIGNAL 4
#define MAX_CAN_MESSAGES 32

// Types de données des signaux
typedef enum {
    SIGNAL_TYPE_UNSIGNED = 0,
    SIGNAL_TYPE_SIGNED,
    SIGNAL_TYPE_BOOLEAN,
    SIGNAL_TYPE_FLOAT
} signal_value_type_t;

// Ordre des bytes
typedef enum {
    BYTE_ORDER_LITTLE_ENDIAN = 0,
    BYTE_ORDER_BIG_ENDIAN
} byte_order_t;

// Conditions de déclenchement d'événements
typedef enum {
    EVENT_CONDITION_VALUE_EQUALS = 0,    // Signal == value
    EVENT_CONDITION_RISING_EDGE,         // Signal 0→1
    EVENT_CONDITION_FALLING_EDGE,        // Signal 1→0
    EVENT_CONDITION_GREATER_THAN,        // Signal > value
    EVENT_CONDITION_LESS_THAN,           // Signal < value
    EVENT_CONDITION_CHANGE               // Signal a changé
} event_condition_t;

// Configuration d'un événement lié à un signal
typedef struct {
    event_condition_t condition;
    can_event_type_t trigger;           // Type d'événement à déclencher
    float value;                        // Valeur de comparaison (si applicable)
    bool enabled;
} signal_event_config_t;

// Version const pour événements statiques (ROM)
typedef struct {
    event_condition_t condition;
    can_event_type_t trigger;
    float value;
} can_event_config_t;

// Configuration d'un signal CAN
typedef struct {
    char name[MAX_SIGNAL_NAME_LEN];
    uint8_t start_bit;
    uint8_t length;
    byte_order_t byte_order;
    signal_value_type_t value_type;
    float factor;
    float offset;
    char unit[MAX_UNIT_LEN];
    float min_value;
    float max_value;

    // Événements associés à ce signal
    signal_event_config_t events[MAX_EVENTS_PER_SIGNAL];
    uint8_t event_count;

    // État précédent (pour détecter les changements)
    float last_value;
    bool last_value_valid;
} can_signal_config_t;

// Version simplifiée pour données statiques (ROM)
typedef struct {
    uint8_t start_bit;
    uint8_t length;
    byte_order_t byte_order;
    signal_value_type_t value_type;
    float factor;
    float offset;
    const can_event_config_t* events;
    uint8_t event_count;
} can_signal_config_static_t;

// Configuration d'un message CAN
typedef struct {
    uint32_t message_id;
    char name[MAX_MESSAGE_NAME_LEN];
    uint8_t bus;                        // 0=chassis, 1=powertrain, 2=body
    uint16_t cycle_time_ms;

    can_signal_config_t signals[MAX_SIGNALS_PER_MESSAGE];
    uint8_t signal_count;

    uint32_t last_rx_time;              // Dernière réception (ms)
} can_message_config_t;

// Version simplifiée pour messages statiques (ROM)
typedef struct {
    uint32_t message_id;
    uint8_t bus;
    const can_signal_config_static_t* signals;
    uint8_t signal_count;
} can_message_config_static_t;

// Configuration complète du véhicule
typedef struct {
    char make[32];
    char model[32];
    uint16_t year;
    char variant[64];

    uint8_t bus_chassis;
    uint8_t bus_powertrain;
    uint8_t bus_body;
    uint32_t baudrate;

    can_message_config_t messages[MAX_CAN_MESSAGES];
    uint8_t message_count;
} vehicle_can_config_t;

// Version simplifiée pour configuration statique (ROM)
typedef struct {
    const can_message_config_static_t* messages;
    uint8_t message_count;
} vehicle_can_config_static_t;

/**
 * @brief Décode un message CAN en utilisant une configuration statique
 * @param config Configuration statique du véhicule
 * @param message_id ID du message CAN
 * @param data Données CAN (8 bytes max)
 * @param data_len Longueur des données
 * @param bus Numéro du bus CAN
 * @param out_state État du véhicule à mettre à jour
 * @return true si décodé, false sinon
 */
bool vehicle_can_decode_message_static(
    const vehicle_can_config_static_t* config,
    uint32_t message_id,
    const uint8_t* data,
    uint8_t data_len,
    uint8_t bus,
    vehicle_state_t* out_state
);

/**
 * @brief Extrait la valeur d'un signal statique
 * @param signal Configuration du signal
 * @param data Données CAN
 * @param data_len Longueur des données
 * @return Valeur extraite et convertie
 */
float vehicle_can_extract_signal_value_static(
    const can_signal_config_static_t* signal,
    const uint8_t* data,
    uint8_t data_len
);

/**
 * @brief Déclenche un événement CAN (intégration avec système LED)
 * @param event Type d'événement
 * @param state État du véhicule
 */
void can_event_trigger(can_event_type_t event, vehicle_state_t* state);

#ifdef __cplusplus
}
#endif

#endif // VEHICLE_CAN_CONFIG_H
