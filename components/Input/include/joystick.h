#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "sdkconfig.h"

class Joystick {
    public:
    typedef struct {
        bool up;
        bool down;
        bool left;
        bool right;
        bool pushed_up;
        bool pushed_down;
        bool pushed_left;
        bool pushed_right;
        bool pushed_up_edge;
        bool pushed_down_edge;
        bool pushed_left_edge;
        bool pushed_right_edge;
        uint32_t C6_voltage;
        uint32_t C7_voltage;
        long long int release_start_sec;    // release second
        long long int release_sec;    // release second
    } joystick_state_t;
    
    esp_adc_cal_characteristics_t adcChar;
    joystick_state_t joystick_state = {false, false, false, false, false, false, false, false, false, false, false, false, 0, 0, 0, 0};
 
    void setup(){
        printf("Setup Joystick.... ");
        // ADC1_CH6を初期化
        adc_gpio_init(ADC_UNIT_1, ADC_CHANNEL_6);
        // ADC1の解像度を12bit（0~4095）に設定
        adc1_config_width(ADC_WIDTH_BIT_12);
        // ADC1の減衰を11dBに設定
        adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
        // 電圧値に変換するための情報をaddCharに格納
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adcChar);
        
        // ADC1_CH7を初期化
        adc_gpio_init(ADC_UNIT_1, ADC_CHANNEL_7);
        // ADC1の減衰を11dBに設定
        adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11);
        // 電圧値に変換するための情報をaddCharに格納
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adcChar);
    }
    
    int get_joystick_value(adc_channel_t channel) {
        uint32_t voltage;
        // ADC1_CH6の電圧値を取得
        esp_adc_cal_get_voltage(channel, &adcChar, &voltage);

        return voltage;
    }

    joystick_state_t get_joystick_state() {
        
        joystick_state.C6_voltage = get_joystick_value(ADC_CHANNEL_6);
        joystick_state.C7_voltage = get_joystick_value(ADC_CHANNEL_7);

        joystick_state.pushed_up = false;
        joystick_state.pushed_down = false;
        joystick_state.pushed_left = false;
        joystick_state.pushed_right = false;
        
		joystick_state.pushed_up_edge = false;
        joystick_state.pushed_down_edge = false;
        joystick_state.pushed_left_edge = false;
        joystick_state.pushed_right_edge = false;
        
        if (joystick_state.C6_voltage <= 700){
			if (not joystick_state.down) {
				joystick_state.pushed_down_edge = true;
			}
            joystick_state.down = true;
        } else if (joystick_state.C6_voltage >= 3000){
			if (not joystick_state.up) {
				joystick_state.pushed_up_edge = true;
			}
            joystick_state.up = true;
        } else {
            if (joystick_state.up){
                joystick_state.pushed_up = true;
            }
            if (joystick_state.down){
                joystick_state.pushed_down = true;
            }
            joystick_state.up = false;
            joystick_state.down = false;
        }
        
        if (joystick_state.C7_voltage >= 3000){
			if (not joystick_state.left) {
				joystick_state.pushed_left_edge = true;
			}
            joystick_state.left = true;
        } else if (joystick_state.C7_voltage <= 700){
			if (not joystick_state.right) {
				joystick_state.pushed_right_edge = true;
			}
            joystick_state.right = true;
        } else {
            if (joystick_state.left){
                joystick_state.pushed_left = true;
            }
            if (joystick_state.right){
                joystick_state.pushed_right = true;
            }
            joystick_state.left = false;
            joystick_state.right = false;
        }

		if (joystick_state.left or joystick_state.right or joystick_state.up or joystick_state.down) {	
            joystick_state.release_start_sec = esp_timer_get_time();
		} else {	
            joystick_state.release_sec = esp_timer_get_time() - joystick_state.release_start_sec;
		}

        return joystick_state;
    }

	void reset_timer() {
        joystick_state.release_start_sec = esp_timer_get_time();
	}
};
