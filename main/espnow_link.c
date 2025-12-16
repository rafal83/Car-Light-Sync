#include "espnow_link.h"

#include "esp_log.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "config.h"
#include "nvs_manager.h"
#include "status_led.h"
#include "status_manager.h"
#include "wifi_manager.h"

#include <string.h>

#define DISCOVERY_TIMEOUT_S 30

// Protocole
#define PROTOCOL_VERSION 1

typedef enum {
  MSG_DISCOVERY_REQ  = 0x01,
  MSG_DISCOVERY_RESP = 0x02,
  MSG_CAN_DATA       = 0x03,
  MSG_HEARTBEAT      = 0x04,
} msg_type_t;

// Sent by slave during discovery
typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t proto_ver;
  uint8_t slave_type;
  uint8_t req_count;
  uint32_t device_id;
  uint32_t req_ids[ESPNOW_MAX_REQUEST_IDS]; // requested CAN IDs
} msg_discovery_req_t;

// Sent by master in response
typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t status; // 0 = OK, 1 = Denied
  uint16_t reserved;
  uint32_t master_device_id;
} msg_discovery_resp_t;

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
static uint32_t s_device_id             = 0;
static espnow_request_list_t s_requests = {0};
static espnow_can_rx_cb_t s_rx_cb       = NULL;
static espnow_test_rx_cb_t s_test_rx_cb = NULL;
static esp_timer_handle_t s_hb_timer    = NULL;
static uint64_t s_last_peer_hb_us       = 0;
static esp_timer_handle_t s_discovery_timer = NULL;
static int s_discovery_retries = 0;
static bool s_broadcast_peer_added = false;
static bool s_slave_pairing_active = false;
static uint64_t s_last_test_rx_us = 0;
static uint8_t s_self_mac[6] = {0};

#define ESPNOW_MAX_PEERS 8
static espnow_peer_info_t s_peers[ESPNOW_MAX_PEERS];
static size_t s_peer_count = 0;

