#pragma once

#include <cstdint>

#include "led_strip.h"

class Neopixel {
   public:
    Neopixel();
    ~Neopixel();

    Neopixel(const Neopixel&) = delete;
    Neopixel& operator=(const Neopixel&) = delete;
    Neopixel(Neopixel&&) = delete;
    Neopixel& operator=(Neopixel&&) = delete;

    void set_color(uint8_t r, uint8_t g, uint8_t b);

   private:
    led_strip_handle_t handle_;
};

extern Neopixel neopixel;
