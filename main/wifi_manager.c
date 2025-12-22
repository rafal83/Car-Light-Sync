#include "wifi_manager.h"

#include "captive_portal.h"
#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "nvs_flash.h"

#include <string.h>

static wifi_status_t current_status = {0};
static bool wifi_initialized        = false;
static esp_netif_t *ap_netif        = NULL;
static esp_netif_t *sta_netif       = NULL;
static int sta_retry_count          = 0;
static const int STA_MAX_RETRY      = 5;
static bool sta_auto_reconnect      = true;

static void wifi_apply_power_settings(wifi_mode_t mode);

bool wifi_wait_for_sta(uint32_t timeout_ms) {
  uint32_t start = esp_log_timestamp();
  while ((esp_log_timestamp() - start) < timeout_ms) {
    wifi_status_t st;
    wifi_manager_get_status(&st);
    if (st.sta_connected) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return false;
}

// Event handler for Wi-Fi events
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
    case WIFI_EVENT_AP_STACONNECTED: {
      wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
      ESP_LOGI(TAG_WIFI, "Client connected, MAC: %02x:%02x:%02x:%02x:%02x:%02x", MAC2STR(event->mac));
      current_status.connected_clients++;
      break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
      wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
      ESP_LOGI(TAG_WIFI, "Client disconnected, MAC: %02x:%02x:%02x:%02x:%02x:%02x", MAC2STR(event->mac));
      if (current_status.connected_clients > 0) {
        current_status.connected_clients--;
      }
      break;
    }
    case WIFI_EVENT_STA_START:
      ESP_LOGI(TAG_WIFI, "Station mode started");
      sta_retry_count = 0;
      esp_wifi_connect();
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      current_status.sta_connected = false;
      if (sta_auto_reconnect && sta_retry_count < STA_MAX_RETRY) {
        sta_retry_count++;
        ESP_LOGI(TAG_WIFI, "Disconnected from network, retry %d/%d...", sta_retry_count, STA_MAX_RETRY);
        esp_wifi_connect();
      } else if (sta_retry_count >= STA_MAX_RETRY) {
        ESP_LOGW(TAG_WIFI, "Maximum retries reached, stopping automatic reconnection");
        sta_auto_reconnect = false;
      }
      break;
    default:
      break;
    }
  } else if (event_base == IP_EVENT) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      ESP_LOGI(TAG_WIFI, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
      snprintf(current_status.sta_ip, sizeof(current_status.sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
      current_status.sta_connected = true;
      sta_retry_count              = 0; // Reset counter on success
    }
  }
}

esp_err_t wifi_manager_init(void) {
  if (wifi_initialized) {
    ESP_LOGW(TAG_WIFI, "WiFi already initialized");
    return ESP_OK;
  }

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize TCP/IP
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Create network interfaces
  ap_netif  = esp_netif_create_default_wifi_ap();
  sta_netif = esp_netif_create_default_wifi_sta();

  // Configure hostname for mDNS (visible on the local network)
  // Uses the SSID that already embeds the MAC suffix (e.g., "CarLightSync-A1B2C3")
  ESP_ERROR_CHECK(esp_netif_set_hostname(sta_netif, g_wifi_ssid_with_suffix));
  ESP_ERROR_CHECK(esp_netif_set_hostname(ap_netif, g_wifi_ssid_with_suffix));
  ESP_LOGI(TAG_WIFI, "Hostname set: %s", g_wifi_ssid_with_suffix);

  esp_netif_ip_info_t ip_info;
  IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
  IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
  IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

  ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
  ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
  ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

  // Default Wi-Fi configuration
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Register event handlers
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

  wifi_initialized = true;
  ESP_LOGI(TAG_WIFI, "WiFi initialized");
  return ESP_OK;
}

static void wifi_apply_power_settings(wifi_mode_t mode) {
  // Reduce TX power to mitigate brownout during WiFi bursts
  // Range is 8..84 (0.25 dBm units). 8 ~= 2 dBm.
  esp_err_t ret = esp_wifi_set_max_tx_power(8);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG_WIFI, "Failed to set max TX power: %s", esp_err_to_name(ret));
  }

  // Enable modem power save to reduce peak current
  ret = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG_WIFI, "Failed to set WiFi PS: %s", esp_err_to_name(ret));
  }

  // Force 20 MHz bandwidth to lower power draw
  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
    ret = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG_WIFI, "Failed to set AP bandwidth: %s", esp_err_to_name(ret));
    }
  }
  if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
    ret = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG_WIFI, "Failed to set STA bandwidth: %s", esp_err_to_name(ret));
    }
  }
}

