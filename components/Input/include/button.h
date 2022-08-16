#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#define GPIO_OUTPUT_IO_0    18
#define GPIO_OUTPUT_IO_1    19
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<GPIO_OUTPUT_IO_1))
#define GPIO_INPUT_IO_0     GPIO_NUM_4
#define GPIO_INPUT_IO_1     5
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_0) | (1ULL<<GPIO_INPUT_IO_1))
#define ESP_INTR_FLAG_DEFAULT 0

class Button { 
    public:
    
    typedef struct {
        bool pushing;   // pushing flag
        bool pushed;    // pushed flag
        char push_type; // long or short
        long long int push_start_sec; // push start second
        long long int pushing_sec;    // pushing second
        long long int release_sec;    // release second
    } button_state_t;
 
    button_state_t button_state = {false, false, 's', 0, 0, 0};

    long long int long_push_thresh = 150000;
     
    button_state_t get_button_state() {
        if (gpio_get_level(GPIO_INPUT_IO_0) && button_state.pushing == false) {
            button_state.pushing = true;
            button_state.push_start_sec = esp_timer_get_time();
        } else if (!gpio_get_level(GPIO_INPUT_IO_0) && button_state.pushing == true) {
            
            button_state.pushing_sec =  esp_timer_get_time() - button_state.push_start_sec;
            
            if (button_state.pushing_sec > long_push_thresh) {
                button_state.push_type = 'l';    
            } else {
                button_state.push_type = 's';
            }

            button_state.pushing = false;
            button_state.pushed = true;
        }

        if (button_state.pushing == false){
            button_state.release_sec += 1;
        }
        else {
            button_state.release_sec = 0;
        }
        return button_state;
    }

    void clear_button_state() {
        button_state.pushing = false; 
        button_state.pushed = false;
        button_state.push_type = 's';
        button_state.push_start_sec = 0;
        button_state.pushing_sec = 0;
    }

    void setup(){
        gpio_config_t io_conf;

        //interrupt of rising edge
        io_conf.intr_type = GPIO_INTR_ANYEDGE;
        //bit mask of the pins, use GPIO4/5 here
        io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
        //set as input mode
        io_conf.mode = GPIO_MODE_INPUT;
        //enable pull-up mode
        io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
        
        gpio_config(&io_conf);
    }
};
