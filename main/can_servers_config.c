#include "can_servers_config.h"

#include "esp_log.h"
#include "settings_manager.h"

static const char *TAG                            = "CAN_SERVERS_CFG";

// Server names for logging
static const char *SERVER_NAMES[CAN_SERVER_COUNT] = {[CAN_SERVER_GVRET] = "GVRET", [CAN_SERVER_CANSERVER] = "CANServer"};

esp_err_t can_servers_config_set_autostart(can_server_type_t server_type, bool autostart) {
  if (server_type >= CAN_SERVER_COUNT) {
    ESP_LOGE(TAG, "Invalid server type: %d", server_type);
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = settings_set_bool(server_type == CAN_SERVER_GVRET ? "gvret_autostart" : "canserver_autostart", autostart);

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

  return settings_get_bool(server_type == CAN_SERVER_GVRET ? "gvret_autostart" : "canserver_autostart", false);
}
