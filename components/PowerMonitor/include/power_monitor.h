#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "sdkconfig.h"

class PowerMonitor {
    public:
    typedef struct {
        uint32_t power_voltage;
    } power_state_t;
    
    esp_adc_cal_characteristics_t adcChar;
    power_state_t power_state = {123};
 
    void setup(){
        printf("Setup PowerMonitor.... ");
        // ADC1_CH6を初期化
        adc_gpio_init(ADC_UNIT_1, ADC_CHANNEL_3);
        // ADC1の解像度を12bit（0~4095）に設定
        adc1_config_width(ADC_WIDTH_BIT_12);
        // ADC1の減衰を11dBに設定
        adc1_config_channel_atten(ADC1_CHANNEL_3, ADC_ATTEN_DB_11);
        // 電圧値に変換するための情報をaddCharに格納
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adcChar);
    }
    
    power_state_t get_power_state() {
        
        uint32_t voltage;
        // ADC1_CH0の電圧値を取得
        esp_adc_cal_get_voltage(ADC_CHANNEL_3, &adcChar, &voltage);
        
        power_state.power_voltage = voltage;

        return power_state;
    }
};
