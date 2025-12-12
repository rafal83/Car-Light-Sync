#include "can_servers_config.h"

#include "esp_log.h"
#include "nvs_manager.h"

static const char *TAG                            = "CAN_SERVERS_CFG";

// NVS keys for each server type
static const char *NVS_KEYS[CAN_SERVER_COUNT]     = {[CAN_SERVER_GVRET] = "gvret_auto", [CAN_SERVER_CANSERVER] = "canserver_auto", [CAN_SERVER_SLCAN] = "slcan_auto"};

// Server names for logging
static const char *SERVER_NAMES[CAN_SERVER_COUNT] = {[CAN_SERVER_GVRET] = "GVRET", [CAN_SERVER_CANSERVER] = "CANServer", [CAN_SERVER_SLCAN] = "SLCAN"};

esp_err_t can_servers_config_set_autostart(can_server_type_t server_type, bool autostart) {
  if (server_type >= CAN_SERVER_COUNT) {
    ESP_LOGE(TAG, "Invalid server type: %d", server_type);
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = nvs_manager_set_bool(NVS_NAMESPACE_CAN_SERVERS, NVS_KEYS[server_type], autostart);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "%s autostart %s", SERVER_NAMES[server_type], autostart ? "enabled" : "disabled");
  }

  return err;
}

bool can_servers_config_get_autostart(can_server_type_t server_type) {
  if (server_type >= CAN_SERVER_COUNT) {
    ESP_LOGE(TAG, "Invalid server type: %d", server_type);
    return false;
  }

  return nvs_manager_get_bool(NVS_NAMESPACE_CAN_SERVERS, NVS_KEYS[server_type], false);
}
