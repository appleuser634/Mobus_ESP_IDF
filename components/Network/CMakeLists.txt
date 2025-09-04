idf_component_register(
    SRCS "my_component.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp_netif nvs_flash mqtt esp_wifi
)
