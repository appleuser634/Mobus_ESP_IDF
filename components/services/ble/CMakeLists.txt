# Set conditional requirements based on Bluetooth config
idf_component_register(
    SRCS "ble_uart.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash nvs esp_wifi esp_event esp_netif network notification
)
