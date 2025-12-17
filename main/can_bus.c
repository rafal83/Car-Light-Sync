// can_bus.c
#include "can_bus.h"

#include "canserver_udp_server.h"
#include "esp_log.h"
#include "espnow_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gvret_tcp_server.h"
#include "vehicle_can_mapping.h"

// CAN driver ESP-IDF: depending on version it's "twai" or alias "can"
#include "driver/twai.h"

// Structure to manage each CAN bus
typedef struct {
  int tx_gpio;
  int rx_gpio;
  TaskHandle_t rx_task_handle;
  volatile uint32_t rx_count;
  volatile uint32_t tx_count;
  volatile uint32_t errors;
  volatile TickType_t last_rx_tick;
  volatile bool rx_active;
  volatile bool running;
  volatile bool initialized;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
  twai_handle_t bus_handle; // Handle pour TWAI v2
#endif
} can_bus_context_t;

// Contexts for each CAN bus
static can_bus_context_t s_can_buses[CAN_BUS_COUNT] = {0};

// Shared callback for all buses
static can_bus_callback_t s_callback                = NULL;
static void *s_cb_user_data                         = NULL;

// Structure passed to RX tasks
typedef struct {
  can_bus_type_t bus_type;
} can_rx_task_params_t;

// ---- RX Task ----
static void can_rx_task(void *pvParameters) {
  can_rx_task_params_t *params = (can_rx_task_params_t *)pvParameters;
  can_bus_type_t bus_type      = params->bus_type;

  // Free allocated parameters
  free(params);

  can_bus_context_t *ctx = &s_can_buses[bus_type];
  const char *bus_name   = (bus_type == CAN_BUS_BODY) ? "BODY" : "CHASSIS";

  ESP_LOGI(TAG_CAN_BUS, "CAN RX task started for bus %s (GPIO TX=%d RX=%d)", bus_name, ctx->tx_gpio, ctx->rx_gpio);

  while (ctx->running) {
    twai_message_t msg;
    // Block until reception
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0) && SOC_TWAI_CONTROLLER_NUM >= 2
    esp_err_t ret = twai_receive_v2(ctx->bus_handle, &msg, pdMS_TO_TICKS(1000));
#else
    esp_err_t ret = twai_receive(&msg, pdMS_TO_TICKS(1000));
#endif

    if (ret == ESP_OK) {
      ctx->rx_count++;
      ctx->last_rx_tick = xTaskGetTickCount();
      ctx->rx_active    = true;

      // Broadcast to GVRET TCP clients (if server active)
      gvret_tcp_broadcast_can_frame((int)bus_type, &msg);

      // Broadcast to CANServer TCP clients (if server active)
      canserver_udp_broadcast_can_frame((int)bus_type, &msg);

      if (s_callback) {
        can_frame_t frame = {0};
        frame.id          = msg.identifier;
        frame.dlc         = msg.data_length_code;
        if (frame.dlc > 8)
          frame.dlc = 8;
        for (int i = 0; i < frame.dlc; i++) {
          frame.data[i] = msg.data[i];
        }
        frame.timestamp_ms = xTaskGetTickCount();
        frame.bus_id       = (uint8_t)bus_type; // 0=CAN0, 1=CAN1

        s_callback(&frame, bus_type, s_cb_user_data);
      }
    } else if (ret == ESP_ERR_TIMEOUT) {
      continue;
    } else {
      ctx->errors++;
      ESP_LOGW(TAG_CAN_BUS, "[%s] twai_receive error: %s", bus_name, esp_err_to_name(ret));
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  ESP_LOGI(TAG_CAN_BUS, "CAN RX task finished for bus %s", bus_name);
  ctx->rx_task_handle = NULL;
  vTaskDelete(NULL);
}

// ---- API publique ----

esp_err_t can_bus_init(can_bus_type_t bus_type, int tx_gpio, int rx_gpio) {
  if (bus_type >= CAN_BUS_COUNT) {
    return ESP_ERR_INVALID_ARG;
  }

  can_bus_context_t *ctx = &s_can_buses[bus_type];
  const char *bus_name   = (bus_type == CAN_BUS_BODY) ? "BODY" : "CHASSIS";

  if (ctx->initialized) {
    ESP_LOGW(TAG_CAN_BUS, "Bus %s already initialized", bus_name);
    return ESP_ERR_INVALID_STATE;
  }

  ctx->tx_gpio  = tx_gpio;
  ctx->rx_gpio  = rx_gpio;
  ctx->rx_count = ctx->tx_count = ctx->errors = 0;
  ctx->last_rx_tick                           = 0;
  ctx->rx_active                              = false;
  ctx->running                                = false;
  ctx->rx_task_handle                         = NULL;

  // General config: NORMAL mode (allows TX/RX for GVRET, Car Light Sync uses RX only)
  twai_general_config_t g_config              = TWAI_GENERAL_CONFIG_DEFAULT(tx_gpio, rx_gpio, TWAI_MODE_NORMAL);

  // Vitesse 500 kbit/s (Tesla)
  twai_timing_config_t t_config               = TWAI_TIMING_CONFIG_500KBITS();

  // Filter: accept all frames by default
  twai_filter_config_t f_config               = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t ret;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0) && SOC_TWAI_CONTROLLER_NUM >= 2
  // ESP-IDF 5.2.0+ with multi-controller support
  g_config.controller_id = bus_type;

  ret                    = twai_driver_install_v2(&g_config, &t_config, &f_config, &ctx->bus_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_CAN_BUS, "[%s] twai_driver_install_v2 failed: %s", bus_name, esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG_CAN_BUS, "CAN driver %s installed with TWAI v2 (TX=%d, RX=%d, ID=%d)", bus_name, tx_gpio, rx_gpio, bus_type);
#else
  // Older ESP-IDF versions: only one controller supported
  if (bus_type == CAN_BUS_CHASSIS) {
    ESP_LOGW(TAG_CAN_BUS,
             "[%s] Second CAN bus requires ESP-IDF 5.2.0+ or an external controller",
             bus_name);
    ESP_LOGW(TAG_CAN_BUS, "[%s] Feature disabled", bus_name);
    ctx->initialized = true;
    return ESP_OK;
  }

  ret = twai_driver_install(&g_config, &t_config, &f_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG_CAN_BUS, "[%s] twai_driver_install failed: %s", bus_name, esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG_CAN_BUS, "CAN driver %s installed (TX=%d, RX=%d)", bus_name, tx_gpio, rx_gpio);
#endif

  ctx->initialized = true;
  return ESP_OK;
}