static void persist_peers_to_nvs(void) {
  struct {
    uint8_t count;
    espnow_peer_info_t peers[ESPNOW_MAX_PEERS];
  } blob = {0};
  blob.count = (s_peer_count > ESPNOW_MAX_PEERS) ? ESPNOW_MAX_PEERS : s_peer_count;
  if (blob.count) {
    memcpy(blob.peers, s_peers, blob.count * sizeof(espnow_peer_info_t));
  }
  esp_err_t err = nvs_manager_set_blob(NVS_NAMESPACE_ESPNOW, "peers", &blob, sizeof(blob));
  if (err != ESP_OK) {
    ESP_LOGW(TAG_ESP_NOW, "Failed to persist peers to NVS: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG_ESP_NOW, "Persisted %u peer(s) to NVS", (unsigned int)blob.count);
  }
}

static void load_peers_from_nvs(void) {
  size_t required = 0;
  esp_err_t err = nvs_manager_get_blob(NVS_NAMESPACE_ESPNOW, "peers", NULL, &required);
  struct {
    uint8_t count;
    espnow_peer_info_t peers[ESPNOW_MAX_PEERS];
  } blob = {0};
  size_t max_expected = sizeof(blob);
  if (err != ESP_OK || required == 0 || required > max_expected) {
    s_peer_count = 0;
    ESP_LOGI(TAG_ESP_NOW, "No peers in NVS (err=%s, size=%u, max=%u)",
             esp_err_to_name(err), (unsigned int)required, (unsigned int)max_expected);
    return;
  }
  err = nvs_manager_get_blob(NVS_NAMESPACE_ESPNOW, "peers", &blob, &required);
  if (err == ESP_OK) {
    s_peer_count = (blob.count > ESPNOW_MAX_PEERS) ? ESPNOW_MAX_PEERS : blob.count;
    if (s_peer_count) {
      memcpy(s_peers, blob.peers, s_peer_count * sizeof(espnow_peer_info_t));
      for (size_t i = 0; i < s_peer_count; i++) {
        if (s_peers[i].role != ESP_NOW_ROLE_MASTER && s_peers[i].role != ESP_NOW_ROLE_SLAVE) {
          s_peers[i].role = (s_role == ESP_NOW_ROLE_MASTER) ? ESP_NOW_ROLE_SLAVE : ESP_NOW_ROLE_MASTER;
        }
      }
    }
    ESP_LOGI(TAG_ESP_NOW, "Loaded %u peer(s) from NVS", (unsigned int)s_peer_count);
  } else {
    s_peer_count = 0;
    ESP_LOGW(TAG_ESP_NOW, "Failed to load peers from NVS: %s", esp_err_to_name(err));
  }
}

// Pairing mode
static bool s_is_pairing_mode              = false;
static esp_timer_handle_t s_pairing_mode_timer = NULL;


const char *espnow_link_role_to_str(espnow_role_t role) {
  return (role == ESP_NOW_ROLE_SLAVE) ? "slave" : "master";
}

const char *espnow_link_slave_type_to_str(espnow_slave_type_t type) {
  switch (type) {
  case ESP_NOW_SLAVE_EVENTS_LEFT:
    return "events_left";
  case ESP_NOW_SLAVE_EVENTS_RIGHT:
    return "events_right";
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
  if (strcmp(s, "events_left") == 0) {
    *out = ESP_NOW_SLAVE_EVENTS_LEFT;
    return true;
  }
  if (strcmp(s, "events_right") == 0) {
    *out = ESP_NOW_SLAVE_EVENTS_RIGHT;
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

static void add_request_id(espnow_request_list_t *out, uint32_t id) {
  if (!out) return;
  if (out->count >= ESPNOW_MAX_REQUEST_IDS) return;
  out->ids[out->count++] = id;
}

// Table simplifiée des IDs par type d'esclave (limitée pour éviter le flux excessif)
static void load_default_requests(espnow_slave_type_t type, espnow_request_list_t *out) {
  memset(out, 0, sizeof(*out));
  // IDs traités dans vehicle_state_apply_signal (liste exhaustive utilisée pour limiter le flux)
  const uint32_t base_ids[] = {
      0x3C2, // scroll wheels / steering buttons
      0x257, // vitesse
      0x118, // gear / accel
      0x39D, // brake
      0x3F3, // odometer
      0x102, // portes G
      0x103, // portes D + trunk
      0x2E1, // frunk
      0x3F5, // lights/turns
      0x399, // blindspot/autopilot/lane/side
      0x313, // speed limit
      0x292, // SOC
      0x132, // HV voltage
      0x261, // 12V voltage
      0x204, // charge status
      0x273, // lock/night mode
      0x284, // cable presence
      0x212, // charge status (UI)
      0x25D  // charge door
  };

  for (size_t i = 0; i < sizeof(base_ids) / sizeof(base_ids[0]); i++) {
    add_request_id(out, base_ids[i]);
  }

  // Pas d'ajout spécifique par type pour limiter le flux : la liste couvre les frames utilisées
}

static void pairing_mode_timer_callback(void *arg) {
    ESP_LOGI(TAG_ESP_NOW, "Pairing mode disabled by timer");
    s_is_pairing_mode = false;
    status_manager_update_led_now();
}

// Prototypes
static uint8_t get_current_channel(void);
static void espnow_send_discovery_request(void *arg);
static void update_peer_info(const uint8_t mac[6], espnow_role_t role, espnow_slave_type_t type, uint32_t device_id, uint8_t channel_hint);
static void remove_peer_from_cache(const uint8_t mac[6]);
static void register_cached_peers(void);
static bool is_peer_known(const uint8_t mac[6], uint32_t device_id);
static esp_err_t load_self_mac(void);

static uint8_t get_current_channel(void) {
  uint8_t primary_channel = 0;
  wifi_second_chan_t second_channel;
  if (esp_wifi_get_channel(&primary_channel, &second_channel) != ESP_OK) {
    primary_channel = 0;
  }
  return primary_channel;
}

static esp_err_t load_self_mac(void) {
  wifi_mode_t wifi_mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&wifi_mode) != ESP_OK) {
    wifi_mode = WIFI_MODE_APSTA;
  }
  wifi_interface_t ifx = (wifi_mode == WIFI_MODE_STA || wifi_mode == WIFI_MODE_APSTA) ? WIFI_IF_STA : WIFI_IF_AP;
  esp_err_t err = esp_wifi_get_mac(ifx, s_self_mac);
  if (err != ESP_OK) {
    ESP_LOGW(TAG_ESP_NOW, "Failed to get MAC: %s", esp_err_to_name(err));
  }
  return err;
}

// Re-register peers stored in NVS into esp_now peer table
static void register_cached_peers(void) {
  if (s_peer_count == 0) return;

  ESP_LOGI(TAG_ESP_NOW, "Restoring %u cached peer(s)", (unsigned int)s_peer_count);
  uint8_t current_channel = get_current_channel();
  // For slave, if we have a stored master channel, try to switch to it before registering
  if (s_role == ESP_NOW_ROLE_SLAVE) {
    for (size_t i = 0; i < s_peer_count; i++) {
      if (s_peers[i].channel > 0 && s_peers[i].channel != current_channel) {
        esp_err_t ch_ret = esp_wifi_set_channel(s_peers[i].channel, WIFI_SECOND_CHAN_NONE);
        if (ch_ret == ESP_OK) {
          current_channel = s_peers[i].channel;
          ESP_LOGI(TAG_ESP_NOW, "SLAVE: restored channel %u from cache", current_channel);
        } else {
          ESP_LOGW(TAG_ESP_NOW, "SLAVE: failed to set channel %u from cache: %s", s_peers[i].channel, esp_err_to_name(ch_ret));
        }
        break;
      }
    }
  }

  for (size_t i = 0; i < s_peer_count; i++) {
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, s_peers[i].mac, 6);

    wifi_mode_t wifi_mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&wifi_mode) != ESP_OK) {
      wifi_mode = WIFI_MODE_APSTA;
    }
    bool sta_active = (wifi_mode == WIFI_MODE_STA || wifi_mode == WIFI_MODE_APSTA);
    peer.ifidx     = sta_active ? ESP_IF_WIFI_STA : ESP_IF_WIFI_AP;

    peer.channel = s_peers[i].channel ? s_peers[i].channel : current_channel;
    peer.encrypt = false;

    esp_err_t add_ret = esp_now_add_peer(&peer);
    if (add_ret != ESP_OK && add_ret != ESP_ERR_ESPNOW_EXIST) {
      ESP_LOGW(TAG_ESP_NOW, "Failed to restore peer %02X:%02X:%02X:%02X:%02X:%02X: %s",
               peer.peer_addr[0], peer.peer_addr[1], peer.peer_addr[2],
               peer.peer_addr[3], peer.peer_addr[4], peer.peer_addr[5],
               esp_err_to_name(add_ret));
    } else {
      ESP_LOGI(TAG_ESP_NOW, "Restored peer %02X:%02X:%02X:%02X:%02X:%02X (ch=%d)",
               peer.peer_addr[0], peer.peer_addr[1], peer.peer_addr[2],
               peer.peer_addr[3], peer.peer_addr[4], peer.peer_addr[5],
               peer.channel);
    }
  }
}

