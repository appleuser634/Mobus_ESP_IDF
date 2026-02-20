#include "neopixel.hpp"

#include "esp_err.h"
#include "esp_log.h"

namespace {
constexpr int kLedPin = 45;
constexpr int kLedCount = 1;
constexpr const char* kTag = "Neopixel";
}  // namespace

Neopixel neopixel;

Neopixel::Neopixel() : handle_(nullptr) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = kLedPin,
        .max_leds = kLedCount,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
    };

    esp_err_t err =
        led_strip_new_rmt_device(&strip_config, &rmt_config, &handle_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to create led strip: %s", esp_err_to_name(err));
        handle_ = nullptr;
    }
}

Neopixel::~Neopixel() {
    if (handle_) {
        esp_err_t err = led_strip_del(handle_);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "Failed to delete led strip: %s", esp_err_to_name(err));
        }
    }
}

void Neopixel::set_color(uint8_t r, uint8_t g, uint8_t b) {
    if (!handle_) {
        ESP_LOGW(kTag, "LED strip not initialized");
        return;
    }

    esp_err_t err = led_strip_set_pixel(handle_, 0, r, g, b);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "led_strip_set_pixel failed: %s", esp_err_to_name(err));
        return;
    }

    err = led_strip_refresh(handle_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "led_strip_refresh failed: %s", esp_err_to_name(err));
    }
}
