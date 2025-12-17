#include "config.h"

#include "esp_mac.h"

#include <stdio.h>
#include <string.h>

// Variables globales pour stocker les noms avec suffixe
char g_device_name_with_suffix[32] = "CarLightSync";
char g_wifi_ssid_with_suffix[32]   = "CarLightSync";

void config_init_device_names(void) {
  uint8_t mac[6];

  // Retrieve base MAC address
  esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (ret != ESP_OK) {
    // En cas d'erreur, utiliser l'adresse MAC BT
    ret = esp_read_mac(mac, ESP_MAC_BT);
  }

  if (ret == ESP_OK) {
    // Use the last 2 MAC bytes to create a unique suffix
    // Format: XXXX (4 hex characters)
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);

    // Create BLE name with suffix
    snprintf(g_device_name_with_suffix, sizeof(g_device_name_with_suffix), "CarLightSync-%s", suffix);

    // Create WiFi AP name with suffix (same format as BLE)
    snprintf(g_wifi_ssid_with_suffix, sizeof(g_wifi_ssid_with_suffix), "CarLightSync-%s", suffix);
  }
}
