#ifndef ESPNOW_LINK_H
#define ESPNOW_LINK_H

#include "esp_err.h"
#include "vehicle_can_unified.h"

#include <stdbool.h>
#include <stdint.h>

#define TAG_ESP_NOW "ESP_NOW_LINK"

// Rôles
typedef enum {
  ESP_NOW_ROLE_MASTER = 0,
  ESP_NOW_ROLE_SLAVE  = 1
} espnow_role_t;

// Types d'esclaves supportés
typedef enum {
  ESP_NOW_SLAVE_NONE = 0,
  ESP_NOW_SLAVE_EVENTS_LEFT,
  ESP_NOW_SLAVE_EVENTS_RIGHT,
  ESP_NOW_SLAVE_SPEEDOMETER,
  ESP_NOW_SLAVE_MAX
} espnow_slave_type_t;

#define ESPNOW_MAX_REQUEST_IDS 24

typedef struct {
  uint8_t mac[6];
  espnow_role_t role;
  espnow_slave_type_t type;
  uint32_t device_id;
  uint64_t last_seen_us;
  uint8_t channel;
} espnow_peer_info_t;

#define ESPNOW_MAX_PEERS 8

typedef void (*espnow_test_rx_cb_t)(void);
typedef void (*espnow_vehicle_state_rx_cb_t)(const vehicle_state_t *state);

esp_err_t espnow_link_init(espnow_role_t role, espnow_slave_type_t slave_type);
esp_err_t espnow_link_register_peer(const uint8_t mac[6]);
esp_err_t espnow_link_get_peers(espnow_peer_info_t *out_peers, size_t max_peers, size_t *out_count);
esp_err_t espnow_link_send_test_frame(const uint8_t mac[6]); // if mac==NULL, send to all
esp_err_t espnow_link_disconnect(void); // slave disconnect from master
esp_err_t espnow_link_disconnect_peer(const uint8_t mac[6]); // master disconnect specific peer
esp_err_t espnow_link_send_vehicle_state(const vehicle_state_t *state);
void espnow_link_register_vehicle_state_rx_callback(espnow_vehicle_state_rx_cb_t cb);
espnow_role_t espnow_link_get_role(void);
espnow_slave_type_t espnow_link_get_slave_type(void);
void espnow_link_register_test_rx_callback(espnow_test_rx_cb_t cb);
esp_err_t espnow_link_set_pairing_mode(bool enable, uint32_t duration_s);
esp_err_t espnow_link_trigger_slave_pairing(void);
uint8_t espnow_link_get_channel(void);
bool espnow_link_is_pairing_active(void);
bool espnow_link_get_master_info(espnow_peer_info_t *out_master);
uint64_t espnow_link_get_last_test_rx_us(void);
void espnow_link_set_role_type(espnow_role_t role, espnow_slave_type_t type);
esp_err_t espnow_link_get_mac(uint8_t mac_out[6]);
uint32_t espnow_link_get_device_id(void);

// Helpers conversion/affichage
const char *espnow_link_role_to_str(espnow_role_t role);
const char *espnow_link_slave_type_to_str(espnow_slave_type_t type);
bool espnow_link_role_from_str(const char *s, espnow_role_t *out);
bool espnow_link_slave_type_from_str(const char *s, espnow_slave_type_t *out);

// Heartbeat
uint64_t espnow_link_get_last_peer_heartbeat_us(void);
bool espnow_link_peer_alive(uint32_t timeout_ms);
bool espnow_link_is_connected(void);

#endif // ESPNOW_LINK_H
