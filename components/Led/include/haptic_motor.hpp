#pragma once

#include <cstdint>

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class HapticMotor {
   public:
    static HapticMotor &instance();

    static constexpr uint32_t kDefaultFrequencyHz = 150;
    static constexpr uint32_t kDefaultDuty = 128;

    void pulse(uint32_t freq_hz, int duration_ms, uint32_t duty = kDefaultDuty);
    void activate(uint32_t duty = kDefaultDuty);
    void deactivate();
    void play_boot_pattern();
    void play_notification_pattern();

   private:
    HapticMotor() = default;

    void ensure_configured(uint32_t freq_hz);
    static constexpr uint32_t clamp_duty(uint32_t duty) {
        return duty > kMaxDuty ? kMaxDuty : duty;
    }

    static constexpr gpio_num_t kPin = static_cast<gpio_num_t>(9);
    static constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;
    static constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
    static constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
    static constexpr uint32_t kMaxDuty = 255;

    bool configured_ = false;
    uint32_t current_freq_hz_ = kDefaultFrequencyHz;
};

inline HapticMotor &HapticMotor::instance() {
    static HapticMotor inst;
    return inst;
}

inline void HapticMotor::ensure_configured(uint32_t freq_hz) {
    if (configured_ && current_freq_hz_ == freq_hz) {
        return;
    }

    ledc_timer_config_t timer_config = {
        .speed_mode = kMode,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = kTimer,
        .freq_hz = freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_timer_config(&timer_config));

    ledc_channel_config_t channel_config = {
        .gpio_num = kPin,
        .speed_mode = kMode,
        .channel = kChannel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = kTimer,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_channel_config(&channel_config));

    current_freq_hz_ = freq_hz;
    configured_ = true;
}

inline void HapticMotor::activate(uint32_t duty) {
    ensure_configured(kDefaultFrequencyHz);
    ledc_set_duty(kMode, kChannel, clamp_duty(duty));
    ledc_update_duty(kMode, kChannel);
}

inline void HapticMotor::deactivate() {
    if (!configured_) {
        return;
    }
    ledc_set_duty(kMode, kChannel, 0);
    ledc_update_duty(kMode, kChannel);
}

inline void HapticMotor::pulse(uint32_t freq_hz, int duration_ms, uint32_t duty) {
    if (duration_ms <= 0) {
        return;
    }
    ensure_configured(freq_hz);
    ledc_set_duty(kMode, kChannel, clamp_duty(duty));
    ledc_update_duty(kMode, kChannel);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    deactivate();
}

inline void HapticMotor::play_boot_pattern() {
    pulse(kDefaultFrequencyHz, 100, kDefaultDuty);
    vTaskDelay(pdMS_TO_TICKS(50));
    pulse(kDefaultFrequencyHz, 100, kDefaultDuty);
}

inline void HapticMotor::play_notification_pattern() {
    pulse(kDefaultFrequencyHz, 100, clamp_duty(200));
    vTaskDelay(pdMS_TO_TICKS(50));
    pulse(kDefaultFrequencyHz, 100, clamp_duty(220));
    vTaskDelay(pdMS_TO_TICKS(50));
    pulse(kDefaultFrequencyHz, 100, clamp_duty(240));
}
