#include "espnow_link.h"

#include "ble_api_service.h"
#include "config.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "spiffs_storage.h"
#include "status_led.h"
#include "status_manager.h"
#include "vehicle_can_unified.h"
#include "wifi_manager.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#define DISCOVERY_TIMEOUT_S 30

// Protocol
#define PROTOCOL_VERSION 1

typedef enum {
  MSG_DISCOVERY_REQ  = 0x01,
  MSG_DISCOVERY_RESP = 0x02,
  MSG_TEST           = 0x03,
  MSG_VEHICLE_STATE  = 0x04,
} msg_type_t;

// Sent by slave during discovery
typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t proto_ver;
  uint8_t slave_type;
  uint8_t req_count;
  uint32_t device_id;
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
  uint8_t pattern[4];
} msg_test_t;

typedef struct __attribute__((packed)) {
  uint8_t type;
  vehicle_state_t state;
} msg_vehicle_state_t;
_Static_assert(sizeof(msg_vehicle_state_t) == (1 + sizeof(vehicle_state_t)), "msg_vehicle_state_t size mismatch");

static espnow_role_t s_role                            = ESP_NOW_ROLE_MASTER;
static espnow_slave_type_t s_slave_type                = ESP_NOW_SLAVE_NONE;
static uint32_t s_device_id                            = 0;
static espnow_test_rx_cb_t s_test_rx_cb                = NULL;
static uint64_t s_last_peer_hb_us                      = 0;
static esp_timer_handle_t s_discovery_timer            = NULL;
static int s_discovery_retries                         = 0;
static uint64_t s_next_discovery_interval_us           = 2000000; // Start at 2s, exponential backoff
static bool s_broadcast_peer_added                     = false;
static bool s_slave_pairing_active                     = false;
static uint64_t s_last_test_rx_us                      = 0;
static uint8_t s_self_mac[6]                           = {0};
static bool s_init_done                                = false;
static uint64_t s_last_send_nomem_log_us               = 0;
static uint32_t s_send_nomem_drop_count                = 0;
static vehicle_state_t s_last_vehicle_state            = {0};
static espnow_vehicle_state_rx_cb_t s_vehicle_state_cb = NULL;

static void log_send_error(esp_err_t ret, const char *context) {
  if (ret == ESP_ERR_ESPNOW_NO_MEM) {
    s_send_nomem_drop_count++;
    uint64_t now_us = esp_timer_get_time();
    if (now_us - s_last_send_nomem_log_us > 1000000) { // log at most once per second
      ESP_LOGW(TAG_ESP_NOW, "%s drop (no mem) count=%u", context, (unsigned int)s_send_nomem_drop_count);
      s_last_send_nomem_log_us = now_us;
      s_send_nomem_drop_count  = 0;
    }
  } else {
    ESP_LOGW(TAG_ESP_NOW, "%s failed: %s", context, esp_err_to_name(ret));
  }
}

#define ESPNOW_MAX_PEERS 8
static espnow_peer_info_t s_peers[ESPNOW_MAX_PEERS];
static size_t s_peer_count = 0;

static void persist_peers_to_spiffs(void) {
  struct {
    uint8_t count;
    espnow_peer_info_t peers[ESPNOW_MAX_PEERS];
  } blob     = {0};
  blob.count = (s_peer_count > ESPNOW_MAX_PEERS) ? ESPNOW_MAX_PEERS : s_peer_count;
  if (blob.count) {
    memcpy(blob.peers, s_peers, blob.count * sizeof(espnow_peer_info_t));
  }
  esp_err_t err = spiffs_save_blob("/spiffs/ble/espnow_peers.bin", &blob, sizeof(blob));
  if (err != ESP_OK) {
    ESP_LOGW(TAG_ESP_NOW, "Failed to persist peers to SPIFFS: %s", esp_err_to_name(err));
  }
}

