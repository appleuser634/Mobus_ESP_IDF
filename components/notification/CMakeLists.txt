idf_component_register(
    SRCS "component_stub.c" "src/notification_effects.cpp" "src/notification_bridge.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver led_strip haptics neopixel network
)
