// Prevent multiple inclusion across translation units
#pragma once

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Non-Volatile Storage (NVS) Read and Write a Value - Example

   For other examples please check:
   https://github.com/espressif/esp-idf/tree/master/examples

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <vector>
#include <utility>

// Demo utilities removed to avoid unused warnings

inline void save_nvs(const char *key, const std::string &record) {
    // Initialize NVS once; avoid erasing flash while other subsystems use it
    static bool s_nvs_ok = false;
    if (!s_nvs_ok) {
        esp_err_t init = nvs_flash_init();
        if (init == ESP_OK) {
            s_nvs_ok = true;
        } else {
            ESP_LOGW("NVS", "nvs_flash_init failed: %s", esp_err_to_name(init));
            return;
        }
    }

    // Open NVS handle
    // reduce logs
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error (%s) opening NVS handle!", esp_err_to_name(err));
    }

    // Write string
    err = nvs_set_str(my_handle, key, record.c_str());
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to write string: %s", esp_err_to_name(err));
        nvs_close(my_handle);
        return;
    }

    // Commit written value
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to commit changes: %s", esp_err_to_name(err));
        nvs_close(my_handle);
        return;
    }
    // Close
    nvs_close(my_handle);
    // reduce logs

    // reduce logs
}

inline std::string get_nvs(const char *key) {
    // Initialize NVS once; avoid erasing flash while other subsystems use it
    static bool s_nvs_ok = false;
    if (!s_nvs_ok) {
        esp_err_t init = nvs_flash_init();
        if (init == ESP_OK) {
            s_nvs_ok = true;
        } else {
            ESP_LOGW("NVS", "nvs_flash_init failed: %s", esp_err_to_name(init));
            return std::string("");
        }
    }

    // Open NVS handle
    // reduce logs
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return "";
    }

    // Read string back
    size_t required_size = 0;
    err = nvs_get_str(my_handle, key, nullptr, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Key not found is not an error; return empty string
        nvs_close(my_handle);
        return std::string("");
    } else if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to get string size: %s", esp_err_to_name(err));
        nvs_close(my_handle);
        return "";
    }

    std::string result;
    result.resize(required_size);  // includes null terminator

    err = nvs_get_str(my_handle, key, result.data(), &required_size);
    nvs_close(my_handle);

    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to read string: %s", esp_err_to_name(err));
        return "";
    }

    // Remove trailing null character
    if (!result.empty() && result.back() == '\0') {
        result.pop_back();
    }

    // reduce logs
    return result;
}

// ---- Wi-Fi credentials helpers (max 5 entries) ----
// Keys layout:
//   wifi_ssid_0 .. wifi_ssid_4
//   wifi_pass_0 .. wifi_pass_4
//   wifi_next (u8): next index to overwrite when adding new entry

inline static esp_err_t nvs_open_storage(nvs_handle_t* out)
{
    // Ensure NVS ready
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    return nvs_open("storage", NVS_READWRITE, out);
}

inline void save_wifi_credential(const std::string& ssid, const std::string& pass)
{
    if (ssid.empty()) return;
    nvs_handle_t h; if (nvs_open_storage(&h) != ESP_OK) return;

    // If SSID exists, update password only
    for (int i = 0; i < 5; ++i) {
        char key[16]; snprintf(key, sizeof(key), "wifi_ssid_%d", i);
        size_t sz = 0;
        if (nvs_get_str(h, key, nullptr, &sz) == ESP_OK && sz > 1) {
            std::string cur; cur.resize(sz);
            if (nvs_get_str(h, key, cur.data(), &sz) == ESP_OK) {
                if (!cur.empty() && cur.back() == '\0') cur.pop_back();
                if (cur == ssid) {
                    // update password
                    char pkey[16]; snprintf(pkey, sizeof(pkey), "wifi_pass_%d", i);
                    nvs_set_str(h, pkey, pass.c_str());
                    nvs_commit(h); nvs_close(h);
                    return;
                }
            }
        }
    }

    // New entry: write to wifi_next index
    uint8_t next = 0; (void)next;
    if (nvs_get_u8(h, "wifi_next", &next) != ESP_OK) next = 0;
    if (next >= 5) next = 0;
    {
        char skey[16]; snprintf(skey, sizeof(skey), "wifi_ssid_%d", next);
        char pkey[16]; snprintf(pkey, sizeof(pkey), "wifi_pass_%d", next);
        nvs_set_str(h, skey, ssid.c_str());
        nvs_set_str(h, pkey, pass.c_str());
        uint8_t nn = (uint8_t)((next + 1) % 5);
        nvs_set_u8(h, "wifi_next", nn);
        nvs_commit(h);
    }
    nvs_close(h);
}

inline std::vector<std::pair<std::string,std::string>> get_wifi_credentials()
{
    std::vector<std::pair<std::string,std::string>> out;
    nvs_handle_t h; if (nvs_open_storage(&h) != ESP_OK) return out;
    for (int i = 0; i < 5; ++i) {
        char skey[16]; snprintf(skey, sizeof(skey), "wifi_ssid_%d", i);
        char pkey[16]; snprintf(pkey, sizeof(pkey), "wifi_pass_%d", i);
        size_t ssz = 0;
        if (nvs_get_str(h, skey, nullptr, &ssz) == ESP_OK && ssz > 1) {
            std::string ssid; ssid.resize(ssz);
            if (nvs_get_str(h, skey, ssid.data(), &ssz) == ESP_OK) {
                if (!ssid.empty() && ssid.back() == '\0') ssid.pop_back();
                size_t psz = 0; std::string pass;
                if (nvs_get_str(h, pkey, nullptr, &psz) == ESP_OK && psz > 0) {
                    pass.resize(psz);
                    if (nvs_get_str(h, pkey, pass.data(), &psz) == ESP_OK) {
                        if (!pass.empty() && pass.back() == '\0') pass.pop_back();
                    } else {
                        pass.clear();
                    }
                }
                out.emplace_back(std::move(ssid), std::move(pass));
            }
        }
    }
    nvs_close(h);
    return out;
}