esp_err_t wifi_manager_start_ap(void) {
  if (!wifi_initialized) {
    ESP_LOGE(TAG_WIFI, "WiFi not initialized");
    return ESP_FAIL;
  }

  // Access point configuration with the SSID including the MAC suffix
  wifi_config_t ap_config = {
      .ap = {.ssid = "", .ssid_len = 0, .password = WIFI_AP_PASSWORD, .max_connection = WIFI_MAX_CLIENTS, .authmode = WIFI_AUTH_WPA2_PSK, .channel = 1},
  };

  // Copy SSID with MAC suffix (ensure null termination)
  size_t ssid_len = strlen(g_wifi_ssid_with_suffix);
  size_t max_len  = sizeof(ap_config.ap.ssid) - 1;
  if (ssid_len > max_len) ssid_len = max_len;
  memcpy(ap_config.ap.ssid, g_wifi_ssid_with_suffix, ssid_len);
  ap_config.ap.ssid[ssid_len] = '\0';
  ap_config.ap.ssid_len       = ssid_len;

  if (strlen(WIFI_AP_PASSWORD) == 0) {
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
  }

  // Start AP only (no STA mode to avoid connection attempts)
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  wifi_apply_power_settings(WIFI_MODE_AP);

  // Retrieve AP IP
  esp_netif_ip_info_t ip_info;
  esp_netif_get_ip_info(ap_netif, &ip_info);
  snprintf(current_status.ap_ip, sizeof(current_status.ap_ip), IPSTR, IP2STR(&ip_info.ip));

  current_status.ap_started = true;
  ESP_LOGI(TAG_WIFI, "Access point started: %s, IP: %s", g_wifi_ssid_with_suffix, current_status.ap_ip);

  // Start captive portal
  esp_err_t portal_ret = captive_portal_start();
  if (portal_ret != ESP_OK) {
    ESP_LOGW(TAG_WIFI, "Captive portal start failed: %s", esp_err_to_name(portal_ret));
  } else {
    ESP_LOGI(TAG_WIFI, "Captive portal enabled");
  }

  return ESP_OK;
}

esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password) {
  if (!wifi_initialized) {
    ESP_LOGE(TAG_WIFI, "WiFi not initialized");
    return ESP_FAIL;
  }

  // Switch to APSTA mode to allow client connection
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

  // Ensure the driver is started (AP mode already running)
  esp_err_t start_ret = esp_wifi_start();
  if (start_ret != ESP_OK && start_ret != ESP_ERR_WIFI_CONN) {
    ESP_ERROR_CHECK(start_ret);
  }
  wifi_apply_power_settings(WIFI_MODE_APSTA);

  wifi_config_t sta_config = {0};
  strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
  strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
  sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  sta_config.sta.listen_interval    = 5;

  // ESP-IDF 5.5: esp_wifi_set_config returns ESP_ERR_WIFI_STATE if a connection is in progress.
  // Force a disconnect first then apply the new config before calling connect.
  esp_wifi_disconnect(); // ignore error if already disconnected
  esp_err_t cfg_ret = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
  if (cfg_ret == ESP_ERR_WIFI_STATE) {
    vTaskDelay(pdMS_TO_TICKS(50));
    cfg_ret = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
  }
  ESP_ERROR_CHECK(cfg_ret);
  vTaskDelay(pdMS_TO_TICKS(200));
  ESP_ERROR_CHECK(esp_wifi_connect());

  strncpy(current_status.sta_ssid, ssid, sizeof(current_status.sta_ssid) - 1);

  // Re-enable automatic reconnection and reset counter
  sta_auto_reconnect = true;
  sta_retry_count    = 0;

  ESP_LOGI(TAG_WIFI, "Connecting to %s...", ssid);
  return ESP_OK;
}

esp_err_t wifi_manager_disconnect_sta(void) {
  current_status.sta_connected = false;
  current_status.sta_ssid[0]   = '\0';
  current_status.sta_ip[0]     = '\0';

  // Disable auto-reconnect during manual disconnect
  sta_auto_reconnect           = false;
  sta_retry_count              = 0;

  ESP_LOGI(TAG_WIFI, "Manual disconnection from WiFi network");

  // Disconnect and switch back to AP-only mode
  esp_wifi_disconnect();
  esp_wifi_set_mode(WIFI_MODE_AP);

  return ESP_OK;
}

int wifi_manager_scan_networks(uint32_t scan_time) {
  if (!wifi_initialized) {
    return 0;
  }

  wifi_scan_config_t scan_config =
      {.ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = true, .scan_type = WIFI_SCAN_TYPE_ACTIVE, .scan_time.active.min = scan_time, .scan_time.active.max = scan_time};

  esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_WIFI, "Scan error: %s", esp_err_to_name(ret));
    return 0;
  }

  uint16_t ap_count = 0;
  esp_wifi_scan_get_ap_num(&ap_count);

  ESP_LOGI(TAG_WIFI, "%d networks found", ap_count);
  return ap_count;
}

esp_err_t wifi_manager_get_status(wifi_status_t *status) {
  if (status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  memcpy(status, &current_status, sizeof(wifi_status_t));
  return ESP_OK;
}

esp_err_t wifi_manager_stop(void) {
  if (!wifi_initialized) {
    return ESP_OK;
  }

  // Stop captive portal
  captive_portal_stop();

  esp_wifi_stop();
  current_status.ap_started        = false;
  current_status.sta_connected     = false;
  current_status.connected_clients = 0;

  ESP_LOGI(TAG_WIFI, "WiFi stopped");
  return ESP_OK;
}

esp_err_t wifi_manager_set_mode(wifi_mode_t mode) {
  wifi_mode_t esp_mode;

  switch (mode) {
  case WIFI_MODE_AP:
    esp_mode = WIFI_MODE_AP;
    break;
  case WIFI_MODE_STA:
    esp_mode = WIFI_MODE_STA;
    break;
  case WIFI_MODE_APSTA:
    esp_mode = WIFI_MODE_APSTA;
    break;
  default:
    return ESP_ERR_INVALID_ARG;
  }

  return esp_wifi_set_mode(esp_mode);
}
