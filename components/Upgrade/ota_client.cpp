#include "include/ota_client.hpp"

#include <cstring>
#include <string>

#include "esp_app_desc.h"
#include "esp_http_client.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);
#define HAVE_CRT_BUNDLE 1
#else
#define HAVE_CRT_BUNDLE 0
#endif
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "bootloader_common.h"
#include "esp_flash_partitions.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <memory>

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
    // Use TLS only for HTTPS endpoints
    bool is_https = url.rfind("https://", 0) == 0;
#if HAVE_CRT_BUNDLE
    if (is_https) cfg.crt_bundle_attach = esp_crt_bundle_attach; else cfg.crt_bundle_attach = nullptr;
    cfg.cert_pem = nullptr;
#else
    cfg.cert_pem = is_https ? (const char*)ca_cert_pem_start : nullptr;
#endif
    cfg.timeout_ms = 5000;
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

static std::string rewrite_dev_url_http(const std::string& url) {
    // Force HTTP for common local dev patterns (port 3000 or dev IP/localhost)
    auto starts_with = [](const std::string& s, const char* p){ return s.rfind(p, 0) == 0; };
    if (url.find(":3000/") != std::string::npos ||
        url.find("192.168.2.184") != std::string::npos ||
        url.find("localhost") != std::string::npos ||
        url.find("127.0.0.1") != std::string::npos) {
        if (starts_with(url, "https://")) {
            std::string u = url; u.replace(0, 8, "http://"); return u;
        }
    }
    return url;
}

