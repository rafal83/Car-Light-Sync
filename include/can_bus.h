// can_bus.h
#pragma once

#include "esp_err.h"
#include "vehicle_can_unified.h"  // pour can_frame_t
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Callback pour chaque trame CAN reçue
typedef void (*can_bus_callback_t)(const can_frame_t* frame, void* user_data);

// Initialisation du driver CAN (sans démarrer le bus)
esp_err_t can_bus_init(void);

// Démarre le bus CAN (réception + émission)
esp_err_t can_bus_start(void);

// Arrête le bus CAN
esp_err_t can_bus_stop(void);

// Enregistre un callback appelé à chaque frame reçue
esp_err_t can_bus_register_callback(can_bus_callback_t cb, void* user_data);

// Envoi d’une trame CAN
esp_err_t can_bus_send(const can_frame_t* frame);

// Statut simple (facultatif, pour monitor_task)
typedef struct {
    uint32_t rx_count;
    uint32_t tx_count;
    uint32_t errors;
    bool running;
} can_bus_status_t;

esp_err_t can_bus_get_status(can_bus_status_t* out);

#ifdef __cplusplus
}
#endif
