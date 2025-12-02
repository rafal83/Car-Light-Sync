#include "ble_api_service.h"

#include "sdkconfig.h"

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED

#include "cJSON.h"
#include "config.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

// UUIDs (128-bit) - NimBLE attend les octets en ordre inversé
// Service: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
static const ble_uuid128_t ble_service_uuid  = BLE_UUID128_INIT(0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f, 0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f);

// Command: beb5483e-36e1-4688-b7f5-ea07361b26a8
static const ble_uuid128_t ble_command_uuid  = BLE_UUID128_INIT(0xa8, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7, 0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);

// Response: 64a0990c-52eb-4c1b-aa30-ea826f4ba9dc
static const ble_uuid128_t ble_response_uuid = BLE_UUID128_INIT(0xdc, 0xa9, 0x4b, 0x6f, 0x82, 0xea, 0x30, 0xaa, 0x1b, 0x4c, 0xeb, 0x52, 0x0c, 0x99, 0xa0, 0x64);

typedef struct {
  size_t length;
  char *payload;
} ble_request_message_t;

typedef struct {
  char body[BLE_MAX_RESPONSE_BODY];
  size_t body_length;
  bool truncated;
  int status_code;
  char status_text[32];
  char content_type[64];
} ble_http_response_t;

static uint16_t ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t ble_command_val_handle;
static uint16_t ble_response_val_handle;
static bool ble_connected               = false;
static bool notifications_enabled       = false;
static uint16_t negotiated_mtu          = 23;
static bool ble_started                 = false;
static QueueHandle_t request_queue      = NULL;
static TaskHandle_t request_task_handle = NULL;
static char incoming_buffer[BLE_MAX_REQUEST_LEN];
static size_t incoming_length = 0;

static void ble_request_task(void *arg);
static void ble_handle_command_bytes(const uint8_t *data, size_t length);
static void ble_dispatch_request(const char *payload, size_t len);
static void ble_process_request_message(const char *json);
static void ble_send_json_response(cJSON *object);
static void ble_send_text_response(const char *text);
static void ble_send_error_response(int status, const char *status_text, const char *message);
static bool ble_send_notification(const uint8_t *data, size_t len);
static esp_err_t ble_perform_local_http_request(const char *method, const char *path, const char *body, size_t body_len, cJSON *headers, ble_http_response_t *out_response);
static esp_err_t ble_http_event_handler(esp_http_client_event_t *evt);

static esp_http_client_method_t ble_method_from_string(const char *method) {
  if (!method) {
    return HTTP_METHOD_GET;
  }
  if (!strcasecmp(method, "GET")) {
    return HTTP_METHOD_GET;
  } else if (!strcasecmp(method, "POST")) {
    return HTTP_METHOD_POST;
  } else if (!strcasecmp(method, "PUT")) {
    return HTTP_METHOD_PUT;
  } else if (!strcasecmp(method, "PATCH")) {
    return HTTP_METHOD_PATCH;
  } else if (!strcasecmp(method, "DELETE")) {
    return HTTP_METHOD_DELETE;
  } else if (!strcasecmp(method, "HEAD")) {
    return HTTP_METHOD_HEAD;
  } else if (!strcasecmp(method, "OPTIONS")) {
    return HTTP_METHOD_OPTIONS;
  }
  return HTTP_METHOD_GET;
}

static int ble_gap_event(struct ble_gap_event *event, void *arg);
static int ble_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def ble_gatt_svcs[] = {{.type            = BLE_GATT_SVC_TYPE_PRIMARY,
                                                         .uuid            = &ble_service_uuid.u,
                                                         .characteristics = (struct ble_gatt_chr_def[]){{
                                                                                                            .uuid       = &ble_command_uuid.u,
                                                                                                            .access_cb  = ble_gatt_access_cb,
                                                                                                            .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                                                                                                            .val_handle = &ble_command_val_handle,
                                                                                                        },
                                                                                                        {
                                                                                                            .uuid       = &ble_response_uuid.u,
                                                                                                            .access_cb  = ble_gatt_access_cb,
                                                                                                            .flags      = BLE_GATT_CHR_F_NOTIFY,
                                                                                                            .val_handle = &ble_response_val_handle,
                                                                                                        },
                                                                                                        {0}}},
                                                        {0}};

