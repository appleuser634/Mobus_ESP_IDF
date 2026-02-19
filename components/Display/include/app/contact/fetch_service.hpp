#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "ble_uart.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_client.hpp"
#include "nvs_rw.hpp"

#include "app/contact/domain.hpp"

namespace app::contactbook {

struct FetchLoopHooks {
    std::function<void()> on_tick;
    std::function<bool()> should_cancel;
};

inline bool fetch_contacts_via_ble(const std::string& my_username,
                                   std::vector<ContactEntry>& out,
                                   int timeout_ms,
                                   const FetchLoopHooks& hooks = {}) {
    if (!ble_uart_is_ready()) return false;

    long long rid = esp_timer_get_time();
    std::string req = std::string("{ \"id\":\"") + std::to_string(rid) +
                      "\", \"type\": \"get_friends\" }\n";
    ble_uart_send(reinterpret_cast<const uint8_t*>(req.c_str()), req.size());

    int waited = 0;
    while (waited < timeout_ms) {
        std::string js = get_nvs((char*)"ble_contacts");
        if (!js.empty()) {
            DynamicJsonDocument doc(6144);
            if (deserializeJson(doc, js) == DeserializationError::Ok) {
                JsonArray arr = doc["friends"].as<JsonArray>();
                if (!arr.isNull()) {
                    out.clear();
                    for (JsonObject f : arr) {
                        auto c = make_contact_entry(f);
                        if (should_include_contact(c, my_username)) {
                            out.push_back(c);
                        }
                    }
                    return true;
                }
            }
        }

        if (hooks.on_tick) hooks.on_tick();
        if (hooks.should_cancel && hooks.should_cancel()) return false;

        vTaskDelay(100 / portTICK_PERIOD_MS);
        waited += 100;
    }
    return false;
}

inline HttpClient::FriendsResponse fetch_contacts_via_http(
    HttpClient& http_client, const std::string& my_username,
    std::vector<ContactEntry>& out, uint32_t timeout_ms) {
    HttpClient::FriendsResponse resp =
        http_client.fetch_friends_blocking(timeout_ms);
    if (resp.err != ESP_OK || resp.status < 200 || resp.status >= 300) {
        return resp;
    }

    DynamicJsonDocument doc(4096);
    DeserializationError derr = deserializeJson(doc, resp.payload);
    if (derr != DeserializationError::Ok) {
        resp.err = ESP_FAIL;
        return resp;
    }

    out.clear();
    for (JsonObject f : doc["friends"].as<JsonArray>()) {
        auto c = make_contact_entry(f);
        if (should_include_contact(c, my_username)) {
            out.push_back(c);
        }
    }
    return resp;
}

}  // namespace app::contactbook
