# Set conditional requirements based on Bluetooth config
idf_component_register(
    SRCS "ble_uart.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash Nvs
)