esp_err_t can_bus_start(can_bus_type_t bus_type) {
  if (bus_type >= CAN_BUS_COUNT) {
    return ESP_ERR_INVALID_ARG;
  }

  can_bus_context_t *ctx = &s_can_buses[bus_type];
  const char *bus_name   = (bus_type == CAN_BUS_BODY) ? "BODY" : "CHASSIS";

  if (!ctx->initialized) {
    ESP_LOGE(TAG_CAN_BUS, "[%s] Bus not initialized", bus_name);
    return ESP_ERR_INVALID_STATE;
  }

  if (ctx->running) {
    ESP_LOGW(TAG_CAN_BUS, "[%s] Bus already started", bus_name);
    return ESP_OK;
  }

#if !(ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0) && SOC_TWAI_CONTROLLER_NUM >= 2)
  // Skip for BODY bus if multi-controller not supported
  if (bus_type == CAN_BUS_CHASSIS) {
    ESP_LOGW(TAG_CAN_BUS, "[%s] Bus disabled (requires ESP-IDF 5.2.0+)", bus_name);
    return ESP_OK;
  }
#endif

  esp_err_t ret;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0) && SOC_TWAI_CONTROLLER_NUM >= 2
  ret = twai_start_v2(ctx->bus_handle);
#else
  ret = twai_start();
#endif

  if (ret != ESP_OK) {
    ESP_LOGE(TAG_CAN_BUS, "[%s] twai_start failed: %s", bus_name, esp_err_to_name(ret));
    return ret;
  }

  ctx->running = true;

  // Create the RX task
  if (ctx->rx_task_handle == NULL) {
    can_rx_task_params_t *params = malloc(sizeof(can_rx_task_params_t));
    if (!params) {
      ESP_LOGE(TAG_CAN_BUS, "[%s] Memory allocation error", bus_name);
      return ESP_ERR_NO_MEM;
    }
    params->bus_type = bus_type;

    char task_name[20];
    snprintf(task_name, sizeof(task_name), "can_rx_%s", bus_name);

    xTaskCreatePinnedToCore(can_rx_task,
                            task_name,
                            4096,
                            params,
                            10,
                            &ctx->rx_task_handle,
                            0 // general core
    );
  }

  ESP_LOGI(TAG_CAN_BUS, "CAN bus %s started", bus_name);

  return ESP_OK;
}

