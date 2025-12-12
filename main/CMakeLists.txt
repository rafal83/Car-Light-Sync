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

set(component_priv_includes "")

if(CONFIG_BT_ENABLED)
    list(APPEND component_requires bt)
    if(CONFIG_BT_NIMBLE_ENABLED)
        set(nimble_include_candidates
            "$ENV{IDF_PATH}/components/bt/host/nimble/nimble/host/include"
            "$ENV{IDF_PATH}/components/bt/host/nimble/nimble/porting/nimble/include"
            "$ENV{IDF_PATH}/components/bt/esp_nimble/nimble/host/include"
            "$ENV{IDF_PATH}/components/bt/esp_nimble/nimble/porting/nimble/include"
        )
        foreach(dir ${nimble_include_candidates})
            if(EXISTS ${dir})
                list(APPEND component_priv_includes ${dir})
            endif()
        endforeach()
    endif()
endif()

idf_component_register(
    SRCS
        "main.c"
        "config.c"
        "wifi_manager.c"
        "captive_portal.c"
        "can_bus.c"
        "gvret_tcp_server.c"
        "panda_tcp_server.c"
        "log_stream.c"
        "vehicle_can_unified.c"
        "vehicle_can_unified_config.generated.c"
        "vehicle_can_mapping.c"
        "led_effects.c"
        "led_strip_encoder.c"
        "web_server.c"
        "config_manager.c"
        "ota_update.c"
        "ble_api_service.c"
        "audio_input.c"
        "status_led.c"
        "reset_button.c"
    INCLUDE_DIRS
        "."
        "../include"
    PRIV_INCLUDE_DIRS
        ${component_priv_includes}
    EMBED_FILES
        "../data/index.html.gz"
        "../data/i18n.js.gz"
        "../data/script.js.gz"
        "../data/style.css.gz"
        "../data/carlightsync64.png"
        "../data/default.json"
    REQUIRES
        ${component_requires}
)