static bool is_peer_known(const uint8_t mac[6], uint32_t device_id) {
  for (size_t i = 0; i < s_peer_count; i++) {
    if (mac && memcmp(s_peers[i].mac, mac, 6) == 0) {
      return true;
    }
    if (device_id != 0 && s_peers[i].device_id == device_id) {
      return true;
    }
  }
  if (mac && esp_now_is_peer_exist(mac)) {
    return true;
  }
  return false;
}

static void update_peer_info(const uint8_t mac[6], espnow_role_t role, espnow_slave_type_t type, uint32_t device_id, uint8_t channel_hint) {
  if (!mac) return;
  uint8_t channel = channel_hint ? channel_hint : get_current_channel();
  // search existing
  for (size_t i = 0; i < s_peer_count; i++) {
    if (memcmp(s_peers[i].mac, mac, 6) == 0) {
      bool changed = false;
      if (s_peers[i].role != role) {
        s_peers[i].role = role;
        changed = true;
      }
      if (s_peers[i].type != type) {
        s_peers[i].type = type;
        changed = true;
      }
      if (device_id != 0 && s_peers[i].device_id != device_id) {
        s_peers[i].device_id = device_id;
        changed = true;
      }
      if (s_peers[i].channel != channel) {
        s_peers[i].channel = channel;
        changed = true;
      }
      s_peers[i].last_seen_us = esp_timer_get_time();
      if (changed) {
        persist_peers_to_nvs();
      }
      return;
    }
  }
  if (s_peer_count >= ESPNOW_MAX_PEERS) {
    // overwrite the oldest
    size_t oldest = 0;
    for (size_t i = 1; i < s_peer_count; i++) {
      if (s_peers[i].last_seen_us < s_peers[oldest].last_seen_us) {
        oldest = i;
      }
    }
    memcpy(s_peers[oldest].mac, mac, 6);
    s_peers[oldest].role = role;
    s_peers[oldest].type = type;
    s_peers[oldest].device_id = device_id;
    s_peers[oldest].last_seen_us = esp_timer_get_time();
    s_peers[oldest].channel = channel;
    return;
  }
  memcpy(s_peers[s_peer_count].mac, mac, 6);
  s_peers[s_peer_count].role = role;
  s_peers[s_peer_count].type = type;
  s_peers[s_peer_count].device_id = device_id;
  s_peers[s_peer_count].last_seen_us = esp_timer_get_time();
  s_peers[s_peer_count].channel = channel;
  s_peer_count++;
  persist_peers_to_nvs();
}

static void remove_peer_from_cache(const uint8_t mac[6]) {
  if (!mac) return;
  for (size_t i = 0; i < s_peer_count; i++) {
    if (memcmp(s_peers[i].mac, mac, 6) == 0) {
      if (i != s_peer_count - 1) {
        s_peers[i] = s_peers[s_peer_count - 1];
      }
      s_peer_count--;
      persist_peers_to_nvs();
      break;
    }
  }
}

