#pragma once

#include "ui_strings.hpp"

namespace app::settingmenuaction {

enum class Action {
    None,
    Wifi,
    Language,
    Sound,
    Vibration,
    BootSound,
    Bluetooth,
    OtaManifest,
    UpdateNow,
    AutoUpdate,
    FirmwareInfo,
    Develop,
    Profile,
    Rtc,
    OpenChat,
    Composer,
    FactoryReset,
};

inline Action resolve(ui::Key key, bool triggered) {
    if (!triggered) return Action::None;

    switch (key) {
        case ui::Key::SettingsWifi:
            return Action::Wifi;
        case ui::Key::SettingsLanguage:
            return Action::Language;
        case ui::Key::SettingsSound:
            return Action::Sound;
        case ui::Key::SettingsVibration:
            return Action::Vibration;
        case ui::Key::SettingsBootSound:
            return Action::BootSound;
        case ui::Key::SettingsBluetooth:
            return Action::Bluetooth;
        case ui::Key::SettingsOtaManifest:
            return Action::OtaManifest;
        case ui::Key::SettingsUpdateNow:
            return Action::UpdateNow;
        case ui::Key::SettingsAutoUpdate:
            return Action::AutoUpdate;
        case ui::Key::SettingsFirmwareInfo:
            return Action::FirmwareInfo;
        case ui::Key::SettingsDevelop:
            return Action::Develop;
        case ui::Key::SettingsProfile:
            return Action::Profile;
        case ui::Key::SettingsRtc:
            return Action::Rtc;
        case ui::Key::SettingsOpenChat:
            return Action::OpenChat;
        case ui::Key::SettingsComposer:
            return Action::Composer;
        case ui::Key::SettingsFactoryReset:
            return Action::FactoryReset;
        default:
            return Action::None;
    }
}

}  // namespace app::settingmenuaction
