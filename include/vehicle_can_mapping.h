#ifndef VEHICLE_CAN_MAPPING_H
#define VEHICLE_CAN_MAPPING_H

#include "can_bus.h"
#include "vehicle_can_unified.h"
#include "vehicle_can_unified_config.h"

#include <stdint.h>

#define TAG_CAN "CAN"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Callbacks (overridables) pour mapper signaux -> état / events
// ---------------------------------------------------------------------------

void vehicle_state_apply_signal(const struct can_message_def_t *msg, const struct can_signal_def_t *sig, float value, uint8_t bus_id, vehicle_state_t *state);

// Callback pour les événements de scroll (appelé immédiatement quand scroll change)
// scroll_value: >0 pour scroll up, <0 pour scroll down
typedef void (*vehicle_wheel_scroll_callback_t)(float scroll_value, const vehicle_state_t *state);
void vehicle_can_set_wheel_scroll_callback(vehicle_wheel_scroll_callback_t callback);

// Construit un couple (code, mask) pour filtrage TWAI matériel (standard ID 11 bits).
// Retourne false si aucune liste d'IDs n'est définie pour ce bus (laisser filtrage large).
bool vehicle_can_get_twai_filter(can_bus_type_t bus, uint32_t *code_out, uint32_t *mask_out);

#ifdef __cplusplus
}
#endif

#endif // VEHICLE_CAN_MAPPING_H
