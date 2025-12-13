#include <atomic>
#include <string>

#include <haptic_motor.hpp>
#include <joystick.h>
#include <nvs_rw.hpp>

namespace {
constexpr const char *kNvsKey = "joy_haptic";

std::atomic<bool> s_enabled{false};

bool parse_bool(const std::string &v, bool default_value) {
    if (v == "true" || v == "1" || v == "on" || v == "ON") return true;
    if (v == "false" || v == "0" || v == "off" || v == "OFF") return false;
    return default_value;
}

void on_joystick_edge(const Joystick::joystick_state_t &) {
    if (!s_enabled.load(std::memory_order_relaxed)) return;
    HapticMotor::instance().pulse(HapticMotor::kDefaultFrequencyHz, 12,
                                  HapticMotor::kDefaultDuty);
}
}  // namespace

void joystick_haptics_init() {
    const std::string v = get_nvs(kNvsKey);
    s_enabled.store(parse_bool(v, false), std::memory_order_relaxed);
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
