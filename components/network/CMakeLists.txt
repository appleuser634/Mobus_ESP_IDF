idf_component_register(
    SRCS "component_stub.c" "mqtt_runtime.cpp" "wifi_state.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp_netif nvs_flash mqtt esp_wifi
)
