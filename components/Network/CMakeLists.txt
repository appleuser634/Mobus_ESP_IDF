idf_component_register(
    SRCS "my_component.c" "mqtt_runtime.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp_netif nvs_flash mqtt esp_wifi
)