static int ble_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  if (attr_handle == ble_command_val_handle) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
      uint16_t om_len;
      om_len = OS_MBUF_PKTLEN(ctxt->om);
      if (om_len > 0) {
        uint8_t *data = malloc(om_len);
        if (data) {
          ble_hs_mbuf_to_flat(ctxt->om, data, om_len, NULL);
          ble_handle_command_bytes(data, om_len);
          free(data);
        }
      }
      return 0;
    }
  }
  return BLE_ATT_ERR_UNLIKELY;
}

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      ble_conn_handle       = event->connect.conn_handle;
      ble_connected         = true;
      notifications_enabled = false;
      negotiated_mtu        = 23;
      incoming_length       = 0;
      ESP_LOGI(TAG_BLE_API, "Client BLE connecte");

      struct ble_gap_upd_params params = {
          .itvl_min            = 6,  // 7.5 ms
          .itvl_max            = 12, // 15 ms
          .latency             = 0,
          .supervision_timeout = 400, // 4 s
          .min_ce_len          = 0,
          .max_ce_len          = 0,
      };
      int rc = ble_gap_update_params(event->connect.conn_handle, &params);
      if (rc != 0) {
        ESP_LOGW(TAG_BLE_API, "ble_gap_update_params rc=%d", rc);
      }
    } else {
      ESP_LOGW(TAG_BLE_API, "Connexion BLE echouee, statut=%d", event->connect.status);
      struct ble_gap_adv_params adv = {
          .conn_mode = BLE_GAP_CONN_MODE_UND,
          .disc_mode = BLE_GAP_DISC_MODE_GEN,
          .itvl_min  = 0,
          .itvl_max  = 0,
      };
      ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &adv, ble_gap_event, NULL);
    }
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    ble_conn_handle       = BLE_HS_CONN_HANDLE_NONE;
    ble_connected         = false;
    notifications_enabled = false;
    incoming_length       = 0;
    ESP_LOGI(TAG_BLE_API, "Client BLE deconnecte, raison=%d", event->disconnect.reason);
    {
      struct ble_gap_adv_params adv = {
          .conn_mode = BLE_GAP_CONN_MODE_UND,
          .disc_mode = BLE_GAP_DISC_MODE_GEN,
          .itvl_min  = 0,
          .itvl_max  = 0,
      };
      ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &adv, ble_gap_event, NULL);
    }
    break;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    ESP_LOGI(TAG_BLE_API, "Advertising complete");
    {
      struct ble_gap_adv_params adv = {
          .conn_mode = BLE_GAP_CONN_MODE_UND,
          .disc_mode = BLE_GAP_DISC_MODE_GEN,
          .itvl_min  = 0,
          .itvl_max  = 0,
      };
      ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &adv, ble_gap_event, NULL);
    }
    break;

  case BLE_GAP_EVENT_MTU:
    negotiated_mtu = event->mtu.value;
    ESP_LOGI(TAG_BLE_API, "MTU negocie: %u", negotiated_mtu);
    break;

  case BLE_GAP_EVENT_SUBSCRIBE:
    notifications_enabled = event->subscribe.cur_notify;
    ESP_LOGI(TAG_BLE_API, "Notifications BLE %s", notifications_enabled ? "activees" : "desactivees");
    break;
  }
  return 0;
}

static void ble_handle_command_bytes(const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    char c = (char)data[i];
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (incoming_length == 0) {
        continue;
      }
      incoming_buffer[incoming_length] = '\0';
      ESP_LOGI(TAG_BLE_API, "Complete request: %s", incoming_buffer);
      ble_dispatch_request(incoming_buffer, incoming_length);
      incoming_length = 0;
      continue;
    }
    if (incoming_length >= (BLE_MAX_REQUEST_LEN - 1)) {
      ESP_LOGW(TAG_BLE_API, "Requete BLE trop longue, purge");
      incoming_length = 0;
      ble_send_error_response(413, "PayloadTooLarge", "BLE request too large");
      return;
    }
    incoming_buffer[incoming_length++] = c;
  }
}

