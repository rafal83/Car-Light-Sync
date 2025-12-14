#include "espnow_link.h"

#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_manager.h"

#include <string.h>

static const char *TAG_ESP_NOW = "ESP_NOW_LINK";

// Protocole minimal
// type 0x01 HELLO {mac[6], slave_type, req_count, req_ids[]}
// type 0x02 HELLO_ACK {status, interval_ms}
// type 0x03 CAN_DATA {can_id u32, bus u8, dlc u8, data[8], ts_ms u16}

typedef enum {
  MSG_HELLO     = 0x01,
  MSG_HELLO_ACK = 0x02,
  MSG_CAN_DATA  = 0x03,
} msg_type_t;

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t slave_type;
  uint8_t req_count;
  uint8_t reserved;
  uint32_t ids[8];
} msg_hello_t;

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t status;
  uint16_t interval_ms;
} msg_hello_ack_t;

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t bus;
  uint8_t dlc;
  uint8_t reserved;
  uint32_t can_id;
  uint16_t ts_ms;
  uint8_t data[8];
} msg_can_data_t;

static espnow_role_t s_role             = ESP_NOW_ROLE_MASTER;
static espnow_slave_type_t s_slave_type = ESP_NOW_SLAVE_NONE;
static espnow_request_list_t s_requests = {0};
static espnow_can_rx_cb_t s_rx_cb       = NULL;

// Table simplifiée des IDs par type d'esclave
static void load_default_requests(espnow_slave_type_t type, espnow_request_list_t *out) {
  memset(out, 0, sizeof(*out));
  switch (type) {
  case ESP_NOW_SLAVE_BLINDSPOT_LEFT:
    out->ids[out->count++] = 0x399; // exemple Tesla blindspot left
    break;
  case ESP_NOW_SLAVE_BLINDSPOT_RIGHT:
    out->ids[out->count++] = 0x399; // exemple Tesla blindspot right
    break;
  case ESP_NOW_SLAVE_SPEEDOMETER:
    out->ids[out->count++] = 0x257; // vitesse
    out->ids[out->count++] = 0x118; // rapport
    out->ids[out->count++] = 0x334; // conso/puissance
    break;
  default:
    break;
  }
}

// ESP-NOW callbacks
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (!data || len < 1)
    return;
  const uint8_t *mac = recv_info ? recv_info->src_addr : NULL;
  uint8_t type       = data[0];

  if (s_role == ESP_NOW_ROLE_MASTER && type == MSG_HELLO) {
    const msg_hello_t *hello = (const msg_hello_t *)data;
    // Enregistrer les requêtes du peer
    s_requests.count         = 0;
    for (int i = 0; i < hello->req_count && i < 16; i++) {
      s_requests.ids[s_requests.count++] = hello->ids[i];
    }
    msg_hello_ack_t ack = {.type = MSG_HELLO_ACK, .status = 0, .interval_ms = 100};
    if (mac) {
      esp_now_send(mac, (const uint8_t *)&ack, sizeof(ack));
    }
  } else if (s_role == ESP_NOW_ROLE_SLAVE && type == MSG_CAN_DATA) {
    const msg_can_data_t *msg = (const msg_can_data_t *)data;
    if (s_rx_cb) {
      espnow_can_frame_t frame = {.can_id = msg->can_id, .bus = msg->bus, .dlc = msg->dlc, .ts_ms = msg->ts_ms};
      if (frame.dlc > 8)
        frame.dlc = 8;
      memcpy(frame.data, msg->data, frame.dlc);
      s_rx_cb(&frame);
    }
  }
}

static void espnow_send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  (void)tx_info;
  (void)status;
}

esp_err_t espnow_link_init(espnow_role_t role, espnow_slave_type_t slave_type) {
  s_role                 = role;
  s_slave_type           = slave_type;

  // Init Wi-Fi (STA) if not already
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();

  ESP_ERROR_CHECK(esp_now_init());
  esp_now_register_recv_cb(espnow_recv_cb);
  esp_now_register_send_cb(espnow_send_cb);

  ESP_LOGI(TAG_ESP_NOW, "Initialisation du rôle: %s", s_role);

  if (s_role == ESP_NOW_ROLE_SLAVE) {
    load_default_requests(s_slave_type, &s_requests);
    // HELLO broadcast
    uint8_t bcast[6]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    msg_hello_t hello = {0};
    hello.type        = MSG_HELLO;
    hello.slave_type  = (uint8_t)s_slave_type;
    hello.req_count   = s_requests.count;
    for (int i = 0; i < s_requests.count && i < 8; i++) {
      hello.ids[i] = s_requests.ids[i];
    }
    esp_now_send(bcast, (uint8_t *)&hello, sizeof(msg_hello_t));
  }
  return ESP_OK;
}

esp_err_t espnow_link_set_requests(const espnow_request_list_t *reqs) {
  if (!reqs)
    return ESP_ERR_INVALID_ARG;
  s_requests = *reqs;
  return ESP_OK;
}

esp_err_t espnow_link_register_peer(const uint8_t mac[6]) {
  esp_now_peer_info_t peer = {0};
  memcpy(peer.peer_addr, mac, 6);
  peer.ifidx   = ESP_IF_WIFI_STA;
  peer.channel = 0; // current channel
  peer.encrypt = false;
  if (esp_now_is_peer_exist(mac)) {
    esp_now_del_peer(mac);
  }
  return esp_now_add_peer(&peer);
}

void espnow_link_on_can_frame(const twai_message_t *msg, int bus) {
  if (s_role != ESP_NOW_ROLE_MASTER || !msg) {
    return;
  }
  // Filtrer sur les IDs demandés
  bool interested = false;
  for (int i = 0; i < s_requests.count; i++) {
    if (s_requests.ids[i] == msg->identifier) {
      interested = true;
      break;
    }
  }
  if (!interested)
    return;

  msg_can_data_t out = {0};
  out.type           = MSG_CAN_DATA;
  out.bus            = (uint8_t)bus;
  out.dlc            = msg->data_length_code;
  out.can_id         = msg->identifier;
  out.ts_ms          = (uint16_t)((esp_timer_get_time() / 1000ULL) & 0xFFFF);
  for (int i = 0; i < msg->data_length_code && i < 8; i++) {
    out.data[i] = msg->data[i];
  }
  uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(bcast, (uint8_t *)&out, sizeof(out));
}

espnow_role_t espnow_link_get_role(void) {
  return s_role;
}

espnow_slave_type_t espnow_link_get_slave_type(void) {
  return s_slave_type;
}

void espnow_link_register_rx_callback(espnow_can_rx_cb_t cb) {
  s_rx_cb = cb;
}
