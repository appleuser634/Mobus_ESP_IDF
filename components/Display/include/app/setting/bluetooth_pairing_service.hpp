#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

#include <ble_uart.hpp>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <nvs_rw.hpp>

extern EventGroupHandle_t s_wifi_event_group;

namespace app::blepair {

struct PairingState {
    bool pairing = false;
    long long exp_us = 0;
    std::string code;
};

inline std::string nvs_str(const char* key) {
    return get_nvs((char*)key);
}

inline void nvs_put(const char* key, const std::string& value) {
    save_nvs((char*)key, value);
}

inline bool get_wifi_restore_flag() {
    return nvs_str("ble_wifi_rst") == std::string("1");
}

inline void set_wifi_restore_flag(bool enable) {
    nvs_put("ble_wifi_rst", enable ? "1" : "0");
}

inline std::string generate_code() {
    uint32_t r = static_cast<uint32_t>(esp_timer_get_time());
    r = 1103515245u * r + 12345u;
    uint32_t n = (r % 900000u) + 100000u;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06u", static_cast<unsigned>(n));
    return std::string(buf);
}

inline PairingState load_state() {
    PairingState state;
    state.pairing = (nvs_str("ble_pair") == std::string("true"));
    std::string s = nvs_str("ble_exp_us");
    if (!s.empty()) state.exp_us = std::atoll(s.c_str());
    state.code = nvs_str("ble_code");
    return state;
}

inline void persist_state(const PairingState& state) {
    nvs_put("ble_pair", state.pairing ? "true" : "false");
    nvs_put("ble_exp_us", std::to_string(state.exp_us));
    if (!state.code.empty()) nvs_put("ble_code", state.code);
}

inline void normalize_state(PairingState& state, long long now_us) {
    if (state.pairing && state.exp_us <= now_us) {
        state.pairing = false;
        persist_state(state);
    }
    if (state.pairing && state.code.empty()) {
        state.code = generate_code();
        nvs_put("ble_code", state.code);
    }
}

inline long long remaining_seconds(const PairingState& state, long long now_us) {
    if (!state.pairing) return 0;
    return (state.exp_us - now_us) / 1000000LL;
}

inline void restore_wifi_if_needed() {
    if (!get_wifi_restore_flag()) return;

    ESP_LOGI("BLE_PAIR", "Restoring Wi-Fi after pairing");
    set_wifi_restore_flag(false);

    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN &&
        err != ESP_ERR_WIFI_NOT_STOPPED) {
        ESP_LOGW("BLE_PAIR", "esp_wifi_start returned %s", esp_err_to_name(err));
    }

    err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW("BLE_PAIR", "esp_wifi_connect returned %s", esp_err_to_name(err));
    }
}

inline void stop_pairing(PairingState& state) {
    state.pairing = false;
    persist_state(state);
    ble_uart_disable();
}

inline void start_pairing(PairingState& state, long long now_us,
                          int window_sec = 120) {
    state.pairing = true;
    state.code = generate_code();
    state.exp_us = now_us + static_cast<long long>(window_sec) * 1000000LL;
    persist_state(state);
}

inline bool disable_wifi_for_pairing(bool wifi_connected) {
    set_wifi_restore_flag(wifi_connected);
    if (!wifi_connected) {
        return false;
    }

    ESP_LOGI("BLE_PAIR", "Disabling Wi-Fi for pairing");

    esp_err_t derr = esp_wifi_disconnect();
    if (derr != ESP_OK && derr != ESP_ERR_WIFI_NOT_STARTED &&
        derr != ESP_ERR_WIFI_CONN) {
        ESP_LOGW("BLE_PAIR", "esp_wifi_disconnect returned %s",
                 esp_err_to_name(derr));
    }

    esp_err_t serr = esp_wifi_stop();
    if (serr != ESP_OK && serr != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW("BLE_PAIR", "esp_wifi_stop returned %s", esp_err_to_name(serr));
    }

    if (s_wifi_event_group) {
        xEventGroupClearBits(s_wifi_event_group, BIT0);
    }

    auto still_connected = []() -> bool {
        wifi_ap_record_t ap = {};
        return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    };

    constexpr int kMaxWaitIters = 40;
    int wait = 0;
    while (still_connected() && wait < kMaxWaitIters) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
        ++wait;
    }

    if (still_connected()) {
        ESP_LOGW("BLE_PAIR",
                 "Wi-Fi still marked connected after stop; continuing");
    }
    return true;
}

}  // namespace app::blepair