static void ble_dispatch_request(const char *payload, size_t len) {
  if (!request_queue) {
    ESP_LOGW(TAG_BLE_API, "File requetes BLE non disponible");
    ble_send_error_response(500, "QueueError", "Request queue unavailable");
    return;
  }
  size_t copy_len = len < (BLE_MAX_REQUEST_LEN - 1) ? len : (BLE_MAX_REQUEST_LEN - 1);
  char *buffer    = heap_caps_malloc(copy_len + 1, MALLOC_CAP_DEFAULT);
  if (!buffer) {
    ESP_LOGE(TAG_BLE_API, "Impossible d'allouer le buffer requete (%d)", copy_len + 1);
    ble_send_error_response(500, "NoMem", "Allocation failure");
    return;
  }
  memcpy(buffer, payload, copy_len);
  buffer[copy_len]              = '\0';

  ble_request_message_t message = {.length = copy_len, .payload = buffer};

  if (xQueueSend(request_queue, &message, pdMS_TO_TICKS(50)) != pdTRUE) {
    ESP_LOGW(TAG_BLE_API, "File requetes pleine");
    heap_caps_free(buffer);
    ble_send_error_response(429, "QueueFull", "BLE request queue is full");
  }
}

static void ble_request_task(void *arg) {
  ble_request_message_t message;
  while (true) {
    if (xQueueReceive(request_queue, &message, portMAX_DELAY) == pdTRUE) {
      if (message.payload) {
        ble_process_request_message(message.payload);
        heap_caps_free(message.payload);
      }
      taskYIELD();
    }
  }
}

static esp_err_t ble_http_event_handler(esp_http_client_event_t *evt) {
  ble_http_response_t *response = (ble_http_response_t *)evt->user_data;
  if (!response) {
    return ESP_OK;
  }
  switch (evt->event_id) {
  case HTTP_EVENT_ON_DATA:
    if (response->body_length + evt->data_len < BLE_MAX_RESPONSE_BODY) {
      memcpy(response->body + response->body_length, evt->data, evt->data_len);
      response->body_length += evt->data_len;
    } else {
      response->truncated = true;
      size_t remaining    = BLE_MAX_RESPONSE_BODY - response->body_length - 1;
      if (remaining > 0) {
        memcpy(response->body + response->body_length, evt->data, remaining);
        response->body_length += remaining;
      }
    }
    break;
  default:
    break;
  }
  return ESP_OK;
}

static esp_err_t ble_perform_local_http_request(const char *method, const char *path, const char *body, size_t body_len, cJSON *headers, ble_http_response_t *out_response) {
  if (!method) {
    method = "GET";
  }
  if (!path || strlen(path) == 0) {
    path = "/";
  }

  char url[256];
  const char *normalized_path = (path[0] == '/') ? path : "/";
  int written                 = snprintf(url, sizeof(url), BLE_HTTP_LOCAL_BASE_URL "%s", normalized_path);
  if (written <= 0 || written >= (int)sizeof(url)) {
    return ESP_ERR_INVALID_ARG;
  }

  memset(out_response, 0, sizeof(*out_response));
  strlcpy(out_response->status_text, "ERROR", sizeof(out_response->status_text));
  strlcpy(out_response->content_type, "application/json", sizeof(out_response->content_type));

  esp_http_client_config_t config = {
      .url           = url,
      .method        = ble_method_from_string(method),
      .timeout_ms    = BLE_HTTP_TIMEOUT_MS,
      .user_data     = out_response,
      .event_handler = ble_http_event_handler,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    return ESP_FAIL;
  }

  if (headers && cJSON_IsObject(headers)) {
    cJSON *child = headers->child;
    while (child) {
      if (cJSON_IsString(child) && child->valuestring) {
        esp_http_client_set_header(client, child->string, child->valuestring);
      }
      child = child->next;
    }
  }

  if (body && body_len > 0 && config.method != HTTP_METHOD_GET && config.method != HTTP_METHOD_HEAD) {
    esp_http_client_set_post_field(client, body, body_len);
  }

  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    out_response->status_code = esp_http_client_get_status_code(client);
    strlcpy(out_response->status_text, (out_response->status_code >= 200 && out_response->status_code < 300) ? "OK" : "HTTP_ERROR", sizeof(out_response->status_text));
    char *ct_header = NULL;
    if (esp_http_client_get_header(client, "Content-Type", &ct_header) == ESP_OK && ct_header != NULL) {
      strlcpy(out_response->content_type, ct_header, sizeof(out_response->content_type));
    }
  } else {
    ESP_LOGW(TAG_BLE_API, "Requete HTTP locale echouee: %s", esp_err_to_name(err));
    out_response->status_code = 500;
    strlcpy(out_response->status_text, "HTTP_ERROR", sizeof(out_response->status_text));
  }

  if (out_response->body_length < BLE_MAX_RESPONSE_BODY) {
    out_response->body[out_response->body_length] = '\0';
  } else {
    out_response->body[BLE_MAX_RESPONSE_BODY - 1] = '\0';
  }

  esp_http_client_cleanup(client);
  return err;
}

