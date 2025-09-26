idf_component_register(
    SRCS "new_component.c" "src/notification_effects.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver led_strip Haptics Neopixel
)
