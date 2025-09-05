#include "include/ota_client.hpp"

#include <cstring>
#include <string>

#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>

static const char* TAG_OTA = "OTAClient";

extern "C" {
extern const unsigned char ca_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const unsigned char ca_cert_pem_end[]   asm("_binary_ca_cert_pem_end");
}

namespace {
std::string nvs_get(const char* key) {
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READONLY, &h) != ESP_OK) return "";
    size_t len = 0; std::string s;
    if (nvs_get_str(h, key, nullptr, &len) == ESP_OK && len > 0) {
        s.resize(len);
        if (nvs_get_str(h, key, s.data(), &len) != ESP_OK) s.clear();
        if (!s.empty() && s.back()=='\0') s.pop_back();
    }
    nvs_close(h);
    return s;
}

// helper kept for potential future use
// static void nvs_put(const char* key, const std::string& v) {
//     nvs_handle_t h;
//     if (nvs_open("storage", NVS_READWRITE, &h) != ESP_OK) return;
//     nvs_set_str(h, key, v.c_str());
//     nvs_commit(h);
//     nvs_close(h);
// }

esp_err_t http_get(const std::string& url, std::string& out) {
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.cert_pem = (const char*)ca_cert_pem_start;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;
    esp_err_t err = esp_http_client_open(c, 0);
    if (err != ESP_OK) { esp_http_client_cleanup(c); return err; }
    (void)esp_http_client_fetch_headers(c);
    out.clear(); char buf[512];
    while (true) {
        int r = esp_http_client_read(c, buf, sizeof(buf));
        if (r <= 0) {
            break;
        }
        out.append(buf, buf + r);
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return ESP_OK;
}

bool version_is_newer(const std::string& current, const std::string& latest) {
    // Simple compare: different string => treat as newer
    return current != latest && !latest.empty();
}

esp_err_t do_ota(const std::string& bin_url) {
    esp_http_client_config_t hc = {};
    hc.url = bin_url.c_str();
    // Use CA cert only for HTTPS
    bool is_https = bin_url.rfind("https://", 0) == 0;
    hc.cert_pem = is_https ? (const char*)ca_cert_pem_start : nullptr;
    esp_https_ota_config_t ocfg = { .http_config = &hc };
    ESP_LOGI(TAG_OTA, "OTA from %s", bin_url.c_str());
    auto err = esp_https_ota(&ocfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_OTA, "OTA succeed, rebooting");
        esp_restart();
    } else {
        ESP_LOGE(TAG_OTA, "OTA failed: %s", esp_err_to_name(err));
    }
    return err;
}
}

namespace ota_client {

esp_err_t check_and_update_once() {
    const esp_app_desc_t* app = esp_app_get_description();
    std::string current = app ? std::string(app->version) : std::string("0.0.0");
    std::string manifest = nvs_get("ota_manifest");
    if (manifest.empty()) {
        // Default to specified OTA server IP
        manifest = "http://192.168.2.184:3000/api/firmware/latest?device=esp32s3&channel=stable";
    }
    // Append current version as hint
    std::string url = manifest + std::string("&current=") + current;
    std::string body;
    if (http_get(url, body) != ESP_OK) return ESP_FAIL;
    std::string latest, bin;
    auto extract_string = [&](const char* key) -> std::string {
        std::string k = std::string("\"") + key + "\""; // "key"
        auto pos = body.find(k);
        if (pos == std::string::npos) return "";
        pos = body.find(':', pos);
        if (pos == std::string::npos) return "";
        // skip spaces
        while (pos < body.size() && (body[pos] == ':' || body[pos] == ' ')) pos++;
        if (pos >= body.size() || body[pos] != '"') return "";
        pos++;
        std::string out;
        while (pos < body.size()) {
            char c = body[pos++];
            if (c == '\\') { // escape
                if (pos < body.size()) out.push_back(body[pos++]);
            } else if (c == '"') {
                break;
            } else {
                out.push_back(c);
            }
        }
        return out;
    };
    latest = extract_string("version");
    bin    = extract_string("url");
    if (!version_is_newer(current, latest) || bin.empty()) {
        ESP_LOGI(TAG_OTA, "No update. current=%s latest=%s", current.c_str(), latest.c_str());
        return ESP_OK;
    }
    return do_ota(bin);
}

static void ota_bg_task(void*) {
    while (true) {
        std::string auto_flag = nvs_get("ota_auto");
        if (auto_flag == "true") {
            (void)check_and_update_once();
        }
        // Check every 6 hours
        vTaskDelay(pdMS_TO_TICKS(6*60*60*1000));
    }
}

void start_background_task() {
    static bool started = false; if (started) return; started = true;
    xTaskCreatePinnedToCore(ota_bg_task, "ota_bg_task", 8192, nullptr, 5, nullptr, 0);
}

} // namespace ota_client
