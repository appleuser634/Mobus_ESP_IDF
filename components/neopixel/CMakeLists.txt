idf_component_register(
    SRCS "src/neopixel.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver led_strip
)