// ESP-NOW callbacks
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (!data || len < 1)
    return;
  const uint8_t *mac = recv_info ? recv_info->src_addr : NULL;
  uint8_t type       = data[0];
  ESP_LOGD(TAG_ESP_NOW, "RX type=0x%02X len=%d from %02X:%02X:%02X:%02X:%02X:%02X",
           type, len,
           mac ? mac[0] : 0, mac ? mac[1] : 0, mac ? mac[2] : 0,
           mac ? mac[3] : 0, mac ? mac[4] : 0, mac ? mac[5] : 0);

  if (s_role == ESP_NOW_ROLE_MASTER && type == MSG_DISCOVERY_REQ) {
    if (len < 8) {
        ESP_LOGW(TAG_ESP_NOW, "Discovery request too short: len=%d", len);
        return;
    }
    const msg_discovery_req_t *req = (const msg_discovery_req_t *)data;
    if (req->proto_ver != PROTOCOL_VERSION) {
        ESP_LOGW(TAG_ESP_NOW, "Discovery request with wrong protocol version: %d", req->proto_ver);
        return;
    }
    bool known = is_peer_known(mac, req->device_id);
    if (!s_is_pairing_mode && !known) {
        ESP_LOGW(TAG_ESP_NOW, "Master not in pairing mode, ignoring discovery request (unknown peer) count=%u", (unsigned int)s_peer_count);
        return;
    }

    ESP_LOGI(TAG_ESP_NOW, "Discovery request received from slave id=0x%08X", (unsigned int)req->device_id);
    if (mac) {
      update_peer_info(mac, ESP_NOW_ROLE_SLAVE, (espnow_slave_type_t)req->slave_type, req->device_id, 0);
    }
    uint8_t max_ids_from_len = (len > 8) ? (uint8_t)((len - 8) / 4) : 0;
    if (max_ids_from_len > ESPNOW_MAX_REQUEST_IDS) {
      max_ids_from_len = ESPNOW_MAX_REQUEST_IDS;
    }
    s_requests.count = (req->req_count > max_ids_from_len) ? max_ids_from_len : req->req_count;
    for (uint8_t i = 0; i < s_requests.count; i++) {
      s_requests.ids[i] = req->req_ids[i];
    }

    msg_discovery_resp_t resp = {
        .type = MSG_DISCOVERY_RESP, 
        .status = 0, 
        .master_device_id = s_device_id
    };
    if (mac) {
      espnow_link_register_peer(mac);
      esp_now_send(mac, (const uint8_t *)&resp, sizeof(resp));
    }
    
    espnow_link_set_pairing_mode(false, 0);

  } else if (s_role == ESP_NOW_ROLE_SLAVE && type == MSG_DISCOVERY_RESP && len == sizeof(msg_discovery_resp_t)) {
    const msg_discovery_resp_t* resp = (const msg_discovery_resp_t*)data;
    ESP_LOGI(TAG_ESP_NOW, "Discovery response received from master id=0x%08X", (unsigned int)resp->master_device_id);
    s_last_peer_hb_us = esp_timer_get_time();
    if (mac) {
      espnow_link_register_peer(mac);
      update_peer_info(mac, ESP_NOW_ROLE_MASTER, ESP_NOW_SLAVE_NONE, resp->master_device_id, 0);
    }
    ESP_LOGI(TAG_ESP_NOW, "Paired with master, stopping discovery timer");
    if (s_discovery_timer && esp_timer_is_active(s_discovery_timer)) {
        esp_timer_stop(s_discovery_timer);
    }
    s_slave_pairing_active = false;
    status_manager_update_led_now();

  } else if (s_role == ESP_NOW_ROLE_SLAVE && type == MSG_CAN_DATA) {
    const msg_can_data_t *msg = (const msg_can_data_t *)data;
    if (s_rx_cb) {
      espnow_can_frame_t frame = {.can_id = msg->can_id, .bus = msg->bus, .dlc = msg->dlc, .ts_ms = msg->ts_ms};
      if (frame.dlc > 8)
        frame.dlc = 8;
      memcpy(frame.data, msg->data, frame.dlc);
      s_rx_cb(&frame);
    }
    // If it's the test pattern (DE AD BE EF), notify test callback
    if (s_test_rx_cb && len >= sizeof(msg_can_data_t) &&
        msg->dlc == 4 &&
        msg->data[0] == 0xDE && msg->data[1] == 0xAD &&
        msg->data[2] == 0xBE && msg->data[3] == 0xEF) {
      s_last_test_rx_us = esp_timer_get_time();
      ESP_LOGI(TAG_ESP_NOW, "Test frame received from %02X:%02X:%02X:%02X:%02X:%02X",
               mac ? mac[0] : 0, mac ? mac[1] : 0, mac ? mac[2] : 0,
               mac ? mac[3] : 0, mac ? mac[4] : 0, mac ? mac[5] : 0);
      s_test_rx_cb();
    }
  } else if (type == MSG_HEARTBEAT) {
    // If slave has no peers (after a manual disconnect), ignore heartbeats
    if (s_role == ESP_NOW_ROLE_SLAVE && s_peer_count == 0) {
      return;
    }
    s_last_peer_hb_us = esp_timer_get_time();
    ESP_LOGD(TAG_ESP_NOW, "HB rx -> peer alive ts=%llu", (unsigned long long)s_last_peer_hb_us);
    const msg_heartbeat_t *hb = (const msg_heartbeat_t *)data;
    if (mac && hb) {
      espnow_role_t peer_role = (hb->role <= ESP_NOW_ROLE_SLAVE) ? (espnow_role_t)hb->role : ESP_NOW_ROLE_SLAVE;
      update_peer_info(mac, peer_role, (espnow_slave_type_t)hb->slave_type, 0, 0);
    }
    if (s_role == ESP_NOW_ROLE_SLAVE && s_discovery_timer && esp_timer_is_active(s_discovery_timer)) {
      esp_timer_stop(s_discovery_timer);
      ESP_LOGI(TAG_ESP_NOW, "Slave paired, stopping discovery timer");
      s_slave_pairing_active = false;
      status_manager_update_led_now();
    }
  }
}

static void espnow_send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    ESP_LOGD(TAG_ESP_NOW, "ESP-NOW send success");
  } else {
    ESP_LOGW(TAG_ESP_NOW, "ESP-NOW send failed (status=%d)", (int)status);
  }
}

