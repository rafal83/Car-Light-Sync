#include "espnow_link.h"

#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_manager.h"

#include <string.h>

// Protocole minimal
// type 0x01 HELLO {mac[6], slave_type, req_count, req_ids[]}
// type 0x02 HELLO_ACK {status, interval_ms}
// type 0x03 CAN_DATA {can_id u32, bus u8, dlc u8, data[8], ts_ms u16}
// type 0x04 HEARTBEAT {type, role, slave_type}

typedef enum {
  MSG_HELLO     = 0x01,
  MSG_HELLO_ACK = 0x02,
  MSG_CAN_DATA  = 0x03,
  MSG_HEARTBEAT = 0x04,
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

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t role;
  uint8_t slave_type;
  uint8_t reserved;
} msg_heartbeat_t;

static espnow_role_t s_role             = ESP_NOW_ROLE_MASTER;
static espnow_slave_type_t s_slave_type = ESP_NOW_SLAVE_NONE;
static espnow_request_list_t s_requests = {0};
static espnow_can_rx_cb_t s_rx_cb       = NULL;
static esp_timer_handle_t s_hb_timer    = NULL;
static uint64_t s_last_peer_hb_us       = 0;
static esp_timer_handle_t s_hello_timer = NULL;

const char *espnow_link_role_to_str(espnow_role_t role) {
  return (role == ESP_NOW_ROLE_SLAVE) ? "slave" : "master";
}

const char *espnow_link_slave_type_to_str(espnow_slave_type_t type) {
  switch (type) {
  case ESP_NOW_SLAVE_BLINDSPOT_LEFT:
    return "blindspot_left";
  case ESP_NOW_SLAVE_BLINDSPOT_RIGHT:
    return "blindspot_right";
  case ESP_NOW_SLAVE_SPEEDOMETER:
    return "speedometer";
  default:
    return "none";
  }
}

bool espnow_link_role_from_str(const char *s, espnow_role_t *out) {
  if (!s || !out) {
    return false;
  }
  if (strcmp(s, "master") == 0) {
    *out = ESP_NOW_ROLE_MASTER;
    return true;
  }
  if (strcmp(s, "slave") == 0) {
    *out = ESP_NOW_ROLE_SLAVE;
    return true;
  }
  return false;
}

bool espnow_link_slave_type_from_str(const char *s, espnow_slave_type_t *out) {
  if (!s || !out) {
    return false;
  }
  if (strcmp(s, "blindspot_left") == 0) {
    *out = ESP_NOW_SLAVE_BLINDSPOT_LEFT;
    return true;
  }
  if (strcmp(s, "blindspot_right") == 0) {
    *out = ESP_NOW_SLAVE_BLINDSPOT_RIGHT;
    return true;
  }
  if (strcmp(s, "speedometer") == 0) {
    *out = ESP_NOW_SLAVE_SPEEDOMETER;
    return true;
  }
  if (strcmp(s, "none") == 0) {
    *out = ESP_NOW_SLAVE_NONE;
    return true;
  }
  return false;
}

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
  ESP_LOGI(TAG_ESP_NOW, "RX type=0x%02X len=%d from %02X:%02X:%02X:%02X:%02X:%02X",
           type, len,
           mac ? mac[0] : 0, mac ? mac[1] : 0, mac ? mac[2] : 0,
           mac ? mac[3] : 0, mac ? mac[4] : 0, mac ? mac[5] : 0);

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
    s_last_peer_hb_us = esp_timer_get_time();
    ESP_LOGI(TAG_ESP_NOW, "HELLO rx -> peer alive ts=%llu", (unsigned long long)s_last_peer_hb_us);
    if (mac) {
      espnow_link_register_peer(mac);
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
  } else if (type == MSG_HEARTBEAT) {
    s_last_peer_hb_us = esp_timer_get_time();
    ESP_LOGI(TAG_ESP_NOW, "HB rx -> peer alive ts=%llu", (unsigned long long)s_last_peer_hb_us);
    if (s_hello_timer) {
      esp_timer_stop(s_hello_timer);
    }
  }
}

static void espnow_send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  (void)tx_info;
  (void)status;
}

static void espnow_send_hello(void *arg) {
  (void)arg;
  if (!s_hello_timer) {
    return;
  }
  if (s_role != ESP_NOW_ROLE_SLAVE) {
    return;
  }
  if (s_last_peer_hb_us != 0) {
    if (s_hello_timer) {
      esp_timer_stop(s_hello_timer);
    }
    return;
  }
  uint8_t bcast[6]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  msg_hello_t hello = {0};
  hello.type        = MSG_HELLO;
  hello.slave_type  = (uint8_t)s_slave_type;
  hello.req_count   = s_requests.count;
  for (int i = 0; i < s_requests.count && i < 8; i++) {
    hello.ids[i] = s_requests.ids[i];
  }
  ESP_LOGI(TAG_ESP_NOW, "HELLO tx slave_type=%d reqs=%d", hello.slave_type, hello.req_count);
  esp_now_send(bcast, (uint8_t *)&hello, sizeof(msg_hello_t));
}

static void espnow_send_heartbeat(void *arg) {
  (void)arg;
  msg_heartbeat_t hb = {.type = MSG_HEARTBEAT, .role = (uint8_t)s_role, .slave_type = (uint8_t)s_slave_type, .reserved = 0};
  uint8_t bcast[6]    = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  ESP_LOGD(TAG_ESP_NOW, "HB tx role=%d type=%d", hb.role, hb.slave_type);
  esp_now_send(bcast, (uint8_t *)&hb, sizeof(hb));
}

esp_err_t espnow_link_init(espnow_role_t role, espnow_slave_type_t slave_type) {
  s_role                 = role;
  s_slave_type           = slave_type;
/*
  ESP_ERROR_CHECK(esp_now_init());
  esp_now_register_recv_cb(espnow_recv_cb);
  esp_now_register_send_cb(espnow_send_cb);

  // Heartbeat timer (toutes les 2s)
  const esp_timer_create_args_t hb_args = {
      .callback = espnow_send_heartbeat,
      .arg      = NULL,
      .dispatch_method = ESP_TIMER_TASK,
      .name     = "espnow_hb"};
  esp_err_t hb_err = esp_timer_create(&hb_args, &s_hb_timer);
  if (hb_err == ESP_OK && s_hb_timer) {
    esp_timer_start_periodic(s_hb_timer, 2000000);
  } else {
    ESP_LOGE(TAG_ESP_NOW, "Failed to create HB timer: %s", esp_err_to_name(hb_err));
  }

  if (s_role == ESP_NOW_ROLE_SLAVE) {
    ESP_LOGI(TAG_ESP_NOW, "Starting HELLO retry timer (slave)");
    const esp_timer_create_args_t hello_args = {
        .callback = espnow_send_hello,
        .arg      = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name     = "espnow_hello"};
    esp_err_t hello_err = esp_timer_create(&hello_args, &s_hello_timer);
    if (hello_err == ESP_OK && s_hello_timer) {
      esp_timer_start_periodic(s_hello_timer, 1000000);
    } else {
      ESP_LOGE(TAG_ESP_NOW, "Failed to create HELLO timer: %s", esp_err_to_name(hello_err));
    }
  }

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
    */
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

uint64_t espnow_link_get_last_peer_heartbeat_us(void) {
  return s_last_peer_hb_us;
}

bool espnow_link_peer_alive(uint32_t timeout_ms) {
  uint64_t now = esp_timer_get_time();
  return (s_last_peer_hb_us != 0) && ((now - s_last_peer_hb_us) <= ((uint64_t)timeout_ms * 1000ULL));
}
