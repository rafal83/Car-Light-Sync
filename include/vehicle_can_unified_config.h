
#ifndef VEHICLE_CAN_UNIFIED_CONFIG_H
#define VEHICLE_CAN_UNIFIED_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { BYTE_ORDER_LITTLE_ENDIAN = 0, BYTE_ORDER_BIG_ENDIAN = 1 } byte_order_t;

typedef enum { SIGNAL_TYPE_UNSIGNED = 0, SIGNAL_TYPE_SIGNED = 1, SIGNAL_TYPE_BOOLEAN = 2 } signal_type_t;

// Définition d'un signal DBC (ex: DI_vehicleSpeed, UI_turnSignalLeft, etc.)
typedef struct can_signal_def_t {
    const char* name;
    uint8_t start_bit;
    uint8_t length;
    byte_order_t byte_order;
    signal_type_t value_type;
    float factor;
    float offset;
} can_signal_def_t;

// Définition d'un message CAN DBC (ex: ID118DriveSystemStatus)
typedef struct can_message_def_t {
    uint32_t id;
    const char* name;
    const can_signal_def_t* signals;
    uint8_t signal_count;
} can_message_def_t;

// Tableau global généré à partir de Model3CAN.json
extern const can_message_def_t g_can_messages[];
extern const uint16_t g_can_message_count;

#ifdef __cplusplus
}
#endif

#endif  // VEHICLE_CAN_UNIFIED_CONFIG_H