static void espnow_send_discovery_request(void *arg) {
  (void)arg;
  s_discovery_retries++;
  if (s_role != ESP_NOW_ROLE_SLAVE || !s_discovery_timer) {
    return;
  }
  // Stop trying if we are paired or timed out
  if (espnow_link_peer_alive(10000)) {
    esp_timer_stop(s_discovery_timer);
    ESP_LOGI(TAG_ESP_NOW, "Slave paired, stopping discovery timer");
    s_slave_pairing_active = false;
    status_manager_update_led_now();
    return;
  }
  
  if (s_discovery_retries * 2 > DISCOVERY_TIMEOUT_S) {
    esp_timer_stop(s_discovery_timer);
    ESP_LOGW(TAG_ESP_NOW, "Slave discovery timed out, no master found");
    s_slave_pairing_active = false;
    status_manager_update_led_now();
    return;
  }

  uint8_t bcast[6]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  msg_discovery_req_t req = {
    .type = MSG_DISCOVERY_REQ,
    .proto_ver = PROTOCOL_VERSION,
    .slave_type = (uint8_t)s_slave_type,
    .device_id = s_device_id,
    .req_count = 0
  };
  uint8_t req_count = s_requests.count > ESPNOW_MAX_REQUEST_IDS ? ESPNOW_MAX_REQUEST_IDS : s_requests.count;
  req.req_count = req_count;
  for (uint8_t i = 0; i < req_count; i++) {
    req.req_ids[i] = s_requests.ids[i];
  }
  size_t payload_len = 8 + ((size_t)req_count * sizeof(uint32_t));
  ESP_LOGI(TAG_ESP_NOW, "Broadcasting discovery request (try %d)", s_discovery_retries);
  esp_err_t err = esp_now_send(bcast, (uint8_t *)&req, payload_len);
  if (err != ESP_OK) {
    ESP_LOGW(TAG_ESP_NOW, "esp_now_send discovery failed: %s", esp_err_to_name(err));
  }
}

static void espnow_send_heartbeat(void *arg) {
  (void)arg;
  if (s_role == ESP_NOW_ROLE_SLAVE && s_peer_count == 0) {
    // Unpaired slave: ne pas émettre de heartbeat pour éviter de se re-announcer au master
    return;
  }
  msg_heartbeat_t hb = {.type = MSG_HEARTBEAT, .role = (uint8_t)s_role, .slave_type = (uint8_t)s_slave_type, .reserved = 0};
  uint8_t bcast[6]    = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  ESP_LOGD(TAG_ESP_NOW, "HB tx role=%d type=%d", hb.role, hb.slave_type);
  esp_now_send(bcast, (uint8_t *)&hb, sizeof(hb));
}

esp_err_t espnow_link_init(espnow_role_t role, espnow_slave_type_t slave_type) {
  s_role                 = role;
  s_slave_type           = slave_type;
  s_device_id            = esp_random();

  // Charger les pairs persistés
  load_peers_from_nvs();
  load_self_mac();

  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
  ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

  // Register broadcast peer once (needed on some IDF versions to allow FF:FF:FF:FF:FF:FF sends)
  if (!s_broadcast_peer_added) {
    esp_now_peer_info_t peer = {0};
    memset(peer.peer_addr, 0xFF, 6);
    peer.ifidx   = ESP_IF_WIFI_STA;
    peer.channel = 0; // use current channel
    peer.encrypt = false;
    esp_err_t p = esp_now_add_peer(&peer);
    if (p == ESP_OK || p == ESP_ERR_ESPNOW_EXIST) {
      s_broadcast_peer_added = true;
    } else {
      ESP_LOGW(TAG_ESP_NOW, "Failed to add broadcast peer: %s", esp_err_to_name(p));
    }
  }

  if (s_role == ESP_NOW_ROLE_MASTER) {
    const esp_timer_create_args_t pairing_timer_args = {
        .callback = &pairing_mode_timer_callback,
        .name = "espnow_pairing"
    };
    ESP_ERROR_CHECK(esp_timer_create(&pairing_timer_args, &s_pairing_mode_timer));
  }

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

  // Restaurer les peers enregistrés en NVS (master & slave)
  register_cached_peers();

  if (s_role == ESP_NOW_ROLE_SLAVE) {
    // Charger les IDs CAN par défaut pour ce type d'esclave
    load_default_requests(s_slave_type, &s_requests);

    const esp_timer_create_args_t discovery_args = {
        .callback = espnow_send_discovery_request,
        .arg      = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name     = "espnow_discovery"};
    esp_err_t discovery_err = esp_timer_create(&discovery_args, &s_discovery_timer);
    if (discovery_err != ESP_OK) {
      ESP_LOGE(TAG_ESP_NOW, "Failed to create discovery timer: %s", esp_err_to_name(discovery_err));
    }
  }

  // Restaurer les peers enregistrés en NVS (master & slave)
  register_cached_peers();

  // Auto-relaunch slave discovery on boot if not connected
  if (s_role == ESP_NOW_ROLE_SLAVE && !espnow_link_peer_alive(5000) && s_discovery_timer) {
    espnow_link_trigger_slave_pairing();
  }

  ESP_LOGI(TAG_ESP_NOW, "Initialised role: %s, device_id: 0x%08X", espnow_link_role_to_str(s_role), (unsigned int)s_device_id);

  return ESP_OK;
}

