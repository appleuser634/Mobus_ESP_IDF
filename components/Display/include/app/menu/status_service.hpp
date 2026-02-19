#pragma once

#include <cstring>

#include <ArduinoJson.h>

namespace app::menu {

inline int rssi_to_bars(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

inline int clamp_power_voltage(int power_voltage) {
    if (power_voltage > 140) return 140;
    if (power_voltage < 0) return 0;
    return power_voltage;
}

inline int power_voltage_to_pixel(int power_voltage) {
    const float power_per = static_cast<float>(power_voltage) / 1.4f;
    return static_cast<int>(0.12f * power_per);
}

inline bool has_notification(const JsonDocument& notif_res) {
    JsonArrayConst notifications = notif_res["notifications"].as<JsonArrayConst>();
    if (notifications.isNull()) return false;
    for (JsonVariantConst v : notifications) {
        const char* flag = v["notification_flag"];
        if (flag != nullptr && strcmp(flag, "true") == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace app::menu
