#pragma once

#include <string>
#include <utility>
#include <vector>

#include <ArduinoJson.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble_uart.hpp"
#include "chat_api.hpp"
#include "nvs_rw.hpp"

namespace app::contactbook {

struct ContactActionResult {
    bool ok = false;
    std::string error_message;
};

inline std::vector<std::pair<std::string, std::string>> parse_pending_requests(
    const std::string& payload) {
    std::vector<std::pair<std::string, std::string>> out;
    if (payload.empty()) return out;
    DynamicJsonDocument pdoc(4096);
    if (deserializeJson(pdoc, payload) != DeserializationError::Ok) return out;

    JsonArray arr = pdoc["requests"].as<JsonArray>();
    if (arr.isNull()) return out;
    for (JsonObject r : arr) {
        std::string rid = r["request_id"].as<const char*>()
                              ? r["request_id"].as<const char*>()
                              : "";
        std::string uname = r["username"].as<const char*>()
                                ? r["username"].as<const char*>()
                                : "";
        if (!rid.empty()) out.push_back({rid, uname});
    }
    return out;
}

inline ContactActionResult send_friend_request(const std::string& friend_code,
                                               bool use_ble) {
    ContactActionResult result;

    if (use_ble) {
        long long rid = esp_timer_get_time();
        std::string req = std::string("{ \"id\":\"") + std::to_string(rid) +
                          "\", \"type\": \"send_friend_request\", "
                          "\"code\": \"" +
                          friend_code + "\" }\n";
        ble_uart_send(reinterpret_cast<const uint8_t*>(req.c_str()), req.size());

        const int timeout_ms = 3000;
        int waited = 0;
        while (waited < timeout_ms) {
            std::string rid_s = get_nvs((char*)"ble_result_id");
            if (rid_s == std::to_string(rid)) {
                std::string j = get_nvs((char*)"ble_last_result");
                StaticJsonDocument<512> doc;
                if (deserializeJson(doc, j) == DeserializationError::Ok) {
                    result.ok = doc["ok"].as<bool>();
                    if (doc["error"]) {
                        const char* e = doc["error"].as<const char*>();
                        if (e) result.error_message = e;
                    }
                }
                return result;
            }
            vTaskDelay(50 / portTICK_PERIOD_MS);
            waited += 50;
        }
        return result;
    }

    auto& api = chatapi::shared_client(true);
    api.set_scheme("https");
    const auto creds = chatapi::load_credentials_from_nvs();
    (void)chatapi::ensure_authenticated(api, creds, false);
    std::string resp;
    int status = 0;
    auto err = api.send_friend_request(friend_code, &resp, &status);
    result.ok = (err == ESP_OK && status >= 200 && status < 300);
    if (!result.ok && err == ESP_OK && status >= 400) {
        StaticJsonDocument<256> edoc;
        if (deserializeJson(edoc, resp) == DeserializationError::Ok &&
            edoc["error"]) {
            const char* e = edoc["error"].as<const char*>();
            if (e) result.error_message = e;
        }
    }
    return result;
}

inline std::vector<std::pair<std::string, std::string>> fetch_pending_requests(
    bool use_ble, std::function<void()> feed_wdt = nullptr) {
    if (use_ble) {
        long long rid = esp_timer_get_time();
        std::string req = std::string("{ \"id\":\"") + std::to_string(rid) +
                          "\", \"type\": \"get_pending\" }\n";
        ble_uart_send(reinterpret_cast<const uint8_t*>(req.c_str()), req.size());

        const int timeout_ms = 2500;
        int waited = 0;
        while (waited < timeout_ms) {
            if (feed_wdt) feed_wdt();
            std::string js = get_nvs((char*)"ble_pending");
            if (!js.empty()) {
                auto out = parse_pending_requests(js);
                if (!out.empty()) return out;
            }
            vTaskDelay(100 / portTICK_PERIOD_MS);
            waited += 100;
        }
    }

    auto& api = chatapi::shared_client(true);
    api.set_scheme("https");
    const auto creds = chatapi::load_credentials_from_nvs();
    (void)chatapi::ensure_authenticated(api, creds, false);

    std::string presp;
    if (api.get_pending_requests(presp) == ESP_OK) {
        return parse_pending_requests(presp);
    }
    return {};
}

inline ContactActionResult respond_friend_request(const std::string& request_id,
                                                  bool accept, bool use_ble) {
    ContactActionResult result;

    if (use_ble) {
        long long crid = esp_timer_get_time();
        std::string req = std::string("{ \"id\":\"") + std::to_string(crid) +
                          "\", \"type\": \"respond_friend_request\", "
                          "\"request_id\": \"" +
                          request_id + "\", \"accept\": " +
                          (accept ? "true" : "false") + " }\n";
        ble_uart_send(reinterpret_cast<const uint8_t*>(req.c_str()), req.size());

        const int timeout_ms = 2000;
        int waited = 0;
        while (waited < timeout_ms) {
            std::string rid_s = get_nvs((char*)"ble_result_id");
            if (rid_s == std::to_string(crid)) {
                std::string j = get_nvs((char*)"ble_last_result");
                StaticJsonDocument<512> dd;
                if (deserializeJson(dd, j) == DeserializationError::Ok) {
                    result.ok = dd["ok"].as<bool>();
                    if (dd["error"]) {
                        const char* e = dd["error"].as<const char*>();
                        if (e) result.error_message = e;
                    }
                }
                return result;
            }
            vTaskDelay(50 / portTICK_PERIOD_MS);
            waited += 50;
        }
        return result;
    }

    auto& api = chatapi::shared_client(true);
    api.set_scheme("https");
    const auto creds = chatapi::load_credentials_from_nvs();
    (void)chatapi::ensure_authenticated(api, creds, false);

    std::string rresp;
    int rstatus = 0;
    api.respond_friend_request(request_id, accept, &rresp, &rstatus);
    result.ok = (rstatus >= 200 && rstatus < 300);
    return result;
}

}  // namespace app::contactbook