static void ble_process_request_message(const char *json) {
  ESP_LOGI(TAG_BLE_API, "Processing request: %s", json);
  cJSON *root = cJSON_Parse(json);
  if (!root) {
    ESP_LOGW(TAG_BLE_API, "JSON BLE invalide: %s", json);
    ble_send_error_response(400, "BadJSON", "Unable to parse BLE payload");
    return;
  }

  const char *method = cJSON_GetStringValue(cJSON_GetObjectItem(root, "method"));
  const char *path   = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
  const char *body   = cJSON_GetStringValue(cJSON_GetObjectItem(root, "body"));
  size_t body_len    = body ? strlen(body) : 0;
  cJSON *headers     = cJSON_GetObjectItem(root, "headers");

  ESP_LOGI(TAG_BLE_API, "HTTP %s %s", method ? method : "GET", path ? path : "/");

  ble_http_response_t *response = heap_caps_malloc(sizeof(ble_http_response_t), MALLOC_CAP_DEFAULT);
  if (response == NULL) {
    ESP_LOGE(TAG_BLE_API, "Impossible d'allouer la reponse HTTP");
    cJSON_Delete(root);
    ble_send_error_response(500, "NoMem", "Out of memory");
    return;
  }

  esp_err_t err = ble_perform_local_http_request(method, path, body, body_len, headers, response);
  ESP_LOGI(TAG_BLE_API, "HTTP response: status=%d, body_len=%d", response->status_code, response->body_length);

  cJSON *json_response = cJSON_CreateObject();
  cJSON_AddNumberToObject(json_response, "status", response->status_code);
  cJSON_AddStringToObject(json_response, "statusText", response->status_text);

  cJSON *headers_json = cJSON_CreateObject();
  cJSON_AddStringToObject(headers_json, "Content-Type", response->content_type);
  if (response->truncated) {
    cJSON_AddBoolToObject(headers_json, "Truncated", true);
  }
  cJSON_AddItemToObject(json_response, "headers", headers_json);
  cJSON_AddStringToObject(json_response, "body", response->body);

  if (err != ESP_OK) {
    cJSON_AddStringToObject(json_response, "error", esp_err_to_name(err));
  }

  ESP_LOGI(TAG_BLE_API, "Sending JSON response");
  ble_send_json_response(json_response);
  cJSON_Delete(root);
  heap_caps_free(response);
}

static void ble_send_json_response(cJSON *object) {
  char *json_str = cJSON_PrintUnformatted(object);
  cJSON_Delete(object);
  if (!json_str) {
    return;
  }
  ble_send_text_response(json_str);
  cJSON_free(json_str);
}

