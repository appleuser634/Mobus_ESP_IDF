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

#include "app/contact/domain.hpp"

namespace app::contactbook {

struct FetchLoopHooks {
    std::function<void()> on_tick;
    std::function<bool()> should_cancel;
};

inline bool parse_contacts_payload(const std::string& js,
                                   const std::string& my_username,
                                   std::vector<ContactEntry>& out) {
    if (js.empty()) return false;
    DynamicJsonDocument doc(6144);
    if (deserializeJson(doc, js) != DeserializationError::Ok) {
        return false;
    }

    JsonArray arr = doc["friends"].as<JsonArray>();
    if (arr.isNull()) arr = doc["contacts"].as<JsonArray>();
    if (arr.isNull()) arr = doc["data"]["friends"].as<JsonArray>();
    if (arr.isNull()) arr = doc["data"]["contacts"].as<JsonArray>();
    if (arr.isNull() && doc["data"].is<JsonArray>()) {
        arr = doc["data"].as<JsonArray>();
    }
    if (arr.isNull()) return false;

    out.clear();
    for (JsonObject f : arr) {
        auto c = make_contact_entry(f);
        if (should_include_contact(c, my_username)) {
            out.push_back(c);
        }
    }
    return true;
}

inline bool fetch_contacts_via_ble(const std::string& my_username,
                                   std::vector<ContactEntry>& out,
                                   int timeout_ms,
                                   const FetchLoopHooks& hooks = {}) {
    if (!ble_uart_is_ready()) return false;

    long long rid = esp_timer_get_time();
    const std::string rid_str = std::to_string(rid);
    // Drop stale payload before sending a new request.
    ble_uart_clear_cached_contacts();
    std::string req = std::string("{ \"id\":\"") + std::to_string(rid) +
                      "\", \"type\": \"get_friends\" }\n";
    ble_uart_send(reinterpret_cast<const uint8_t*>(req.c_str()), req.size());

    int waited = 0;
    while (waited < timeout_ms) {
        std::string js = ble_uart_get_cached_contacts();
        if (!js.empty()) {
            DynamicJsonDocument doc(6144);
            if (deserializeJson(doc, js) == DeserializationError::Ok) {
                bool id_mismatch = false;
                JsonVariant idv = doc["id"];
                if (!idv.isNull()) {
                    const char* resp_id = idv.as<const char*>();
                    if (resp_id != nullptr && rid_str != resp_id) {
                        id_mismatch = true;
                    }
                }

                if (!id_mismatch &&
                    parse_contacts_payload(js, my_username, out)) {
                        // Consume once to avoid reusing old contacts payload.
                        ble_uart_clear_cached_contacts();
                        return true;
                }
            }
        }
        if (hooks.on_tick) hooks.on_tick();
        if (hooks.should_cancel && hooks.should_cancel()) return false;

        vTaskDelay(100 / portTICK_PERIOD_MS);
        waited += 100;
    }
    // Ensure timeout leaves no stale payload.
    ble_uart_clear_cached_contacts();
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