esp_err_t espnow_link_set_pairing_mode(bool enable, uint32_t duration_s) {
    if (s_role != ESP_NOW_ROLE_MASTER) {
        ESP_LOGE(TAG_ESP_NOW, "Only master can enter pairing mode");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (enable) {
        uint8_t primary_channel;
        wifi_second_chan_t second_channel;
        esp_wifi_get_channel(&primary_channel, &second_channel);
        ESP_LOGI(TAG_ESP_NOW, "MASTER: Starting pairing on current Wi-Fi channel: %d", primary_channel);

        int target_channel = 0;
        wifi_ap_record_t ap_info;
        bool sta_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
        if (target_channel == 0 && sta_connected) {
            target_channel = ap_info.primary;
            ESP_LOGI(TAG_ESP_NOW, "MASTER: STA connected, using AP channel %d for ESP-NOW", target_channel);
        } else if (target_channel > 0) {
            ESP_LOGI(TAG_ESP_NOW, "MASTER: Using fixed channel %d for ESP-NOW", target_channel);
        }
        if (target_channel > 0 && primary_channel != target_channel) {
            esp_err_t ch_ret = esp_wifi_set_channel(target_channel, WIFI_SECOND_CHAN_NONE);
            if (ch_ret == ESP_OK) {
                ESP_LOGW(TAG_ESP_NOW, "MASTER: Forcing ESP-NOW channel to %d for pairing", target_channel);
            } else {
                ESP_LOGW(TAG_ESP_NOW, "MASTER: Failed to set channel to %d: %s", target_channel, esp_err_to_name(ch_ret));
            }
        }
        esp_wifi_get_channel(&primary_channel, &second_channel);
        ESP_LOGI(TAG_ESP_NOW, "MASTER: Pairing will run on Wi-Fi channel: %d", primary_channel);

        s_is_pairing_mode = true;
        status_led_set_state(STATUS_LED_ESPNOW_PAIRING);
        if (duration_s > 0) {
            ESP_ERROR_CHECK(esp_timer_start_once(s_pairing_mode_timer, duration_s * 1000000));
        }
    } else {
        ESP_LOGI(TAG_ESP_NOW, "Pairing mode disabled");
        s_is_pairing_mode = false;
        status_manager_update_led_now();
        if (esp_timer_is_active(s_pairing_mode_timer)) {
            esp_timer_stop(s_pairing_mode_timer);
        }
    }
    return ESP_OK;
}

esp_err_t espnow_link_trigger_slave_pairing(void) {
    if (s_role != ESP_NOW_ROLE_SLAVE) {
        s_slave_pairing_active = false;
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (s_discovery_timer == NULL) {
        ESP_LOGE(TAG_ESP_NOW, "Discovery timer not initialized");
        s_slave_pairing_active = false;
        return ESP_FAIL;
    }
    if (esp_timer_is_active(s_discovery_timer)) {
        ESP_LOGI(TAG_ESP_NOW, "Slave pairing broadcast already active");
        s_slave_pairing_active = true;
        return ESP_OK;
    }
    
    uint8_t primary_channel;
    wifi_second_chan_t second_channel;
    esp_wifi_get_channel(&primary_channel, &second_channel);
    ESP_LOGI(TAG_ESP_NOW, "SLAVE: Starting pairing on current Wi-Fi channel: %d", primary_channel);

    int target_channel = 0;
    wifi_ap_record_t ap_info;
    bool sta_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    if (target_channel == 0 && sta_connected) {
        target_channel = ap_info.primary;
        ESP_LOGI(TAG_ESP_NOW, "SLAVE: STA connected, using AP channel %d for ESP-NOW", target_channel);
    } else if (target_channel > 0) {
        ESP_LOGI(TAG_ESP_NOW, "SLAVE: Using fixed channel %d for ESP-NOW", target_channel);
    }
    if (target_channel > 0 && primary_channel != target_channel) {
        esp_err_t ch_ret = esp_wifi_set_channel(target_channel, WIFI_SECOND_CHAN_NONE);
        if (ch_ret == ESP_OK) {
            ESP_LOGW(TAG_ESP_NOW, "SLAVE: Forcing ESP-NOW channel to %d for pairing", target_channel);
        } else {
            ESP_LOGW(TAG_ESP_NOW, "SLAVE: Failed to set channel to %d: %s", target_channel, esp_err_to_name(ch_ret));
        }
    }
    esp_wifi_get_channel(&primary_channel, &second_channel);
    ESP_LOGI(TAG_ESP_NOW, "SLAVE: Pairing will run on Wi-Fi channel: %d", primary_channel);

    s_discovery_retries = 0;
    s_slave_pairing_active = true;
    status_led_set_state(STATUS_LED_ESPNOW_PAIRING);

    // Start broadcasting immediately, then every 2 seconds
    espnow_send_discovery_request(NULL);
    esp_err_t start_err = esp_timer_start_periodic(s_discovery_timer, 2000000);
    if (start_err != ESP_OK) {
      s_slave_pairing_active = false;
    }
    return start_err;
}

esp_err_t espnow_link_set_requests(const espnow_request_list_t *reqs) {
  if (!reqs) {
    return ESP_ERR_INVALID_ARG;
  }
  uint8_t count = (reqs->count > ESPNOW_MAX_REQUEST_IDS) ? ESPNOW_MAX_REQUEST_IDS : reqs->count;
  s_requests.count = count;
  for (uint8_t i = 0; i < count; i++) {
    s_requests.ids[i] = reqs->ids[i];
  }
  return ESP_OK;
}

esp_err_t espnow_link_register_peer(const uint8_t mac[6]) {
  esp_now_peer_info_t peer = {0};
  memcpy(peer.peer_addr, mac, 6);
  wifi_mode_t wifi_mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&wifi_mode) != ESP_OK) {
    wifi_mode = WIFI_MODE_APSTA;
  }
  bool sta_active = (wifi_mode == WIFI_MODE_STA || wifi_mode == WIFI_MODE_APSTA);
  peer.ifidx     = sta_active ? ESP_IF_WIFI_STA : ESP_IF_WIFI_AP; // prefer STA interface when connected to external AP

  uint8_t primary_channel = 0;
  wifi_second_chan_t second_channel;
  if (esp_wifi_get_channel(&primary_channel, &second_channel) != ESP_OK) {
    primary_channel = 0;
  }
  peer.channel = primary_channel; // lock to current channel to avoid hopping between AP/STA contexts
  peer.encrypt = false;
  
  if (esp_now_is_peer_exist(mac)) {
    ESP_LOGI(TAG_ESP_NOW, "Peer %02X:%02X:%02X:%02X:%02X:%02X already exists, updating (ifidx=%d ch=%d).", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5], peer.ifidx, peer.channel);
    return esp_now_mod_peer(&peer);
  }

  ESP_LOGI(TAG_ESP_NOW, "Adding peer %02X:%02X:%02X:%02X:%02X:%02X on if=%d channel=%d", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5], peer.ifidx, peer.channel);
  esp_err_t add_ret = esp_now_add_peer(&peer);
  if (add_ret == ESP_OK) {
    espnow_role_t peer_role = (s_role == ESP_NOW_ROLE_MASTER) ? ESP_NOW_ROLE_SLAVE : ESP_NOW_ROLE_MASTER;
    update_peer_info(mac, peer_role, ESP_NOW_SLAVE_NONE, 0, peer.channel);
  }
  return add_ret;
}