static void ble_send_error_response(int status, const char *status_text, const char *message) {
  cJSON *response = cJSON_CreateObject();
  cJSON_AddNumberToObject(response, "status", status);
  cJSON_AddStringToObject(response, "statusText", status_text ? status_text : "ERROR");
  cJSON *headers = cJSON_CreateObject();
  cJSON_AddStringToObject(headers, "Content-Type", "application/json");
  cJSON_AddItemToObject(response, "headers", headers);
  cJSON_AddStringToObject(response, "body", message ? message : "");
  ble_send_json_response(response);
}

static bool ble_send_notification(const uint8_t *data, size_t len) {
  if (!ble_connected || !notifications_enabled || ble_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    ESP_LOGW(TAG_BLE_API, "Impossible d'envoyer notification (connexion inactive)");
    return false;
  }
  if (len == 0) {
    return true;
  }

  // Utiliser MTU - 3 pour l'overhead BLE, avec un maximum de 244 bytes
  // (244 est une taille sûre qui évite les problèmes de fragmentation)
  size_t max_payload = negotiated_mtu > 3 ? (negotiated_mtu - 3) : 20;
  if (max_payload > 244) {
    max_payload = 244;
  }

  struct os_mbuf *om;
  size_t offset   = 0;
  int chunk_count = 0;

  while (offset < len) {
    size_t chunk = (len - offset) > max_payload ? max_payload : (len - offset);

    om           = ble_hs_mbuf_from_flat(data + offset, chunk);
    if (!om) {
      ESP_LOGE(TAG_BLE_API, "Erreur allocation mbuf (chunk %d/%d, offset=%d)", chunk_count + 1, (len + max_payload - 1) / max_payload, offset);
      // Attendre un peu plus et réessayer une fois
      vTaskDelay(pdMS_TO_TICKS(50));
      om = ble_hs_mbuf_from_flat(data + offset, chunk);
      if (!om) {
        ESP_LOGE(TAG_BLE_API, "Erreur allocation mbuf après retry");
        return false;
      }
    }

    // Revérifier l'état avant chaque envoi
    if (!ble_connected || !notifications_enabled || ble_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
      ESP_LOGW(TAG_BLE_API, "Connexion perdue pendant l'envoi (chunk %d)", chunk_count);
      os_mbuf_free_chain(om);
      return false;
    }

    int rc = ble_gattc_notify_custom(ble_conn_handle, ble_response_val_handle, om);
    if (rc != 0) {
      ESP_LOGE(TAG_BLE_API, "Erreur notification BLE: %d (chunk %d/%d)", rc, chunk_count + 1, (len + max_payload - 1) / max_payload);
      return false;
    }

    chunk_count++;
    offset += chunk;

    // Délai entre chunks pour éviter de saturer le buffer BLE
    // Plus le transfert est gros, plus on attend pour laisser le temps au client de traiter
    if (offset < len) { // Pas de délai après le dernier chunk
      if (len > 2000) {
        vTaskDelay(pdMS_TO_TICKS(30)); // 30ms pour les très gros transferts
      } else if (len > 500) {
        vTaskDelay(pdMS_TO_TICKS(20)); // 20ms pour les transferts moyens
      } else {
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms pour les petits transferts
      }
    }
  }

  return true;
}

static void ble_send_text_response(const char *text) {
  if (!text) {
    ESP_LOGW(TAG_BLE_API, "Null text response");
    return;
  }
  size_t len = strlen(text);
  ESP_LOGD(TAG_BLE_API, "Sending text response: %d bytes", len);
  if (len > 0) {
    bool sent = ble_send_notification((const uint8_t *)text, len);
    if (!sent) {
      ESP_LOGE(TAG_BLE_API, "Failed to send response body");
    }
  }
  const uint8_t newline = '\n';
  ble_send_notification(&newline, 1);
}

