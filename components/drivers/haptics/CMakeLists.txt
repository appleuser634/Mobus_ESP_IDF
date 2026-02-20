idf_component_register(
    SRCS "src/joystick_haptics.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver input nvs esp_adc nvs_flash esp_timer
)
