
#ifndef VEHICLE_CAN_MAPPING_H
#define VEHICLE_CAN_MAPPING_H

#include <stdint.h>
#include "vehicle_can_unified.h"
#include "vehicle_can_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Callbacks (overridables) pour mapper signaux -> état / events
// ---------------------------------------------------------------------------

void vehicle_state_apply_signal(const struct can_message_def_t* msg,
                                const struct can_signal_def_t* sig,
                                float value,
                                vehicle_state_t* state);

#ifdef __cplusplus
}
#endif

#endif  // VEHICLE_CAN_MAPPING_H
