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

// Simplified DNS header structure
typedef struct {
  uint16_t id;
  uint16_t flags;
  uint16_t questions;
  uint16_t answers;
  uint16_t authority;
  uint16_t additional;
} __attribute__((packed)) dns_header_t;

// Parse the domain name in a DNS query
static int parse_dns_name(const uint8_t *data, char *name, int max_len) {
  int pos      = 0;
  int name_pos = 0;

  while (data[pos] != 0 && pos < max_len) {
    uint8_t label_len = data[pos];
    if (label_len > 63)
      break; // Label too long

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
  return pos + 1; // +1 for final null byte
}

// DNS server task
static void dns_server_task(void *pvParameters) {
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);
  uint8_t rx_buffer[DNS_MAX_LEN];
  uint8_t tx_buffer[DNS_MAX_LEN];

  ESP_LOGI(TAG_CAPTIVE_PORTAL, "Starting DNS server for captive portal");

  // Create the UDP socket
  dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (dns_socket < 0) {
    ESP_LOGE(TAG_CAPTIVE_PORTAL, "DNS socket creation error");
    dns_server_running = false;
    vTaskDelete(NULL);
    return;
  }

  // Configure server address
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family      = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port        = htons(DNS_PORT);

  // Bind socket
  if (bind(dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG_CAPTIVE_PORTAL, "DNS socket bind error");
    close(dns_socket);
    dns_socket         = -1;
    dns_server_running = false;
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG_CAPTIVE_PORTAL, "DNS server listening on port %d", DNS_PORT);
  dns_server_running = true;

  while (dns_server_running) {
    // Receive a DNS request
    int len = recvfrom(dns_socket, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&client_addr, &client_addr_len);

    if (len < sizeof(dns_header_t)) {
      continue;
    }

    dns_header_t *header = (dns_header_t *)rx_buffer;

    // Verify this is a query (QR=0)
    if ((ntohs(header->flags) & 0x8000) != 0) {
      continue;
    }

    // Parse requested domain name
    char domain_name[256];
    parse_dns_name(rx_buffer + sizeof(dns_header_t), domain_name, sizeof(domain_name));

    ESP_LOGD(TAG_CAPTIVE_PORTAL, "DNS request for: %s", domain_name);

    // Build the DNS response
    memcpy(tx_buffer, rx_buffer, len);
    dns_header_t *resp_header = (dns_header_t *)tx_buffer;

    // Flags: QR=1 (response), AA=1 (authoritative)
    resp_header->flags        = htons(0x8400);
    resp_header->answers      = htons(1);
    resp_header->authority    = 0;
    resp_header->additional   = 0;

    int response_len          = len;

    // Add answer section
    // Pointer to the name (DNS compression)
    tx_buffer[response_len++] = 0xC0; // Pointer
    tx_buffer[response_len++] = 0x0C; // Offset to the name in question

    // Type A (IPv4)
    tx_buffer[response_len++] = 0x00;
    tx_buffer[response_len++] = 0x01;

    // Class IN
    tx_buffer[response_len++] = 0x00;
    tx_buffer[response_len++] = 0x01;

    // TTL (60 seconds)
    tx_buffer[response_len++] = 0x00;
    tx_buffer[response_len++] = 0x00;
    tx_buffer[response_len++] = 0x00;
    tx_buffer[response_len++] = 0x3C;

    // Data length (4 bytes for IPv4)
    tx_buffer[response_len++] = 0x00;
    tx_buffer[response_len++] = 0x04;

    // AP IP address (192.168.4.1 default)
    tx_buffer[response_len++] = 192;
    tx_buffer[response_len++] = 168;
    tx_buffer[response_len++] = 4;
    tx_buffer[response_len++] = 1;

    // Send the response
    sendto(dns_socket, tx_buffer, response_len, 0, (struct sockaddr *)&client_addr, client_addr_len);

    ESP_LOGD(TAG_CAPTIVE_PORTAL, "DNS response sent: %s -> 192.168.4.1", domain_name);
  }

  if (dns_socket >= 0) {
    close(dns_socket);
    dns_socket = -1;
  }

  ESP_LOGI(TAG_CAPTIVE_PORTAL, "DNS server stopped");
  vTaskDelete(NULL);
}

esp_err_t captive_portal_init(void) {
  ESP_LOGI(TAG_CAPTIVE_PORTAL, "Captive portal initialization");
  return ESP_OK;
}

esp_err_t captive_portal_start(void) {
  if (dns_server_running) {
    ESP_LOGW(TAG_CAPTIVE_PORTAL, "Captive portal already running");
    return ESP_OK;
  }

  BaseType_t ret = xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &dns_task_handle);
  if (ret != pdPASS) {
    ESP_LOGE(TAG_CAPTIVE_PORTAL, "Error creating DNS task");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG_CAPTIVE_PORTAL, "Captive portal started");
  return ESP_OK;
}

esp_err_t captive_portal_stop(void) {
  if (!dns_server_running) {
    return ESP_OK;
  }

  dns_server_running = false;

  // Close socket to unblock the task
  if (dns_socket >= 0) {
    shutdown(dns_socket, SHUT_RDWR);
    close(dns_socket);
    dns_socket = -1;
  }

  // Wait for the task to finish
  if (dns_task_handle != NULL) {
    vTaskDelay(pdMS_TO_TICKS(100));
    dns_task_handle = NULL;
  }

  ESP_LOGI(TAG_CAPTIVE_PORTAL, "Captive portal stopped");
  return ESP_OK;
}

bool captive_portal_is_running(void) {
  return dns_server_running;
}
