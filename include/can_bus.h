// can_bus.h
#pragma once

#include "esp_err.h"
#include "vehicle_can_unified.h" // for can_frame_t

#include <stdbool.h>

#define TAG_CAN_BUS "CAN_BUS"

#ifdef __cplusplus
extern "C" {
#endif

// Type de bus CAN
typedef enum {
  CAN_BUS_CHASSIS = 0,
  CAN_BUS_BODY    = 1,
  CAN_BUS_COUNT   = 2
} can_bus_type_t;

// Callback for each received CAN frame
typedef void (*can_bus_callback_t)(const can_frame_t *frame, can_bus_type_t bus_type, void *user_data);

// Initialize a specific CAN bus (without starting the bus)
esp_err_t can_bus_init(can_bus_type_t bus_type, int tx_gpio, int rx_gpio);

// Starts a CAN bus (reception + transmission)
esp_err_t can_bus_start(can_bus_type_t bus_type);

// Stops a CAN bus
esp_err_t can_bus_stop(can_bus_type_t bus_type);

// Registers a callback called for each received frame (shared by all buses)
esp_err_t can_bus_register_callback(can_bus_callback_t cb, void *user_data);

// Sends a CAN frame on a specific bus
esp_err_t can_bus_send(can_bus_type_t bus_type, const can_frame_t *frame);

// Simple status (optional, for monitor_task)
typedef struct {
  uint32_t rx_count;
  uint32_t tx_count;
  uint32_t errors;
  bool running;        // driver started (not necessarily frames received)
  bool receiving;      // frames received recently (short window)
  uint32_t last_rx_ms; // timestamp (ms) of last received frame, 0 if never
} can_bus_status_t;

esp_err_t can_bus_get_status(can_bus_type_t bus_type, can_bus_status_t *out);

#ifdef __cplusplus
}
#endif
