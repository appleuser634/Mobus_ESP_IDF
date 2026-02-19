#pragma once

#include <cstdio>
#include <string>

#include <joystick_haptics.hpp>
#include <nvs_rw.hpp>
#include <sound_settings.hpp>
#include "ui_strings.hpp"

namespace app::settingmenu {

inline std::string build_label(ui::Key key, ui::Lang lang) {
    if (key == ui::Key::SettingsDevelop) {
        std::string dev = get_nvs((char*)"develop_mode");
        bool on = (dev == "true");
        return std::string(ui::text(ui::Key::SettingsDevelop, lang)) + " [" +
               ui::text(on ? ui::Key::LabelOn : ui::Key::LabelOff, lang) + "]";
    }

    if (key == ui::Key::SettingsSound) {
        bool on = sound_settings::enabled();
        int vol_pct = static_cast<int>(sound_settings::volume() * 100.0f + 0.5f);
        char label[64];
        std::snprintf(label, sizeof(label), "%s [%s, %d%%]",
                      ui::text(ui::Key::SettingsSound, lang),
                      ui::text(on ? ui::Key::LabelOn : ui::Key::LabelOff, lang),
                      vol_pct);
        return std::string(label);
    }

    if (key == ui::Key::SettingsAutoUpdate) {
        std::string au = get_nvs((char*)"ota_auto");
        bool on = (au == "true");
        return std::string(ui::text(ui::Key::SettingsAutoUpdate, lang)) + " [" +
               ui::text(on ? ui::Key::LabelOn : ui::Key::LabelOff, lang) + "]";
    }

    if (key == ui::Key::SettingsVibration) {
        bool on = joystick_haptics_enabled();
        return std::string(ui::text(ui::Key::SettingsVibration, lang)) + " [" +
               ui::text(on ? ui::Key::LabelOn : ui::Key::LabelOff, lang) + "]";
    }

    if (key == ui::Key::SettingsBootSound) {
        std::string bs = get_nvs((char*)"boot_sound");
        if (bs.empty()) bs = "cute";
        std::string shown = bs == std::string("majestic")
                                ? "Majestic"
                                : (bs == std::string("random") ? "Random" : "Cute");
        return std::string(ui::text(ui::Key::SettingsBootSound, lang)) + " [" +
               shown + "]";
    }

    return std::string(ui::text(key, lang));
}

inline std::string build_bluetooth_label(ui::Lang lang, bool connected,
                                         bool pairing_on) {
    std::string label = ui::text(ui::Key::SettingsBluetooth, lang);
    if (connected) {
        label += " [" + std::string(ui::text(ui::Key::LabelConnected, lang)) +
                 "]";
    } else if (pairing_on) {
        label += " [" + std::string(ui::text(ui::Key::LabelPairing, lang)) + "]";
    }
    return label;
}

}  // namespace app::settingmenu