static void load_peers_from_spiffs(void) {
  struct {
    uint8_t count;
    espnow_peer_info_t peers[ESPNOW_MAX_PEERS];
  } blob          = {0};
  size_t required = sizeof(blob);

  esp_err_t err   = spiffs_load_blob("/spiffs/ble/espnow_peers.bin", &blob, &required);
  if (err != ESP_OK) {
    s_peer_count = 0;
    return;
  }

  s_peer_count = (blob.count > ESPNOW_MAX_PEERS) ? ESPNOW_MAX_PEERS : blob.count;
  if (s_peer_count) {
    memcpy(s_peers, blob.peers, s_peer_count * sizeof(espnow_peer_info_t));
    for (size_t i = 0; i < s_peer_count; i++) {
      if (s_peers[i].role != ESP_NOW_ROLE_MASTER && s_peers[i].role != ESP_NOW_ROLE_SLAVE) {
        s_peers[i].role = (s_role == ESP_NOW_ROLE_MASTER) ? ESP_NOW_ROLE_SLAVE : ESP_NOW_ROLE_MASTER;
      }
    }
  }
}

// Pairing mode
static bool s_is_pairing_mode                  = false;
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
  esp_err_t err        = esp_wifi_get_mac(ifx, s_self_mac);
  if (err != ESP_OK) {
    ESP_LOGW(TAG_ESP_NOW, "Failed to get MAC: %s", esp_err_to_name(err));
  }
  return err;
}

