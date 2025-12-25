#ifndef VEHICLE_CAN_MAPPING_H
#define VEHICLE_CAN_MAPPING_H

#include "can_bus.h"
#include "vehicle_can_unified.h"
#include "vehicle_can_unified_config.h"
#include "esp_attr.h"

#include <stdbool.h>
#include <stdint.h>

#define TAG_CAN "CAN"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Callbacks (overridable) to map signals -> state / events
// ---------------------------------------------------------------------------

// IRAM_ATTR: critical function called for every received CAN frame
void IRAM_ATTR vehicle_state_apply_signal(const struct can_message_def_t *msg, const struct can_signal_def_t *sig, float value, uint8_t bus_id, vehicle_state_t *state);

// Callback for scroll events (called immediately when scroll changes)
// scroll_value: >0 pour scroll up, <0 pour scroll down
typedef void (*vehicle_wheel_scroll_callback_t)(float scroll_value, const vehicle_state_t *state);
void vehicle_can_set_wheel_scroll_callback(vehicle_wheel_scroll_callback_t callback);

// Deferred state send flag (set in CAN callback, consumed in task context)
void vehicle_can_state_dirty_set(void);
bool vehicle_can_state_dirty_get(void);
void vehicle_can_state_dirty_clear(void);

#ifdef __cplusplus
}
#endif

#endif // VEHICLE_CAN_MAPPING_H