esp_err_t can_bus_stop(can_bus_type_t bus_type) {
  if (bus_type >= CAN_BUS_COUNT) {
    return ESP_ERR_INVALID_ARG;
  }

  can_bus_context_t *ctx = &s_can_buses[bus_type];
  const char *bus_name   = (bus_type == CAN_BUS_BODY) ? "BODY" : "CHASSIS";

  ctx->running           = false;

  // Wait for the task to finish
  if (ctx->rx_task_handle) {
    vTaskDelay(pdMS_TO_TICKS(100)); // Let the task complete
  }

  esp_err_t ret;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0) && SOC_TWAI_CONTROLLER_NUM >= 2
  ret = twai_stop_v2(ctx->bus_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG_CAN_BUS, "[%s] twai_stop_v2: %s", bus_name, esp_err_to_name(ret));
  }

  ret = twai_driver_uninstall_v2(ctx->bus_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG_CAN_BUS, "[%s] twai_driver_uninstall_v2: %s", bus_name, esp_err_to_name(ret));
  }
#else
  ret = twai_stop();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG_CAN_BUS, "[%s] twai_stop: %s", bus_name, esp_err_to_name(ret));
  }

  ret = twai_driver_uninstall();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG_CAN_BUS, "[%s] twai_driver_uninstall: %s", bus_name, esp_err_to_name(ret));
  }
#endif

  ctx->initialized = false;

  return ESP_OK;
}

esp_err_t can_bus_register_callback(can_bus_callback_t cb, void *user_data) {
  s_callback     = cb;
  s_cb_user_data = user_data;
  return ESP_OK;
}

esp_err_t can_bus_send(can_bus_type_t bus_type, const can_frame_t *frame) {
  if (bus_type >= CAN_BUS_COUNT || !frame) {
    return ESP_ERR_INVALID_ARG;
  }

  can_bus_context_t *ctx = &s_can_buses[bus_type];

  if (!ctx->running) {
    return ESP_ERR_INVALID_STATE;
  }

  twai_message_t msg   = {0};
  msg.identifier       = frame->id;
  msg.data_length_code = frame->dlc;
  if (msg.data_length_code > 8)
    msg.data_length_code = 8;

  msg.extd = 0; // Tesla = ID standard 11 bits
  msg.rtr  = 0; // pas de Remote Transmission Request

  for (int i = 0; i < msg.data_length_code; i++) {
    msg.data[i] = frame->data[i];
  }

  esp_err_t ret;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0) && SOC_TWAI_CONTROLLER_NUM >= 2
  ret = twai_transmit_v2(ctx->bus_handle, &msg, pdMS_TO_TICKS(10));
#else
  ret = twai_transmit(&msg, pdMS_TO_TICKS(10));
#endif

  if (ret != ESP_OK) {
    ctx->errors++;
    const char *bus_name = (bus_type == CAN_BUS_BODY) ? "BODY" : "CHASSIS";
    ESP_LOGW(TAG_CAN_BUS, "[%s] Erreur twai_transmit: %s", bus_name, esp_err_to_name(ret));
    return ret;
  }

  ctx->tx_count++;
  return ESP_OK;
}

esp_err_t can_bus_get_status(can_bus_type_t bus_type, can_bus_status_t *out) {
  if (bus_type >= CAN_BUS_COUNT || !out) {
    return ESP_ERR_INVALID_ARG;
  }

  can_bus_context_t *ctx = &s_can_buses[bus_type];

  out->rx_count          = ctx->rx_count;
  out->tx_count          = ctx->tx_count;
  out->errors            = ctx->errors;
  out->running           = ctx->running;
  // receiving indicates if frames were seen recently (1s threshold)
  TickType_t now         = xTaskGetTickCount();
  out->receiving         = ctx->rx_active && ((now - ctx->last_rx_tick) <= pdMS_TO_TICKS(1000));
  out->last_rx_ms        = ctx->rx_active ? (ctx->last_rx_tick * portTICK_PERIOD_MS) : 0;
  return ESP_OK;
}
