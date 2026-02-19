#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "hal/adc_types.h"

#define EXAMPLE_ADC_ATTEN ADC_ATTEN_DB_11

class PowerMonitor {
   public:
    typedef struct {
        int power_voltage;
    } power_state_t;

    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1;
    adc_cali_handle_t adc1_cali_chan0_handle = NULL;

    PowerMonitor() {
        init_config1 = {
            .unit_id = ADC_UNIT_2,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };

        adc_oneshot_new_unit(&init_config1, &adc1_handle);
        //-------------ADC1 Config---------------//
        adc_oneshot_chan_cfg_t config = {
            .atten = EXAMPLE_ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_3, &config);
    }

    // esp_adc_cal_characteristics_t adcChar;
    power_state_t power_state = {123};

    power_state_t get_power_state() {
        int row;
        // int voltage;

        // ADC1_CH0の電圧値を取得
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_3, &row);

        // adc_cali_raw_to_voltage(adc1_cali_chan0_handle, row, &voltage);
        power_state.power_voltage = row;

        return power_state;
    }
};
