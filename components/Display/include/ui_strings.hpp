// UI string table for OLED language selection (English/Japanese)
#pragma once

#include <string>
#include <nvs_rw.hpp>

namespace ui {

enum class Lang {
    En,
    Ja,
};

enum class Key {
    SettingsProfile,
    SettingsWifi,
    SettingsBluetooth,
    SettingsSound,
    SettingsVibration,
    SettingsBootSound,
    SettingsRtc,
    SettingsOpenChat,
    SettingsComposer,
    SettingsAutoUpdate,
    SettingsOtaManifest,
    SettingsUpdateNow,
    SettingsFirmwareInfo,
    SettingsDevelop,
    SettingsFactoryReset,
    SettingsLanguage,
    LabelOn,
    LabelOff,
    LabelConnected,
    LabelPairing,
    TitleLanguage,
    LangEnglish,
    LangJapanese,
};

inline Lang current_lang() {
    std::string v = get_nvs("ui_lang");
    if (v == "ja") return Lang::Ja;
    return Lang::En;
}

inline const char* text(Key key, Lang lang = current_lang()) {
    if (lang == Lang::Ja) {
        switch (key) {
            case Key::SettingsProfile: return "プロフィール";
            case Key::SettingsWifi: return "ワイファイ";
            case Key::SettingsBluetooth: return "ブルートゥース";
            case Key::SettingsSound: return "サウンド";
            case Key::SettingsVibration: return "バイブ";
            case Key::SettingsBootSound: return "ブートサウンド";
            case Key::SettingsRtc: return "リアルタイムチャット";
            case Key::SettingsOpenChat: return "オープンチャット";
            case Key::SettingsComposer: return "コンポーザ";
            case Key::SettingsAutoUpdate: return "オートアップデート";
            case Key::SettingsOtaManifest: return "OTA マニフェスト";
            case Key::SettingsUpdateNow: return "アップデート";
            case Key::SettingsFirmwareInfo: return "ファームインフォ";
            case Key::SettingsDevelop: return "デベロップ";
            case Key::SettingsFactoryReset: return "ファクトリリセット";
            case Key::SettingsLanguage: return "ゲンゴ";
            case Key::LabelOn: return "オン";
            case Key::LabelOff: return "オフ";
            case Key::LabelConnected: return "コネクト";
            case Key::LabelPairing: return "ペアリング";
            case Key::TitleLanguage: return "ゲンゴ センタク";
            case Key::LangEnglish: return "イングリッシュ";
            case Key::LangJapanese: return "ニホンゴ";
        }
    }

    switch (key) {
        case Key::SettingsProfile: return "Profile";
        case Key::SettingsWifi: return "Wi-Fi";
        case Key::SettingsBluetooth: return "Bluetooth";
        case Key::SettingsSound: return "Sound";
        case Key::SettingsVibration: return "Vibration";
        case Key::SettingsBootSound: return "Boot Sound";
        case Key::SettingsRtc: return "Real Time Chat";
        case Key::SettingsOpenChat: return "Open Chat";
        case Key::SettingsComposer: return "Composer";
        case Key::SettingsAutoUpdate: return "Auto Update";
        case Key::SettingsOtaManifest: return "OTA Manifest";
        case Key::SettingsUpdateNow: return "Update Now";
        case Key::SettingsFirmwareInfo: return "Firmware Info";
        case Key::SettingsDevelop: return "Develop";
        case Key::SettingsFactoryReset: return "Factory Reset";
        case Key::SettingsLanguage: return "Language";
        case Key::LabelOn: return "ON";
        case Key::LabelOff: return "OFF";
        case Key::LabelConnected: return "Connected";
        case Key::LabelPairing: return "PAIRING";
        case Key::TitleLanguage: return "Select Language";
        case Key::LangEnglish: return "English";
        case Key::LangJapanese: return "Japanese";
    }

    return "";
}

}  // namespace ui
