idf_component_register(
    SRCS "new_component.c" "src/notification_effects.cpp" "src/notification_bridge.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver led_strip Haptics Neopixel Network
)
