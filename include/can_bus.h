// can_bus.h
#pragma once

#include "esp_err.h"
#include "vehicle_can_unified.h" // pour can_frame_t
#include <stdbool.h>

#define TAG_CAN_BUS "CAN_BUS"

#ifdef __cplusplus
extern "C" {
#endif

// Type de bus CAN
typedef enum {
  CAN_BUS_CHASSIS = 0,
  CAN_BUS_BODY = 1,
  CAN_BUS_COUNT = 2
} can_bus_type_t;

// Callback pour chaque trame CAN reçue
typedef void (*can_bus_callback_t)(const can_frame_t *frame, can_bus_type_t bus_type, void *user_data);

// Initialisation d'un bus CAN spécifique (sans démarrer le bus)
esp_err_t can_bus_init(can_bus_type_t bus_type, int tx_gpio, int rx_gpio);

// Démarre un bus CAN (réception + émission)
esp_err_t can_bus_start(can_bus_type_t bus_type);

// Arrête un bus CAN
esp_err_t can_bus_stop(can_bus_type_t bus_type);

// Enregistre un callback appelé à chaque frame reçue (partagé par tous les bus)
esp_err_t can_bus_register_callback(can_bus_callback_t cb, void *user_data);

// Envoi d'une trame CAN sur un bus spécifique
esp_err_t can_bus_send(can_bus_type_t bus_type, const can_frame_t *frame);

// Statut simple (facultatif, pour monitor_task)
typedef struct {
  uint32_t rx_count;
  uint32_t tx_count;
  uint32_t errors;
  bool running;
} can_bus_status_t;

esp_err_t can_bus_get_status(can_bus_type_t bus_type, can_bus_status_t *out);

#ifdef __cplusplus
}
#endif
