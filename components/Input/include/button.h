#pragma once

#include <array>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

class Button {
   public:
    gpio_num_t gpio_num;

    Button(gpio_num_t gpio_n = GPIO_NUM_4) {
        gpio_num = gpio_n;
        (void)configure_gpio_once(gpio_n);
    }

    typedef struct {
        bool push_edge;  // pushing flag
        bool pushing;    // pushing flag
        bool pushed;     // pushed flag
        bool pushed_same_time;
        char push_type;                   // long or short
        long long int push_start_sec;     // push start second
        long long int pushing_sec;        // pushing second
        long long int release_start_sec;  // release second
        long long int release_sec;        // release second
    } button_state_t;

    button_state_t button_state = {false, false, false, false, 's', 0, 0, 0};
    long long int long_push_thresh = 130000;

    button_state_t get_button_state() {
        button_state.push_edge = false;
        if (gpio_get_level(gpio_num) && button_state.pushing == false) {
            button_state.pushing = true;
            button_state.push_edge = true;
            button_state.push_start_sec = esp_timer_get_time();
        } else if (!gpio_get_level(gpio_num) && button_state.pushing == true) {
            button_state.pushing_sec =
                esp_timer_get_time() - button_state.push_start_sec;

            if (button_state.pushing_sec > long_push_thresh) {
                button_state.push_type = 'l';
            } else {
                button_state.push_type = 's';
            }

            button_state.pushing = false;
            button_state.pushed = true;
        }

        if (button_state.pushing == false) {
            button_state.release_sec =
                esp_timer_get_time() - button_state.release_start_sec;
        } else {
            button_state.release_start_sec = esp_timer_get_time();
        }
        return button_state;
    }

    void pushed_same_time() { button_state.pushed_same_time = true; }

    void clear_button_state() {
        button_state.push_edge = false;
        button_state.pushing = false;
        button_state.pushed = false;
        button_state.pushed_same_time = false;
        button_state.push_type = 's';
        button_state.push_start_sec = 0;
        button_state.pushing_sec = 0;
    }

    void reset_timer() {
        button_state.release_start_sec = esp_timer_get_time();
    }

   private:
    static bool configure_gpio_once(gpio_num_t gpio_n) {
        static std::mutex s_cfg_mutex;
        static std::array<bool, GPIO_NUM_MAX> s_configured = {};

        const int idx = static_cast<int>(gpio_n);
        if (idx < 0 || idx >= GPIO_NUM_MAX) {
            ESP_LOGE("Button", "Invalid gpio number: %d", idx);
            return false;
        }

        std::lock_guard<std::mutex> lock(s_cfg_mutex);
        if (s_configured[idx]) {
            return true;
        }

        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_ANYEDGE;
        io_conf.pin_bit_mask = (1ULL << gpio_n);
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;

        const esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGE("Button", "gpio_config(%d) failed: %s", idx,
                     esp_err_to_name(err));
            return false;
        }

        s_configured[idx] = true;
        return true;
    }
};
