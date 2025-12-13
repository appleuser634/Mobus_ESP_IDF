#include <atomic>
#include <string>

#include <haptic_motor.hpp>
#include <joystick.h>
#include <nvs_rw.hpp>
#include "esp_timer.h"

namespace {
constexpr const char *kNvsKey = "joy_haptic";
constexpr int kPulseMs = 30;

std::atomic<bool> s_enabled{false};
esp_timer_handle_t s_off_timer = nullptr;

bool parse_bool(const std::string &v, bool default_value) {
    if (v == "true" || v == "1" || v == "on" || v == "ON") return true;
    if (v == "false" || v == "0" || v == "off" || v == "OFF") return false;
    return default_value;
}

void haptic_off_timer_cb(void *) { HapticMotor::instance().deactivate(); }

void on_joystick_edge(const Joystick::joystick_state_t &) {
    if (!s_enabled.load(std::memory_order_relaxed)) return;
    auto &motor = HapticMotor::instance();
    motor.activate();
    if (s_off_timer) {
        (void)esp_timer_stop(s_off_timer);
        (void)esp_timer_start_once(s_off_timer, (uint64_t)kPulseMs * 1000ULL);
    }
}
}  // namespace

void joystick_haptics_init() {
    const std::string v = get_nvs(kNvsKey);
    s_enabled.store(parse_bool(v, false), std::memory_order_relaxed);
    if (!s_off_timer) {
        const esp_timer_create_args_t args = {
            .callback = &haptic_off_timer_cb,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "joy_hap_off",
            .skip_unhandled_events = true,
        };
        (void)esp_timer_create(&args, &s_off_timer);
    }
    Joystick::set_edge_callback(&on_joystick_edge);
}

bool joystick_haptics_enabled() {
    return s_enabled.load(std::memory_order_relaxed);
}

void joystick_haptics_set_enabled(bool enabled, bool persist) {
    s_enabled.store(enabled, std::memory_order_relaxed);
    if (persist) {
        save_nvs(kNvsKey, enabled ? std::string("true") : std::string("false"));
    }
}
