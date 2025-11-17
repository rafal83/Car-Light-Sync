#include "ble_api_service.h"
#include "sdkconfig.h"

#if CONFIG_BT_ENABLED

#include <string.h>
#include <strings.h>
#include <stdio.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "cJSON.h"

#define BLE_API_TAG                   "BLE_API"
#define BLE_API_DEVICE_NAME           "TESLA-STRIP"
#define BLE_HTTP_LOCAL_BASE_URL       "http://127.0.0.1"
#define BLE_MAX_REQUEST_LEN           16384
#define BLE_MAX_RESPONSE_BODY         8192
#define BLE_NOTIFY_CHUNK_MAX          120
#define BLE_REQUEST_QUEUE_LENGTH      3
#define BLE_HTTP_TIMEOUT_MS           8000

enum {
    BLE_IDX_SERVICE,
    BLE_IDX_CHAR_COMMAND,
    BLE_IDX_CHAR_COMMAND_VAL,
    BLE_IDX_CHAR_RESPONSE,
    BLE_IDX_CHAR_RESPONSE_VAL,
    BLE_IDX_CHAR_RESPONSE_CFG,
    BLE_IDX_TOTAL,
};

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

static const uint8_t ble_service_uuid[ESP_UUID_LEN_128] = {
    0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
    0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f
};

static const uint8_t ble_command_uuid[ESP_UUID_LEN_128] = {
    0xa8, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
    0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe
};

static const uint8_t ble_response_uuid[ESP_UUID_LEN_128] = {
    0xdc, 0xa9, 0x4b, 0x6f, 0x82, 0xea, 0x30, 0xaa,
    0x1b, 0x4c, 0xeb, 0x52, 0x0c, 0x99, 0xa0, 0x64
};

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static esp_gatts_attr_db_t ble_gatt_db[BLE_IDX_TOTAL] = {
    [BLE_IDX_SERVICE] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
         sizeof(ble_service_uuid), sizeof(ble_service_uuid), (uint8_t *)ble_service_uuid}
    },
    [BLE_IDX_CHAR_COMMAND] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t),
         (uint8_t *)&(uint8_t){ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR}}
    },
    [BLE_IDX_CHAR_COMMAND_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {ESP_UUID_LEN_128, (uint8_t *)ble_command_uuid,
         ESP_GATT_PERM_WRITE,
         BLE_MAX_REQUEST_LEN, 0, NULL}
    },
    [BLE_IDX_CHAR_RESPONSE] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t),
         (uint8_t *)&(uint8_t){ESP_GATT_CHAR_PROP_BIT_NOTIFY}}
    },
    [BLE_IDX_CHAR_RESPONSE_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {ESP_UUID_LEN_128, (uint8_t *)ble_response_uuid,
         ESP_GATT_PERM_READ,
         BLE_MAX_RESPONSE_BODY, 0, NULL}
    },
    [BLE_IDX_CHAR_RESPONSE_CFG] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(uint16_t), sizeof(uint16_t), (uint8_t *)&(uint16_t){0}}
    },
};

static esp_ble_adv_data_t ble_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x20,
    .max_interval = 0x40,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(ble_service_uuid),
    .p_service_uuid = (uint8_t *)ble_service_uuid,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t ble_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static esp_gatt_if_t ble_gatts_if = ESP_GATT_IF_NONE;
static uint16_t ble_handle_table[BLE_IDX_TOTAL];
static uint16_t ble_conn_id = 0xFFFF;
static bool ble_connected = false;
static bool notifications_enabled = false;
static uint16_t negotiated_mtu = 23;
static bool ble_started = false;
static QueueHandle_t request_queue = NULL;
static TaskHandle_t request_task_handle = NULL;
static char incoming_buffer[BLE_MAX_REQUEST_LEN];
static size_t incoming_length = 0;

static void ble_wait_for_send_window(void) {
    if (!ble_connected) {
        return;
    }
    int attempts = 0;
    while (esp_ble_get_cur_sendable_packets_num(ble_conn_id) == 0 && attempts < 200) {
        vTaskDelay(pdMS_TO_TICKS(5));
        attempts++;
    }
}

