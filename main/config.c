#include "config.h"
#include "esp_mac.h"
#include <stdio.h>
#include <string.h>

// Variables globales pour stocker les noms avec suffixe
char g_device_name_with_suffix[32] = "CarLightSync";
char g_wifi_ssid_with_suffix[32] = "CarLightSync";

void config_init_device_names(void)
{
    uint8_t mac[6];

    // Récupérer l'adresse MAC de base
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        // En cas d'erreur, utiliser l'adresse MAC BT
        ret = esp_read_mac(mac, ESP_MAC_BT);
    }

    if (ret == ESP_OK) {
        // Utiliser les 2 derniers octets de l'adresse MAC pour créer un suffixe unique
        // Format: XXXX (4 caractères hexadécimaux)
        char suffix[8];
        snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);

        // Créer le nom BLE avec suffixe
        snprintf(g_device_name_with_suffix, sizeof(g_device_name_with_suffix), "CarLightSync-%s", suffix);

        // Créer le nom WiFi AP avec suffixe (même format que BLE)
        snprintf(g_wifi_ssid_with_suffix, sizeof(g_wifi_ssid_with_suffix), "CarLightSync-%s", suffix);
    }
}