static void ble_on_sync(void) {
  int rc;

  // Générer et configurer une adresse aléatoire statique
  ble_addr_t addr;
  rc = ble_hs_id_gen_rnd(1, &addr);
  if (rc != 0) {
    ESP_LOGE(TAG_BLE_API, "Erreur generation adresse aleatoire: %d", rc);
    return;
  }

  rc = ble_hs_id_set_rnd(addr.val);
  if (rc != 0) {
    ESP_LOGE(TAG_BLE_API, "Erreur configuration adresse: %d", rc);
    return;
  }

  ESP_LOGI(TAG_BLE_API, "Adresse BLE: %02x:%02x:%02x:%02x:%02x:%02x", addr.val[5], addr.val[4], addr.val[3], addr.val[2], addr.val[1], addr.val[0]);

  struct ble_hs_adv_fields fields = {0};
  fields.flags                    = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.name                     = (uint8_t *)g_device_name_with_suffix;
  fields.name_len                 = strlen(g_device_name_with_suffix);
  fields.name_is_complete         = 1;

  rc                              = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG_BLE_API, "Erreur config advertising: %d", rc);
    return;
  }

  struct ble_gap_adv_params adv_params = {0};
  adv_params.conn_mode                 = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode                 = BLE_GAP_DISC_MODE_GEN;
  adv_params.itvl_min                  = 0; // Use default min interval
  adv_params.itvl_max                  = 0; // Use default max interval

  // Utiliser une adresse aléatoire statique (BLE_OWN_ADDR_RANDOM)
  rc                                   = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG_BLE_API, "Erreur demarrage advertising: %d", rc);
    return;
  }

  ESP_LOGI(TAG_BLE_API, "Advertising BLE demarre");
}

static void ble_on_reset(int reason) {
  ESP_LOGE(TAG_BLE_API, "BLE reset, raison: %d", reason);
}

static void ble_host_task(void *param) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

esp_err_t ble_api_service_init(void) {
  if (ble_started) {
    return ESP_OK;
  }

  request_queue = xQueueCreate(BLE_REQUEST_QUEUE_LENGTH, sizeof(ble_request_message_t));
  if (!request_queue) {
    ESP_LOGE(TAG_BLE_API, "Impossible de creer la file des requetes BLE");
    return ESP_ERR_NO_MEM;
  }

  BaseType_t task_created = xTaskCreatePinnedToCore(ble_request_task, "ble_api_req", 12288, NULL, 7, &request_task_handle, tskNO_AFFINITY);
  if (task_created != pdPASS) {
    ESP_LOGE(TAG_BLE_API, "Impossible de creer la tache BLE API");
    vQueueDelete(request_queue);
    request_queue = NULL;
    return ESP_FAIL;
  }

  esp_err_t ret;

  ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_BLE_API, "nimble_port_init echoue: %s", esp_err_to_name(ret));
    return ret;
  }

  ble_hs_cfg.sync_cb  = ble_on_sync;
  ble_hs_cfg.reset_cb = ble_on_reset;

  ble_svc_gap_device_name_set(g_device_name_with_suffix);
  ble_svc_gap_init();
  ble_svc_gatt_init();

  int rc = ble_gatts_count_cfg(ble_gatt_svcs);
  if (rc != 0) {
    ESP_LOGE(TAG_BLE_API, "ble_gatts_count_cfg echoue: %d", rc);
    return ESP_FAIL;
  }

  rc = ble_gatts_add_svcs(ble_gatt_svcs);
  if (rc != 0) {
    ESP_LOGE(TAG_BLE_API, "ble_gatts_add_svcs echoue: %d", rc);
    return ESP_FAIL;
  }

  nimble_port_freertos_init(ble_host_task);

  ble_started = true;
  ESP_LOGI(TAG_BLE_API, "Service BLE NimBLE initialise");
  return ESP_OK;
}

esp_err_t ble_api_service_start(void) {
  if (!ble_started) {
    ESP_LOGW(TAG_BLE_API, "Service BLE non initialise");
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}

esp_err_t ble_api_service_stop(void) {
  if (!ble_started) {
    return ESP_OK;
  }

  if (ble_connected) {
    ble_gap_terminate(ble_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
  }

  ble_gap_adv_stop();
  return ESP_OK;
}

bool ble_api_service_is_connected(void) {
  return ble_connected;
}

#else

esp_err_t ble_api_service_init(void) {
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ble_api_service_start(void) {
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ble_api_service_stop(void) {
  return ESP_ERR_NOT_SUPPORTED;
}

bool ble_api_service_is_connected(void) {
  return false;
}

#endif