static void ble_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void ble_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                    esp_ble_gatts_cb_param_t *param);
static void ble_request_task(void *arg);
static void ble_handle_command_bytes(const uint8_t *data, size_t length);
static void ble_dispatch_request(const char *payload, size_t len);
static void ble_process_request_message(const char *json);
static void ble_send_json_response(cJSON *object);
static void ble_send_text_response(const char *text);
static void ble_send_error_response(int status, const char *status_text, const char *message);

static bool ble_send_notification(const uint8_t *data, size_t len);
static void ble_restart_advertising(void);
static esp_err_t ble_perform_local_http_request(const char *method,
                                                const char *path,
                                                const char *body,
                                                size_t body_len,
                                                cJSON *headers,
                                                ble_http_response_t *out_response);
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

static void ble_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            ble_restart_advertising();
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(BLE_API_TAG, "Echec demarrage advertising: %s",
                         esp_err_to_name(param->adv_start_cmpl.status));
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(BLE_API_TAG, "Echec arret advertising: %s",
                         esp_err_to_name(param->adv_stop_cmpl.status));
            }
            break;
        default:
            break;
    }
}

static void ble_restart_advertising(void) {
    if (!ble_started) {
        return;
    }
    esp_ble_gap_start_advertising(&ble_adv_params);
}

static void ble_handle_write_event(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    if (param->write.handle == ble_handle_table[BLE_IDX_CHAR_COMMAND_VAL]) {
        ble_handle_command_bytes(param->write.value, param->write.len);
    } else if (param->write.handle == ble_handle_table[BLE_IDX_CHAR_RESPONSE_CFG]) {
        uint16_t value = param->write.value[1] << 8 | param->write.value[0];
        notifications_enabled = (value != 0);
        ESP_LOGI(BLE_API_TAG, "Notifications BLE %s",
                 notifications_enabled ? "activees" : "desactivees");
    }

    if (param->write.need_rsp) {
        esp_gatt_rsp_t rsp = {0};
        rsp.attr_value.handle = param->write.handle;
        rsp.attr_value.len = 0;
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                    param->write.trans_id, ESP_GATT_OK, &rsp);
    }
}

static void ble_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                    esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            ble_gatts_if = gatts_if;
            esp_ble_gap_set_device_name(BLE_API_DEVICE_NAME);
            esp_ble_gap_config_adv_data(&ble_adv_data);
            esp_ble_gatts_create_attr_tab(ble_gatt_db, gatts_if, BLE_IDX_TOTAL, 0);
            break;
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status != ESP_GATT_OK) {
                ESP_LOGE(BLE_API_TAG, "Creation table GATT echouee, statut 0x%x",
                         param->add_attr_tab.status);
            } else {
                memcpy(ble_handle_table, param->add_attr_tab.handles,
                       sizeof(ble_handle_table));
                esp_ble_gatts_start_service(ble_handle_table[BLE_IDX_SERVICE]);
            }
            break;
        case ESP_GATTS_CONNECT_EVT:
            ble_conn_id = param->connect.conn_id;
            ble_connected = true;
            notifications_enabled = false;
            negotiated_mtu = 23;
            ESP_LOGI(BLE_API_TAG, "Client BLE connecte");
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            ble_conn_id = 0xFFFF;
            ble_connected = false;
            notifications_enabled = false;
            incoming_length = 0;
            ESP_LOGI(BLE_API_TAG, "Client BLE deconnecte");
            ble_restart_advertising();
            break;
        case ESP_GATTS_WRITE_EVT:
            ble_handle_write_event(gatts_if, param);
            break;
        case ESP_GATTS_MTU_EVT:
            negotiated_mtu = param->mtu.mtu;
            ESP_LOGI(BLE_API_TAG, "MTU negocie: %u", negotiated_mtu);
            break;
        default:
            break;
    }
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
            ble_dispatch_request(incoming_buffer, incoming_length);
            incoming_length = 0;
            continue;
        }
        if (incoming_length >= (BLE_MAX_REQUEST_LEN - 1)) {
            ESP_LOGW(BLE_API_TAG, "Requete BLE trop longue, purge");
            incoming_length = 0;
            ble_send_error_response(413, "PayloadTooLarge", "BLE request too large");
            return;
        }
        incoming_buffer[incoming_length++] = c;
    }
}