void espnow_link_on_can_frame(const twai_message_t *msg, int bus) {
  // This function is now master-only and doesn't need a role check
  if (!msg) {
    return;
  }
  
  if (s_requests.count > 0) {
    bool interested = false;
    for (uint8_t i = 0; i < s_requests.count && i < ESPNOW_MAX_REQUEST_IDS; i++) {
      if (s_requests.ids[i] == msg->identifier) {
        interested = true;
        break;
      }
    }
    if (!interested) {
      return;
    }
  }

  msg_can_data_t out = {0};
  out.type           = MSG_CAN_DATA;
  out.bus            = (uint8_t)bus;
  out.dlc            = msg->data_length_code;
  out.can_id         = msg->identifier;
  out.ts_ms          = (uint16_t)((esp_timer_get_time() / 1000ULL) & 0xFFFF);
  if(out.dlc > 8) out.dlc = 8;
  memcpy(out.data, msg->data, out.dlc);
  
  esp_now_peer_info_t peer = {0};
    for (esp_err_t ret = esp_now_fetch_peer(true, &peer); ret == ESP_OK; ret = esp_now_fetch_peer(false, &peer)) {
        esp_now_send(peer.peer_addr, (const uint8_t *)&out, sizeof(out));
    }
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

void espnow_link_register_test_rx_callback(espnow_test_rx_cb_t cb) {
  s_test_rx_cb = cb;
}

uint64_t espnow_link_get_last_peer_heartbeat_us(void) {
  return s_last_peer_hb_us;
}

bool espnow_link_peer_alive(uint32_t timeout_ms) {
  uint64_t now = esp_timer_get_time();
  return (s_last_peer_hb_us != 0) && ((now - s_last_peer_hb_us) <= ((uint64_t)timeout_ms * 1000ULL));
}

bool espnow_link_is_connected(void) {
  return espnow_link_peer_alive(5000);
}

esp_err_t espnow_link_disconnect(void) {
  if (s_role != ESP_NOW_ROLE_SLAVE) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  // Clear peer list and NVS
  esp_now_peer_info_t peer = {0};
  for (esp_err_t ret = esp_now_fetch_peer(true, &peer); ret == ESP_OK; ret = esp_now_fetch_peer(false, &peer)) {
    esp_now_del_peer(peer.peer_addr);
    remove_peer_from_cache(peer.peer_addr);
  }
  s_peer_count = 0;
  persist_peers_to_nvs();
  s_last_peer_hb_us = 0;
  if (s_discovery_timer && esp_timer_is_active(s_discovery_timer)) {
    esp_timer_stop(s_discovery_timer);
  }
  s_slave_pairing_active = false;
  status_manager_update_led_now();
  return ESP_OK;
}

esp_err_t espnow_link_disconnect_peer(const uint8_t mac[6]) {
  if (s_role != ESP_NOW_ROLE_MASTER || !mac) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  if (esp_now_is_peer_exist(mac)) {
    esp_now_del_peer(mac);
  }
  remove_peer_from_cache(mac);
  persist_peers_to_nvs();
  return ESP_OK;
}

esp_err_t espnow_link_get_peers(espnow_peer_info_t *out_peers, size_t max_peers, size_t *out_count) {
  if (out_count) {
    *out_count = s_peer_count;
  }
  if (!out_peers || max_peers == 0) {
    return ESP_OK;
  }
  size_t copy_count = (s_peer_count < max_peers) ? s_peer_count : max_peers;
  memcpy(out_peers, s_peers, copy_count * sizeof(espnow_peer_info_t));
  if (out_count) {
    *out_count = copy_count;
  }
  return ESP_OK;
}

esp_err_t espnow_link_send_test_frame(const uint8_t mac[6]) {
  if (s_role != ESP_NOW_ROLE_MASTER) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  if (mac && !esp_now_is_peer_exist(mac)) {
    // Try to register the peer on the fly if it's known in cache
    espnow_link_register_peer(mac);
    if (!esp_now_is_peer_exist(mac)) {
      return ESP_ERR_NOT_FOUND;
    }
  }
  msg_can_data_t out = {0};
  out.type = MSG_CAN_DATA;
  out.bus = 0;
  out.dlc = 4;
  out.can_id = 0x123;
  out.ts_ms = (uint16_t)((esp_timer_get_time() / 1000ULL) & 0xFFFF);
  out.data[0] = 0xDE;
  out.data[1] = 0xAD;
  out.data[2] = 0xBE;
  out.data[3] = 0xEF;

  if (mac) {
    return esp_now_send(mac, (const uint8_t *)&out, sizeof(out));
  }

  esp_now_peer_info_t peer = {0};
  for (esp_err_t ret = esp_now_fetch_peer(true, &peer); ret == ESP_OK; ret = esp_now_fetch_peer(false, &peer)) {
    esp_now_send(peer.peer_addr, (const uint8_t *)&out, sizeof(out));
  }
  return ESP_OK;
}

uint8_t espnow_link_get_channel(void) {
  return get_current_channel();
}

bool espnow_link_is_pairing_active(void) {
  if (s_role == ESP_NOW_ROLE_MASTER) {
    bool timer_active = s_pairing_mode_timer && esp_timer_is_active(s_pairing_mode_timer);
    return s_is_pairing_mode || timer_active;
  }
  if (s_role == ESP_NOW_ROLE_SLAVE) {
    bool timer_active = s_discovery_timer && esp_timer_is_active(s_discovery_timer);
    return s_slave_pairing_active || timer_active;
  }
  return false;
}

bool espnow_link_get_master_info(espnow_peer_info_t *out_master) {
  if (!out_master || s_role != ESP_NOW_ROLE_SLAVE) {
    return false;
  }
  for (size_t i = 0; i < s_peer_count; i++) {
    if (s_peers[i].role == ESP_NOW_ROLE_MASTER) {
      memcpy(out_master, &s_peers[i], sizeof(espnow_peer_info_t));
      return true;
    }
  }
  if (s_peer_count > 0) {
    memcpy(out_master, &s_peers[0], sizeof(espnow_peer_info_t));
    return true;
  }
  return false;
}

uint64_t espnow_link_get_last_test_rx_us(void) {
  return s_last_test_rx_us;
}

void espnow_link_set_role_type(espnow_role_t role, espnow_slave_type_t type) {
  s_role = role;
  s_slave_type = type;

  if (role == ESP_NOW_ROLE_MASTER) {
    // Stop any slave discovery state
    if (s_discovery_timer && esp_timer_is_active(s_discovery_timer)) {
      esp_timer_stop(s_discovery_timer);
    }
    s_slave_pairing_active = false;

    // Ensure pairing timer exists for master
    if (!s_pairing_mode_timer) {
      const esp_timer_create_args_t pairing_timer_args = {
          .callback = &pairing_mode_timer_callback,
          .name = "espnow_pairing"
      };
      esp_timer_create(&pairing_timer_args, &s_pairing_mode_timer);
    }
  } else {
    // Switching to slave: stop pairing mode if active
    if (s_pairing_mode_timer && esp_timer_is_active(s_pairing_mode_timer)) {
      esp_timer_stop(s_pairing_mode_timer);
    }
    s_is_pairing_mode = false;
    load_default_requests(type, &s_requests);
  }
}

esp_err_t espnow_link_get_mac(uint8_t mac_out[6]) {
  if (!mac_out) return ESP_ERR_INVALID_ARG;
  if (s_self_mac[0] == 0 && s_self_mac[1] == 0) {
    load_self_mac();
  }
  memcpy(mac_out, s_self_mac, 6);
  return ESP_OK;
}

uint32_t espnow_link_get_device_id(void) {
  return s_device_id;
}
