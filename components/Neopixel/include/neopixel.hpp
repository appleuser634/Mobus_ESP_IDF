#include "led_strip.h"

#define LED_PIN 45  // WS2812の制御ピン
#define LED_NUM 1   // LEDの数

static led_strip_handle_t led_strip;

class Neopixel {
   public:
    Neopixel() {
        led_strip_config_t strip_config = {
            .strip_gpio_num = LED_PIN,
            .max_leds = LED_NUM,
            .led_model = LED_MODEL_WS2812,
            .color_component_format =
                LED_STRIP_COLOR_COMPONENT_FMT_GRB,  // The color component
                                                    // format is G-R-B
            .flags = {
                .invert_out = false,  // don't invert the output signal
            }};

        led_strip_rmt_config_t rmt_config = {
            .resolution_hz = 10 * 1000 * 1000,  // 10MHz
        };

        ESP_ERROR_CHECK(
            led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    }

    void set_color(uint8_t r, uint8_t g, uint8_t b) {
        led_strip_set_pixel(led_strip, 0, r, g, b);
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    }
};