static void ble_dispatch_request(const char *payload, size_t len) {
    if (!request_queue) {
        ESP_LOGW(BLE_API_TAG, "File requetes BLE non disponible");
        ble_send_error_response(500, "QueueError", "Request queue unavailable");
        return;
    }
    size_t copy_len = len < (BLE_MAX_REQUEST_LEN - 1) ? len : (BLE_MAX_REQUEST_LEN - 1);
    char *buffer = heap_caps_malloc(copy_len + 1, MALLOC_CAP_DEFAULT);
    if (!buffer) {
        ESP_LOGE(BLE_API_TAG, "Impossible d'allouer le buffer requete (%d)", copy_len + 1);
        ble_send_error_response(500, "NoMem", "Allocation failure");
        return;
    }
    memcpy(buffer, payload, copy_len);
    buffer[copy_len] = '\0';

    ble_request_message_t message = {
        .length = copy_len,
        .payload = buffer
    };

    if (xQueueSend(request_queue, &message, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(BLE_API_TAG, "File requetes pleine");
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
            vTaskDelay(pdMS_TO_TICKS(10));
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
                size_t remaining = BLE_MAX_RESPONSE_BODY - response->body_length - 1;
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

static esp_err_t ble_perform_local_http_request(const char *method,
                                                const char *path,
                                                const char *body,
                                                size_t body_len,
                                                cJSON *headers,
                                                ble_http_response_t *out_response) {
    if (!method) {
        method = "GET";
    }
    if (!path || strlen(path) == 0) {
        path = "/";
    }

    char url[256];
    const char *normalized_path = (path[0] == '/') ? path : "/";
    int written = snprintf(url, sizeof(url), BLE_HTTP_LOCAL_BASE_URL "%s", normalized_path);
    if (written <= 0 || written >= (int)sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_response, 0, sizeof(*out_response));
    strlcpy(out_response->status_text, "ERROR", sizeof(out_response->status_text));
    strlcpy(out_response->content_type, "application/json", sizeof(out_response->content_type));

    esp_http_client_config_t config = {
        .url = url,
        .method = ble_method_from_string(method),
        .timeout_ms = BLE_HTTP_TIMEOUT_MS,
        .user_data = out_response,
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

    if (body && body_len > 0 &&
        config.method != HTTP_METHOD_GET &&
        config.method != HTTP_METHOD_HEAD) {
        esp_http_client_set_post_field(client, body, body_len);
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        out_response->status_code = esp_http_client_get_status_code(client);
        strlcpy(out_response->status_text,
                (out_response->status_code >= 200 && out_response->status_code < 300) ? "OK" : "HTTP_ERROR",
                sizeof(out_response->status_text));
        char *ct_header = NULL;
        if (esp_http_client_get_header(client, "Content-Type", &ct_header) == ESP_OK &&
            ct_header != NULL) {
            strlcpy(out_response->content_type, ct_header, sizeof(out_response->content_type));
        }
    } else {
        ESP_LOGW(BLE_API_TAG, "Requete HTTP locale echouee: %s", esp_err_to_name(err));
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
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(BLE_API_TAG, "JSON BLE invalide: %s", json);
        ble_send_error_response(400, "BadJSON", "Unable to parse BLE payload");
        return;
    }

    const char *method = cJSON_GetStringValue(cJSON_GetObjectItem(root, "method"));
    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *body = cJSON_GetStringValue(cJSON_GetObjectItem(root, "body"));
    size_t body_len = body ? strlen(body) : 0;
    cJSON *headers = cJSON_GetObjectItem(root, "headers");

    ble_http_response_t *response = heap_caps_malloc(sizeof(ble_http_response_t), MALLOC_CAP_DEFAULT);
    if (response == NULL) {
        ESP_LOGE(BLE_API_TAG, "Impossible d'allouer la reponse HTTP");
        cJSON_Delete(root);
        ble_send_error_response(500, "NoMem", "Out of memory");
        return;
    }

    esp_err_t err = ble_perform_local_http_request(method, path, body, body_len, headers, response);

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
    if (!ble_connected || !notifications_enabled || ble_gatts_if == ESP_GATT_IF_NONE ||
        ble_handle_table[BLE_IDX_CHAR_RESPONSE_VAL] == 0) {
        ESP_LOGW(BLE_API_TAG, "Impossible d'envoyer notification (connexion inactive)");
        return false;
    }
    if (len == 0) {
        return true;
    }

    size_t max_payload = negotiated_mtu > 3 ? (negotiated_mtu - 3) : 20;
    if (max_payload > BLE_NOTIFY_CHUNK_MAX) {
        max_payload = BLE_NOTIFY_CHUNK_MAX;
    }
    if (max_payload == 0) {
        max_payload = 20;
    }

    size_t offset = 0;
    while (offset < len) {
        size_t chunk = (len - offset) > max_payload ? max_payload : (len - offset);
        esp_err_t err;
        int retries = 0;
        do {
            ble_wait_for_send_window();
            err = esp_ble_gatts_send_indicate(
                ble_gatts_if,
                ble_conn_id,
                ble_handle_table[BLE_IDX_CHAR_RESPONSE_VAL],
                chunk,
                (uint8_t *)(data + offset),
                false);
            if (err == ESP_GATT_CONGESTED || err == ESP_GATT_BUSY || err == ESP_GATT_ERROR) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        } while ((err == ESP_GATT_CONGESTED || err == ESP_GATT_BUSY || err == ESP_GATT_ERROR) && retries++ < 12);
        if (err != ESP_OK) {
            ESP_LOGE(BLE_API_TAG, "Erreur notification BLE: %s", esp_err_to_name(err));
            return false;
        }
        offset += chunk;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

static void ble_send_text_response(const char *text) {
    if (!text) {
        return;
    }
    size_t len = strlen(text);
    if (len > 0) {
        ble_send_notification((const uint8_t *)text, len);
    }
    const uint8_t newline = '\n';
    ble_send_notification(&newline, 1);
}

esp_err_t ble_api_service_init(void) {
    if (ble_started) {
        return ESP_OK;
    }

    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(BLE_API_TAG, "Impossible de liberer la RAM BT classic: %s", esp_err_to_name(ret));
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(BLE_API_TAG, "esp_bt_controller_init echoue: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(BLE_API_TAG, "esp_bt_controller_enable echoue: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(BLE_API_TAG, "esp_bluedroid_init echoue: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(BLE_API_TAG, "esp_bluedroid_enable echoue: %s", esp_err_to_name(ret));
        return ret;
    }

    request_queue = xQueueCreate(BLE_REQUEST_QUEUE_LENGTH, sizeof(ble_request_message_t));
    if (!request_queue) {
        ESP_LOGE(BLE_API_TAG, "Impossible de creer la file des requetes BLE");
        ble_started = false;
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_created = xTaskCreatePinnedToCore(
        ble_request_task,
        "ble_api_req",
        12288,
        NULL,
        7,
        &request_task_handle,
        tskNO_AFFINITY);
    if (task_created != pdPASS) {
        ESP_LOGE(BLE_API_TAG, "Impossible de creer la tache BLE API");
        vQueueDelete(request_queue);
        request_queue = NULL;
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(ble_gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(ble_gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0));

    ble_started = true;
    return ESP_OK;
}

esp_err_t ble_api_service_start(void) {
    if (!ble_started) {
        ESP_LOGW(BLE_API_TAG, "Service BLE non initialise");
        return ESP_ERR_INVALID_STATE;
    }
    ble_restart_advertising();
    ESP_LOGI(BLE_API_TAG, "Advertising BLE demarre");
    return ESP_OK;
}

esp_err_t ble_api_service_stop(void) {
    if (!ble_started) {
        return ESP_OK;
    }
    esp_ble_gap_stop_advertising();
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
