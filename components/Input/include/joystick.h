#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#define EXAMPLE_ADC_ATTEN ADC_ATTEN_DB_11
#define EXAMPLE_ADC1_CHAN0 ADC_CHANNEL_6
#define EXAMPLE_ADC1_CHAN1 ADC_CHANNEL_7

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
    uint32_t x_voltage;
    uint32_t y_voltage;
    long long int release_start_sec; // release second
    long long int release_sec;       // release second
  } joystick_state_t;

  /*---------------------------------------------------------------
          ADC Calibration
  ---------------------------------------------------------------*/
  static bool example_adc_calibration_init(adc_unit_t unit,
                                           adc_channel_t channel,
                                           adc_atten_t atten,
                                           adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
      ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
      adc_cali_curve_fitting_config_t cali_config = {
          .unit_id = unit,
          .chan = channel,
          .atten = atten,
          .bitwidth = ADC_BITWIDTH_DEFAULT,
      };
      ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
      if (ret == ESP_OK) {
        calibrated = true;
      }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
      ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
      adc_cali_line_fitting_config_t cali_config = {
          .unit_id = unit,
          .atten = atten,
          .bitwidth = ADC_BITWIDTH_DEFAULT,
      };
      ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
      if (ret == ESP_OK) {
        calibrated = true;
      }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
      ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
      ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
  }

  // esp_adc_cal_characteristics_t adcChar;
  joystick_state_t joystick_state = {false, false, false, false, false, false,
                                     false, false, false, false, false, false,
                                     0,     0,     0,     0};

  adc_oneshot_unit_handle_t adc1_handle;
  adc_oneshot_unit_init_cfg_t init_config1;
  bool do_calibration1_chan0;
  bool do_calibration1_chan1;
  adc_cali_handle_t adc1_cali_chan0_handle = NULL;
  adc_cali_handle_t adc1_cali_chan1_handle = NULL;

  Joystick() {

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
    adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN0, &config);
    adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN1, &config);

    //-------------ADC1 Calibration Init---------------//
    do_calibration1_chan0 = example_adc_calibration_init(
        ADC_UNIT_1, EXAMPLE_ADC1_CHAN0, EXAMPLE_ADC_ATTEN,
        &adc1_cali_chan0_handle);
    do_calibration1_chan1 = example_adc_calibration_init(
        ADC_UNIT_1, EXAMPLE_ADC1_CHAN1, EXAMPLE_ADC_ATTEN,
        &adc1_cali_chan1_handle);
  }

  int get_joystick_value(adc_channel_t channel) {
    int adc_raw;
    int voltage;

    adc_oneshot_read(adc1_handle, channel, &adc_raw);
    ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, channel,
             adc_raw);
    if (do_calibration1_chan0) {
      adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw, &voltage);
      ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1,
               channel, voltage);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    return voltage;
  }

  joystick_state_t get_joystick_state() {

    joystick_state.x_voltage = get_joystick_value(ADC_CHANNEL_7);
    joystick_state.y_voltage = get_joystick_value(ADC_CHANNEL_6);

    joystick_state.pushed_up = false;
    joystick_state.pushed_down = false;
    joystick_state.pushed_left = false;
    joystick_state.pushed_right = false;

    joystick_state.pushed_up_edge = false;
    joystick_state.pushed_down_edge = false;
    joystick_state.pushed_left_edge = false;
    joystick_state.pushed_right_edge = false;

    if (joystick_state.x_voltage <= 700) {
      if (not joystick_state.down) {
        joystick_state.pushed_down_edge = true;
      }
      joystick_state.down = true;
    } else if (joystick_state.x_voltage >= 3000) {
      if (not joystick_state.up) {
        joystick_state.pushed_up_edge = true;
      }
      joystick_state.up = true;
    } else {
      if (joystick_state.up) {
        joystick_state.pushed_up = true;
      }
      if (joystick_state.down) {
        joystick_state.pushed_down = true;
      }
      joystick_state.up = false;
      joystick_state.down = false;
    }

    if (joystick_state.y_voltage >= 3000) {
      if (not joystick_state.left) {
        joystick_state.pushed_left_edge = true;
      }
      joystick_state.left = true;
    } else if (joystick_state.y_voltage <= 700) {
      if (not joystick_state.right) {
        joystick_state.pushed_right_edge = true;
      }
      joystick_state.right = true;
    } else {
      if (joystick_state.left) {
        joystick_state.pushed_left = true;
      }
      if (joystick_state.right) {
        joystick_state.pushed_right = true;
      }
      joystick_state.left = false;
      joystick_state.right = false;
    }

    if (joystick_state.left or joystick_state.right or joystick_state.up or
        joystick_state.down) {
      joystick_state.release_start_sec = esp_timer_get_time();
    } else {
      joystick_state.release_sec =
          esp_timer_get_time() - joystick_state.release_start_sec;
    }

    return joystick_state;
  }

  void reset_timer() {
    joystick_state.release_start_sec = esp_timer_get_time();
  }
};
