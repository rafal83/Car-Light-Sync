set(component_requires
    esp_wifi
    esp_http_server
    nvs_flash
    driver
    esp_netif
    json
    app_update
    esp_http_client
)

if(CONFIG_BT_ENABLED)
    list(APPEND component_requires bt)
endif()

idf_component_register(
    SRCS
        "main.c"
        "wifi_manager.c"
        "commander.c"
        "tesla_can.c"
        "vehicle_can_config.c"
        "led_effects.c"
        "led_strip_encoder.c"
        "web_server.c"
        "config_manager.c"
        "ota_update.c"
        "ble_api_service.c"
    INCLUDE_DIRS
        "."
        "../include"
    EMBED_FILES
        "../data/index.html.gz"
        "../data/icon.svg"
    REQUIRES
        ${component_requires}
)
