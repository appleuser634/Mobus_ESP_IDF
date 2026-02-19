#pragma once

#include <string>

#include <joystick_haptics.hpp>
#include <nvs_rw.hpp>

namespace app::settingaction {

struct DevelopToggleResult {
    bool on = false;
    std::string endpoint;
};

inline bool toggle_auto_update() {
    bool on = (get_nvs((char*)"ota_auto") == "true");
    on = !on;
    save_nvs((char*)"ota_auto", on ? std::string("true") : std::string("false"));
    return on;
}

inline bool toggle_vibration() {
    bool on = joystick_haptics_enabled();
    on = !on;
    joystick_haptics_set_enabled(on, true);
    return on;
}

inline DevelopToggleResult toggle_develop_mode() {
    DevelopToggleResult result;
    bool on = (get_nvs((char*)"develop_mode") == "true");
    on = !on;

    save_nvs((char*)"develop_mode", on ? std::string("true") : std::string("false"));

    if (on) {
        save_nvs((char*)"server_scheme", std::string("http"));
        save_nvs((char*)"server_host", std::string("192.168.2.184"));
        save_nvs((char*)"server_port", std::string("8080"));
        save_nvs((char*)"mqtt_host", std::string("192.168.2.184"));
        save_nvs((char*)"mqtt_port", std::string("1883"));
        result.endpoint = "http://192.168.2.184";
    } else {
        save_nvs((char*)"server_scheme", std::string("https"));
        save_nvs((char*)"server_host", std::string("mimoc.jp"));
        save_nvs((char*)"server_port", std::string("443"));
        save_nvs((char*)"mqtt_host", std::string("mimoc.jp"));
        save_nvs((char*)"mqtt_port", std::string("1883"));
        result.endpoint = "https://mimoc.jp";
    }

    result.on = on;
    return result;
}

}  // namespace app::settingaction