// Re-register peers stored in SPIFFS into esp_now peer table
static void register_cached_peers(void) {
  if (s_peer_count == 0)
    return;

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
    bool sta_active   = (wifi_mode == WIFI_MODE_STA || wifi_mode == WIFI_MODE_APSTA);
    peer.ifidx        = sta_active ? ESP_IF_WIFI_STA : ESP_IF_WIFI_AP;

    peer.channel      = s_peers[i].channel ? s_peers[i].channel : current_channel;
    peer.encrypt      = false;

    esp_err_t add_ret = esp_now_add_peer(&peer);
    if (add_ret != ESP_OK && add_ret != ESP_ERR_ESPNOW_EXIST) {
      ESP_LOGW(TAG_ESP_NOW,
               "Failed to restore peer %02X:%02X:%02X:%02X:%02X:%02X: %s",
               peer.peer_addr[0],
               peer.peer_addr[1],
               peer.peer_addr[2],
               peer.peer_addr[3],
               peer.peer_addr[4],
               peer.peer_addr[5],
               esp_err_to_name(add_ret));
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
  if (!mac)
    return;
  uint8_t channel = channel_hint ? channel_hint : get_current_channel();
  // search existing
  for (size_t i = 0; i < s_peer_count; i++) {
    if (memcmp(s_peers[i].mac, mac, 6) == 0) {
      bool changed = false;
      if (s_peers[i].role != role) {
        s_peers[i].role = role;
        changed         = true;
      }
      if (s_peers[i].type != type) {
        s_peers[i].type = type;
        changed         = true;
      }
      if (device_id != 0 && s_peers[i].device_id != device_id) {
        s_peers[i].device_id = device_id;
        changed              = true;
      }
      if (s_peers[i].channel != channel) {
        s_peers[i].channel = channel;
        changed            = true;
      }
      s_peers[i].last_seen_us = esp_timer_get_time();
      if (changed) {
        persist_peers_to_spiffs();
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
    s_peers[oldest].role         = role;
    s_peers[oldest].type         = type;
    s_peers[oldest].device_id    = device_id;
    s_peers[oldest].last_seen_us = esp_timer_get_time();
    s_peers[oldest].channel      = channel;
    return;
  }
  memcpy(s_peers[s_peer_count].mac, mac, 6);
  s_peers[s_peer_count].role         = role;
  s_peers[s_peer_count].type         = type;
  s_peers[s_peer_count].device_id    = device_id;
  s_peers[s_peer_count].last_seen_us = esp_timer_get_time();
  s_peers[s_peer_count].channel      = channel;
  s_peer_count++;
  persist_peers_to_spiffs();
}

static void remove_peer_from_cache(const uint8_t mac[6]) {
  if (!mac)
    return;
  for (size_t i = 0; i < s_peer_count; i++) {
    if (memcmp(s_peers[i].mac, mac, 6) == 0) {
      if (i != s_peer_count - 1) {
        s_peers[i] = s_peers[s_peer_count - 1];
      }
      s_peer_count--;
      persist_peers_to_spiffs();
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
  ESP_LOGD(TAG_ESP_NOW,
           "RX type=0x%02X len=%d from %02X:%02X:%02X:%02X:%02X:%02X",
           type,
           len,
           mac ? mac[0] : 0,
           mac ? mac[1] : 0,
           mac ? mac[2] : 0,
           mac ? mac[3] : 0,
           mac ? mac[4] : 0,
           mac ? mac[5] : 0);

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

    if (mac) {
      update_peer_info(mac, ESP_NOW_ROLE_SLAVE, (espnow_slave_type_t)req->slave_type, req->device_id, 0);
    }
    uint8_t max_ids_from_len = (len > 8) ? (uint8_t)((len - 8) / 4) : 0;
    if (max_ids_from_len > ESPNOW_MAX_REQUEST_IDS) {
      max_ids_from_len = ESPNOW_MAX_REQUEST_IDS;
    }

    msg_discovery_resp_t resp = {.type = MSG_DISCOVERY_RESP, .status = 0, .master_device_id = s_device_id};
    if (mac) {
      espnow_link_register_peer(mac);
      esp_now_send(mac, (const uint8_t *)&resp, sizeof(resp));
    }

    espnow_link_set_pairing_mode(false, 0);

  } else if (s_role == ESP_NOW_ROLE_SLAVE && type == MSG_DISCOVERY_RESP && len == sizeof(msg_discovery_resp_t)) {
    const msg_discovery_resp_t *resp = (const msg_discovery_resp_t *)data;
    s_last_peer_hb_us                = esp_timer_get_time();
    if (mac) {
      espnow_link_register_peer(mac);
      update_peer_info(mac, ESP_NOW_ROLE_MASTER, ESP_NOW_SLAVE_NONE, resp->master_device_id, 0);
    }
    if (s_discovery_timer && esp_timer_is_active(s_discovery_timer)) {
      esp_timer_stop(s_discovery_timer);
    }
    s_slave_pairing_active = false;
    status_manager_update_led_now();

  } else if (s_role == ESP_NOW_ROLE_SLAVE && type == MSG_VEHICLE_STATE) {
    if (len < (int)sizeof(msg_vehicle_state_t)) {
      ESP_LOGW(TAG_ESP_NOW, "Vehicle state too short: len=%d", len);
      return;
    }
    const msg_vehicle_state_t *msg = (const msg_vehicle_state_t *)data;
    vehicle_state_t state          = {0};
    memcpy(&state, &msg->state, sizeof(vehicle_state_t));
    s_last_peer_hb_us    = esp_timer_get_time();
    s_last_vehicle_state = state;
    if (s_vehicle_state_cb) {
      s_vehicle_state_cb(&state);
    }
  } else if (type == MSG_TEST) {
    s_last_test_rx_us = esp_timer_get_time();
    if (s_test_rx_cb) {
      s_test_rx_cb();
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

  uint8_t bcast[6]        = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  msg_discovery_req_t req = {.type = MSG_DISCOVERY_REQ, .proto_ver = PROTOCOL_VERSION, .slave_type = (uint8_t)s_slave_type, .device_id = s_device_id, .req_count = 0};
  size_t payload_len      = sizeof(msg_discovery_req_t);
  esp_err_t err           = esp_now_send(bcast, (uint8_t *)&req, payload_len);
  if (err != ESP_OK) {
    ESP_LOGW(TAG_ESP_NOW, "esp_now_send discovery failed: %s", esp_err_to_name(err));
  }

  // Exponential backoff: double the interval (max 16s)
  s_next_discovery_interval_us *= 2;
  if (s_next_discovery_interval_us > 16000000) {
    s_next_discovery_interval_us = 16000000; // Cap at 16s
  }

  // Reprogram timer with new interval (one-shot)
  esp_timer_stop(s_discovery_timer);
  esp_err_t start_err = esp_timer_start_once(s_discovery_timer, s_next_discovery_interval_us);
  if (start_err != ESP_OK) {
    ESP_LOGW(TAG_ESP_NOW, "Failed to restart discovery timer: %s", esp_err_to_name(start_err));
  }
}

// Heartbeat removed: vehicle state messages now act as liveness indicator

esp_err_t espnow_link_init(espnow_role_t role, espnow_slave_type_t slave_type) {
  s_role       = role;
  s_slave_type = slave_type;
  s_device_id  = esp_random();

  // Load persisted peers
  load_peers_from_spiffs();
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
    esp_err_t p  = esp_now_add_peer(&peer);
    if (p == ESP_OK || p == ESP_ERR_ESPNOW_EXIST) {
      s_broadcast_peer_added = true;
    } else {
      ESP_LOGW(TAG_ESP_NOW, "Failed to add broadcast peer: %s", esp_err_to_name(p));
    }
  }

  if (s_role == ESP_NOW_ROLE_MASTER) {
    const esp_timer_create_args_t pairing_timer_args = {.callback = &pairing_mode_timer_callback, .name = "espnow_pairing"};
    ESP_ERROR_CHECK(esp_timer_create(&pairing_timer_args, &s_pairing_mode_timer));
  }

  // Restore peers saved in SPIFFS (master & slave)
  register_cached_peers();

  if (s_role == ESP_NOW_ROLE_SLAVE) {
    const esp_timer_create_args_t discovery_args = {.callback = espnow_send_discovery_request, .arg = NULL, .dispatch_method = ESP_TIMER_TASK, .name = "espnow_discovery"};
    esp_err_t discovery_err                      = esp_timer_create(&discovery_args, &s_discovery_timer);
    if (discovery_err != ESP_OK) {
      ESP_LOGE(TAG_ESP_NOW, "Failed to create discovery timer: %s", esp_err_to_name(discovery_err));
    }
  }

  // Restore peers saved in SPIFFS (master & slave)
  register_cached_peers();

  // Auto-relaunch slave discovery on boot if not connected
  if (s_role == ESP_NOW_ROLE_SLAVE && !espnow_link_peer_alive(5000) && s_discovery_timer) {
    espnow_link_trigger_slave_pairing();
  }

  s_init_done = true;
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
    ESP_LOGD(TAG_ESP_NOW, "MASTER: Starting pairing on current Wi-Fi channel: %d", primary_channel);

    int target_channel = 0;
    wifi_ap_record_t ap_info;
    bool sta_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    if (target_channel == 0 && sta_connected) {
      target_channel = ap_info.primary;
      ESP_LOGD(TAG_ESP_NOW, "MASTER: STA connected, using AP channel %d for ESP-NOW", target_channel);
    } else if (target_channel > 0) {
      ESP_LOGD(TAG_ESP_NOW, "MASTER: Using fixed channel %d for ESP-NOW", target_channel);
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
    ESP_LOGD(TAG_ESP_NOW, "MASTER: Pairing will run on Wi-Fi channel: %d", primary_channel);

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

  int target_channel = 0;
  wifi_ap_record_t ap_info;
  bool sta_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
  if (target_channel == 0 && sta_connected) {
    target_channel = ap_info.primary;
    ESP_LOGD(TAG_ESP_NOW, "SLAVE: STA connected, using AP channel %d for ESP-NOW", target_channel);
  } else if (target_channel > 0) {
    ESP_LOGD(TAG_ESP_NOW, "SLAVE: Using fixed channel %d for ESP-NOW", target_channel);
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

  s_discovery_retries          = 0;
  s_next_discovery_interval_us = 2000000; // Reset to 2s
  s_slave_pairing_active       = true;
  status_led_set_state(STATUS_LED_ESPNOW_PAIRING);

  // Start broadcasting immediately, then use exponential backoff (2s, 4s, 8s, 16s)
  espnow_send_discovery_request(NULL);

  // No need to start timer here, espnow_send_discovery_request will schedule next retry
  return ESP_OK;
}

esp_err_t espnow_link_register_peer(const uint8_t mac[6]) {
  esp_now_peer_info_t peer = {0};
  memcpy(peer.peer_addr, mac, 6);
  wifi_mode_t wifi_mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&wifi_mode) != ESP_OK) {
    wifi_mode = WIFI_MODE_APSTA;
  }
  bool sta_active         = (wifi_mode == WIFI_MODE_STA || wifi_mode == WIFI_MODE_APSTA);
  peer.ifidx              = sta_active ? ESP_IF_WIFI_STA : ESP_IF_WIFI_AP; // prefer STA interface when connected to external AP

  uint8_t primary_channel = 0;
  wifi_second_chan_t second_channel;
  if (esp_wifi_get_channel(&primary_channel, &second_channel) != ESP_OK) {
    primary_channel = 0;
  }
  peer.channel = primary_channel; // lock to current channel to avoid hopping between AP/STA contexts
  peer.encrypt = false;

  if (esp_now_is_peer_exist(mac)) {
    ESP_LOGI(TAG_ESP_NOW, "Peer %02X:%02X:%02X:%02X:%02X:%02X already exists, updating (ifidx=%d ch=%d).", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], peer.ifidx, peer.channel);
    return esp_now_mod_peer(&peer);
  }

  ESP_LOGI(TAG_ESP_NOW, "Adding peer %02X:%02X:%02X:%02X:%02X:%02X on if=%d channel=%d", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], peer.ifidx, peer.channel);
  esp_err_t add_ret = esp_now_add_peer(&peer);
  if (add_ret == ESP_OK) {
    espnow_role_t peer_role = (s_role == ESP_NOW_ROLE_MASTER) ? ESP_NOW_ROLE_SLAVE : ESP_NOW_ROLE_MASTER;
    update_peer_info(mac, peer_role, ESP_NOW_SLAVE_NONE, 0, peer.channel);
  }
  return add_ret;
}

esp_err_t espnow_link_send_vehicle_state(const vehicle_state_t *state) {
  if (!state) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_init_done || s_role != ESP_NOW_ROLE_MASTER) {
    return ESP_ERR_INVALID_STATE;
  }

  // Quick check: if no peers, don't send anything
  if (s_peer_count == 0) {
    return ESP_OK; // No peers connected, skip sending
  }

  msg_vehicle_state_t msg = {.type = MSG_VEHICLE_STATE};
  memcpy(&msg.state, state, sizeof(vehicle_state_t));

  uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_err_t ret    = esp_now_send(bcast, (const uint8_t *)&msg, sizeof(msg));
  if (ret != ESP_OK) {
    log_send_error(ret, "esp_now_send vehicle_state");
  }

  return ret;
}

espnow_role_t espnow_link_get_role(void) {
  return s_role;
}

espnow_slave_type_t espnow_link_get_slave_type(void) {
  return s_slave_type;
}

void espnow_link_register_test_rx_callback(espnow_test_rx_cb_t cb) {
  s_test_rx_cb = cb;
}

void espnow_link_register_vehicle_state_rx_callback(espnow_vehicle_state_rx_cb_t cb) {
  s_vehicle_state_cb = cb;
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
  // Clear peer list and SPIFFS
  esp_now_peer_info_t peer = {0};
  for (esp_err_t ret = esp_now_fetch_peer(true, &peer); ret == ESP_OK; ret = esp_now_fetch_peer(false, &peer)) {
    esp_now_del_peer(peer.peer_addr);
    remove_peer_from_cache(peer.peer_addr);
  }
  s_peer_count = 0;
  persist_peers_to_spiffs();
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
  persist_peers_to_spiffs();
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

  msg_test_t test   = {.type = MSG_TEST, .pattern = {0xDE, 0xAD, 0xBE, 0xEF}};

  uint8_t target[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (mac) {
    memcpy(target, mac, 6);
  }

  esp_err_t ret = esp_now_send(target, (const uint8_t *)&test, sizeof(test));
  if (ret != ESP_OK) {
    log_send_error(ret, "esp_now_send test");
  }
  return ret;
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
  s_role       = role;
  s_slave_type = type;

  if (role == ESP_NOW_ROLE_MASTER) {
    // Stop any slave discovery state
    if (s_discovery_timer && esp_timer_is_active(s_discovery_timer)) {
      esp_timer_stop(s_discovery_timer);
    }
    s_slave_pairing_active = false;

    // Ensure pairing timer exists for master
    if (!s_pairing_mode_timer) {
      const esp_timer_create_args_t pairing_timer_args = {.callback = &pairing_mode_timer_callback, .name = "espnow_pairing"};
      esp_timer_create(&pairing_timer_args, &s_pairing_mode_timer);
    }
  } else {
    // Switching to slave: stop pairing mode if active
    if (s_pairing_mode_timer && esp_timer_is_active(s_pairing_mode_timer)) {
      esp_timer_stop(s_pairing_mode_timer);
    }
    s_is_pairing_mode = false;
  }
}

esp_err_t espnow_link_get_mac(uint8_t mac_out[6]) {
  if (!mac_out)
    return ESP_ERR_INVALID_ARG;
  if (s_self_mac[0] == 0 && s_self_mac[1] == 0) {
    load_self_mac();
  }
  memcpy(mac_out, s_self_mac, 6);
  return ESP_OK;
}

uint32_t espnow_link_get_device_id(void) {
  return s_device_id;
}
