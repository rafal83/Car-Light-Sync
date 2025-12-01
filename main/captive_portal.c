#include "captive_portal.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

#include <string.h>

// Port DNS standard
#define DNS_PORT 53
#define DNS_MAX_LEN 512

static bool dns_server_running      = false;
static TaskHandle_t dns_task_handle = NULL;
static int dns_socket               = -1;

// Structure simplifiée d'un en-tête DNS
typedef struct {
  uint16_t id;
  uint16_t flags;
  uint16_t questions;
  uint16_t answers;
  uint16_t authority;
  uint16_t additional;
} __attribute__((packed)) dns_header_t;

// Fonction pour parser le nom de domaine dans la requête DNS
static int parse_dns_name(const uint8_t *data, char *name, int max_len) {
  int pos      = 0;
  int name_pos = 0;

  while (data[pos] != 0 && pos < max_len) {
    uint8_t label_len = data[pos];
    if (label_len > 63)
      break; // Label trop long

    pos++;
    if (name_pos + label_len + 1 >= max_len)
      break;

    if (name_pos > 0) {
      name[name_pos++] = '.';
    }

    memcpy(name + name_pos, data + pos, label_len);
    name_pos += label_len;
    pos += label_len;
  }

  name[name_pos] = '\0';
  return pos + 1; // +1 pour le null byte final
}

// Tâche du serveur DNS
static void dns_server_task(void *pvParameters) {
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);
  uint8_t rx_buffer[DNS_MAX_LEN];
  uint8_t tx_buffer[DNS_MAX_LEN];

  ESP_LOGI(TAG_CAPTIVE_PORTAL, "Démarrage du serveur DNS pour le portail captif");

  // Créer le socket UDP
  dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (dns_socket < 0) {
    ESP_LOGE(TAG_CAPTIVE_PORTAL, "Erreur création socket DNS");
    dns_server_running = false;
    vTaskDelete(NULL);
    return;
  }

  // Configuration de l'adresse du serveur
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family      = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port        = htons(DNS_PORT);

  // Bind du socket
  if (bind(dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG_CAPTIVE_PORTAL, "Erreur bind socket DNS");
    close(dns_socket);
    dns_socket         = -1;
    dns_server_running = false;
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG_CAPTIVE_PORTAL, "Serveur DNS en écoute sur le port %d", DNS_PORT);
  dns_server_running = true;

  while (dns_server_running) {
    // Recevoir une requête DNS
    int len = recvfrom(dns_socket, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&client_addr, &client_addr_len);

    if (len < sizeof(dns_header_t)) {
      continue;
    }

    dns_header_t *header = (dns_header_t *)rx_buffer;

    // Vérifier que c'est une requête (QR=0)
    if ((ntohs(header->flags) & 0x8000) != 0) {
      continue;
    }

    // Parser le nom de domaine demandé
    char domain_name[256];
    parse_dns_name(rx_buffer + sizeof(dns_header_t), domain_name, sizeof(domain_name));

    ESP_LOGD(TAG_CAPTIVE_PORTAL, "Requête DNS pour: %s", domain_name);

    // Construire la réponse DNS
    memcpy(tx_buffer, rx_buffer, len);
    dns_header_t *resp_header = (dns_header_t *)tx_buffer;

    // Modifier les flags: QR=1 (réponse), AA=1 (authoritative)
    resp_header->flags        = htons(0x8400);
    resp_header->answers      = htons(1);
    resp_header->authority    = 0;
    resp_header->additional   = 0;

    int response_len          = len;

    // Ajouter la section réponse (Answer)
    // Pointer vers le nom (compression DNS)
    tx_buffer[response_len++] = 0xC0; // Pointeur
    tx_buffer[response_len++] = 0x0C; // Offset vers le nom dans la question

    // Type A (IPv4)
    tx_buffer[response_len++] = 0x00;
    tx_buffer[response_len++] = 0x01;

    // Class IN
    tx_buffer[response_len++] = 0x00;
    tx_buffer[response_len++] = 0x01;

    // TTL (60 secondes)
    tx_buffer[response_len++] = 0x00;
    tx_buffer[response_len++] = 0x00;
    tx_buffer[response_len++] = 0x00;
    tx_buffer[response_len++] = 0x3C;

    // Data length (4 bytes pour IPv4)
    tx_buffer[response_len++] = 0x00;
    tx_buffer[response_len++] = 0x04;

    // Adresse IP de l'AP (192.168.10.1 par défaut)
    tx_buffer[response_len++] = 192;
    tx_buffer[response_len++] = 168;
    tx_buffer[response_len++] = 10;
    tx_buffer[response_len++] = 1;

    // Envoyer la réponse
    sendto(dns_socket, tx_buffer, response_len, 0, (struct sockaddr *)&client_addr, client_addr_len);

    ESP_LOGD(TAG_CAPTIVE_PORTAL, "Réponse DNS envoyée: %s -> 192.168.10.1", domain_name);
  }

  if (dns_socket >= 0) {
    close(dns_socket);
    dns_socket = -1;
  }

  ESP_LOGI(TAG_CAPTIVE_PORTAL, "Serveur DNS arrêté");
  vTaskDelete(NULL);
}

esp_err_t captive_portal_init(void) {
  ESP_LOGI(TAG_CAPTIVE_PORTAL, "Initialisation du portail captif");
  return ESP_OK;
}

esp_err_t captive_portal_start(void) {
  if (dns_server_running) {
    ESP_LOGW(TAG_CAPTIVE_PORTAL, "Portail captif déjà actif");
    return ESP_OK;
  }

  BaseType_t ret = xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &dns_task_handle);
  if (ret != pdPASS) {
    ESP_LOGE(TAG_CAPTIVE_PORTAL, "Erreur création tâche DNS");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG_CAPTIVE_PORTAL, "Portail captif démarré");
  return ESP_OK;
}

esp_err_t captive_portal_stop(void) {
  if (!dns_server_running) {
    return ESP_OK;
  }

  dns_server_running = false;

  // Fermer le socket pour débloquer la tâche
  if (dns_socket >= 0) {
    shutdown(dns_socket, SHUT_RDWR);
    close(dns_socket);
    dns_socket = -1;
  }

  // Attendre la fin de la tâche
  if (dns_task_handle != NULL) {
    vTaskDelay(pdMS_TO_TICKS(100));
    dns_task_handle = NULL;
  }

  ESP_LOGI(TAG_CAPTIVE_PORTAL, "Portail captif arrêté");
  return ESP_OK;
}

bool captive_portal_is_running(void) {
  return dns_server_running;
}
