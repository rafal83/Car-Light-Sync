#ifndef ESPNOW_LINK_H
#define ESPNOW_LINK_H

#include "esp_err.h"
#include "driver/twai.h"
#include <stdint.h>
#include <stdbool.h>

// Active les hooks ESP-NOW dans les modules qui l'incluent
#ifndef ESPNOW_LINK_ENABLED
#define ESPNOW_LINK_ENABLED 1
#endif

// Rôles
typedef enum {
    ESP_NOW_ROLE_MASTER = 0,
    ESP_NOW_ROLE_SLAVE  = 1
} espnow_role_t;

// Types d'esclaves supportés
typedef enum {
    ESP_NOW_SLAVE_NONE = 0,
    ESP_NOW_SLAVE_BLINDSPOT_LEFT,
    ESP_NOW_SLAVE_BLINDSPOT_RIGHT,
    ESP_NOW_SLAVE_SPEEDOMETER,
    ESP_NOW_SLAVE_MAX
} espnow_slave_type_t;

typedef struct {
    uint32_t ids[16];
    uint8_t count;
} espnow_request_list_t;

typedef struct {
    uint32_t can_id;
    uint8_t bus;
    uint8_t dlc;
    uint8_t data[8];
    uint16_t ts_ms;
} espnow_can_frame_t;

typedef void (*espnow_can_rx_cb_t)(const espnow_can_frame_t *frame);

esp_err_t espnow_link_init(espnow_role_t role, espnow_slave_type_t slave_type);
esp_err_t espnow_link_set_requests(const espnow_request_list_t *reqs);
esp_err_t espnow_link_register_peer(const uint8_t mac[6]);
void espnow_link_on_can_frame(const twai_message_t *msg, int bus);
espnow_role_t espnow_link_get_role(void);
espnow_slave_type_t espnow_link_get_slave_type(void);
void espnow_link_register_rx_callback(espnow_can_rx_cb_t cb);

#endif // ESPNOW_LINK_H
