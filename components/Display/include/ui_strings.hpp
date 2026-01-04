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
    TitleLanguageConfirm,
    TitleWifiSetup,
    TitleAccount,
    TitleChoose,
    LabelConfirm,
    LabelBack,
    LabelRetry,
    ActionLogin,
    ActionSignup,
    LangEnglish,
    LangJapanese,
    GreetingLine1,
    GreetingLine2,
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
            case Key::TitleLanguageConfirm: return "コレデ イイ?";
            case Key::TitleWifiSetup: return "ワイファイ セッテイ";
            case Key::TitleAccount: return "アカウント";
            case Key::TitleChoose: return "エラブ";
            case Key::LabelConfirm: return "ケッテイ";
            case Key::LabelBack: return "モドル";
            case Key::LabelRetry: return "モウイチド";
            case Key::ActionLogin: return "ログイン";
            case Key::ActionSignup: return "サインアップ";
            case Key::LangEnglish: return "イングリッシュ";
            case Key::LangJapanese: return "ニホンゴ";
            case Key::GreetingLine1: return "ヨウコソ";
            case Key::GreetingLine2: return "ミモック";
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
        case Key::TitleLanguageConfirm: return "Confirm Language";
        case Key::TitleWifiSetup: return "Wi-Fi Setup";
        case Key::TitleAccount: return "Account";
        case Key::TitleChoose: return "Choose";
        case Key::LabelConfirm: return "OK";
        case Key::LabelBack: return "Back";
        case Key::LabelRetry: return "Retry";
        case Key::ActionLogin: return "Login";
        case Key::ActionSignup: return "Sign Up";
        case Key::LangEnglish: return "English";
        case Key::LangJapanese: return "Japanese";
        case Key::GreetingLine1: return "Welcome";
        case Key::GreetingLine2: return "to Mimoc";
    }

    return "";
}

}  // namespace ui