// Download firmware via HTTP(S), write to next OTA partition, and set it to boot.
// Validation of the image is deferred to bootloader/app (rollback flow).
esp_err_t do_ota(const std::string& bin_url_in) {
    auto set_boot_partition_unverified = [](const esp_partition_t* update_partition) -> esp_err_t {
        if (!update_partition || update_partition->type != ESP_PARTITION_TYPE_APP ||
            update_partition->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MIN || update_partition->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
            return ESP_ERR_INVALID_ARG;
        }

        // Read current OTA data entries
        const esp_partition_t* otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
        if (!otadata) return ESP_ERR_NOT_FOUND;

        esp_ota_select_entry_t two[2];
        esp_err_t err = ESP_OK;
        size_t esz = otadata->erase_size;
        if ((err = esp_partition_read(otadata, 0, &two[0], sizeof(two[0]))) != ESP_OK) return err;
        if ((err = esp_partition_read(otadata, esz, &two[1], sizeof(two[1]))) != ESP_OK) return err;

        int active = bootloader_common_get_active_otadata(two);
        int n = (int)esp_ota_get_app_partition_count();
        if (n <= 0) return ESP_ERR_NOT_FOUND;
        int target_index = (int)update_partition->subtype - (int)ESP_PARTITION_SUBTYPE_APP_OTA_MIN; // 0..n-1
        uint32_t base = (uint32_t)((target_index + 1) % n);
        uint32_t cur_seq = (active >= 0) ? two[active].ota_seq : 0;
        uint32_t i = 0;
        while (cur_seq > base + i * (uint32_t)n) i++;
        uint32_t new_seq = (active >= 0) ? (base + i * (uint32_t)n) : (uint32_t)(target_index + 1);
        int next = (active >= 0) ? ((~active) & 1) : 0;

        esp_ota_select_entry_t new_entry = two[next];
        new_entry.ota_seq = new_seq;
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
        new_entry.ota_state = ESP_OTA_IMG_NEW;
#else
        new_entry.ota_state = ESP_OTA_IMG_UNDEFINED;
#endif
        new_entry.crc = bootloader_common_ota_select_crc(&new_entry);

        if ((err = esp_partition_erase_range(otadata, (size_t)next * esz, esz)) != ESP_OK) return err;
        if ((err = esp_partition_write(otadata, (size_t)next * esz, &new_entry, sizeof(new_entry))) != ESP_OK) return err;
        return ESP_OK;
    };

    auto download_and_write = [&](const std::string& url) -> esp_err_t {
        esp_http_client_config_t hc = {};
        hc.url = url.c_str();
        hc.timeout_ms = 15000;
        hc.keep_alive_enable = true;
        bool is_https = url.rfind("https://", 0) == 0;
#if HAVE_CRT_BUNDLE
        if (is_https) hc.crt_bundle_attach = esp_crt_bundle_attach; else hc.crt_bundle_attach = nullptr;
        hc.cert_pem = nullptr;
#else
        hc.cert_pem = is_https ? (const char*)ca_cert_pem_start : nullptr;
#endif

        ESP_LOGI(TAG_OTA, "OTA from %s", url.c_str());
        esp_http_client_handle_t client = esp_http_client_init(&hc);
        if (!client) return ESP_FAIL;
        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_OTA, "HTTP open failed: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return err;
        }

        // Read and log response headers. Ensure status 200 before reading body.
        int64_t content_len = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG_OTA, "HTTP status=%d, content_length=%lld", status, (long long)content_len);
        if (status != 200) {
            ESP_LOGE(TAG_OTA, "Unexpected HTTP status: %d", status);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
        if (!update_partition) {
            ESP_LOGE(TAG_OTA, "No OTA partition available");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG_OTA, "Writing to <%s> partition at offset 0x%lx", update_partition->label, (unsigned long)update_partition->address);

        esp_ota_handle_t ota_handle = 0;
        err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_OTA, "esp_ota_begin failed: %s", esp_err_to_name(err));
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return err;
        }

        constexpr size_t BUF_SZ = 16 * 1024;
        std::unique_ptr<uint8_t[], void(*)(void*)> buf((uint8_t*)heap_caps_malloc(BUF_SZ, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT), free);
        if (!buf) {
            buf.reset((uint8_t*)malloc(BUF_SZ));
        }
        if (!buf) {
            ESP_LOGE(TAG_OTA, "No memory for OTA buffer");
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_ERR_NO_MEM;
        }

        int read_total = 0;
        while (true) {
            int r = esp_http_client_read(client, (char*)buf.get(), BUF_SZ);
            if (r < 0) {
                ESP_LOGE(TAG_OTA, "HTTP read error");
                err = ESP_FAIL;
                break;
            }
            if (r == 0) {
                // EOF
                err = ESP_OK;
                break;
            }
            err = esp_ota_write(ota_handle, buf.get(), r);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_OTA, "esp_ota_write failed at %d: %s", read_total, esp_err_to_name(err));
                break;
            }
            read_total += r;
        }

        if (read_total == 0) {
            ESP_LOGE(TAG_OTA, "No data received for OTA image");
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            return err;
        }

        err = esp_ota_end(ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_OTA, "esp_ota_end failed: %s", esp_err_to_name(err));
            if (err != ESP_ERR_OTA_VALIDATE_FAILED) {
                return err;
            }
            ESP_LOGW(TAG_OTA, "Proceeding despite validation failure; will validate on boot");
        }

        // Set next boot slot without pre-validation, rely on bootloader validation
        err = set_boot_partition_unverified(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_OTA, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG_OTA, "Set boot partition to: %s; rebooting for validation", update_partition->label);
        esp_restart();
        return ESP_OK; // not reached
    };

    // First try with given URL; if it's an HTTPS dev URL, also try HTTP fallback
    std::string url = rewrite_dev_url_http(bin_url_in);
    esp_err_t err = download_and_write(url);
    if (err == ESP_OK) return err; // rebooted already

    bool was_https = bin_url_in.rfind("https://", 0) == 0;
    std::string alt = rewrite_dev_url_http(bin_url_in);
    if (was_https && alt != bin_url_in) {
        ESP_LOGW(TAG_OTA, "Retry OTA via HTTP: %s", alt.c_str());
        return download_and_write(alt);
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
        // Default to production OTA server
        manifest = "https://mimoc.jp/api/firmware/latest?device=esp32s3&channel=stable";
    }
    manifest = rewrite_dev_url_http(manifest);
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
