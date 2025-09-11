idf_component_register(
    SRCS "my_component.c" "ota_client.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp_https_ota nvs_flash esp_netif esp_wifi app_update esp_app_format json mbedtls bootloader_support
)
