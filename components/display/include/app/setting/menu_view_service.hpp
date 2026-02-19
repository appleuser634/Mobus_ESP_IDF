#pragma once

#include <array>
#include <utility>
#include <vector>

#include <ble_uart.hpp>
#include <esp_wifi.h>

#include "app/setting/menu_label_service.hpp"
#include "ui/setting/menu_mvp.hpp"

namespace app::settingmenuview {

inline bool wifi_connected_for_label() {
    wifi_ap_record_t ap = {};
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

template <size_t N>
inline std::vector<ui::settingmenu::RowData> build_rows(
    const std::array<ui::Key, N>& setting_keys, ui::Lang lang) {
    std::vector<ui::settingmenu::RowData> rows;
    rows.reserve(setting_keys.size());

    for (ui::Key key : setting_keys) {
        std::string label;
        if (key == ui::Key::SettingsBluetooth) {
            const bool connected = !wifi_connected_for_label() && ble_uart_is_ready();
            const bool pairing_on = get_nvs((char*)"ble_pair") == "true";
            label = app::settingmenu::build_bluetooth_label(lang, connected,
                                                            pairing_on);
        } else {
            label = app::settingmenu::build_label(key, lang);
        }
        rows.push_back({key, std::move(label)});
    }

    return rows;
}

inline ui::Key selected_key_or_default(const ui::settingmenu::ViewState& state,
                                       ui::Key fallback) {
    if (state.rows.empty()) return fallback;
    const int idx = state.select_index;
    if (idx < 0 || idx >= static_cast<int>(state.rows.size())) return fallback;
    return state.rows[idx].key;
}

}  // namespace app::settingmenuview
