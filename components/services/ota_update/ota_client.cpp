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
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "bootloader_common.h"
#include "esp_flash_partitions.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <memory>

static const char* TAG_OTA = "OTAClient";
static constexpr int kOtaMaxAttempts = 3;
static constexpr int kRangeChunkSize = 256 * 1024;
static constexpr int kRangeNoProgressMaxRetries = 5;
ota_client::progress_callback_t g_progress_cb = nullptr;
void* g_progress_user_data = nullptr;

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

void nvs_set_string(const char* key, const char* value) {
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READWRITE, &h) != ESP_OK) return;
    (void)nvs_set_str(h, key, value);
    (void)nvs_commit(h);
    nvs_close(h);
}

inline void feed_task_wdt_best_effort() {
    if (esp_task_wdt_status(nullptr) == ESP_OK) {
        (void)esp_task_wdt_reset();
    }
}

inline void delay_with_wdt(int ms_total) {
    const int step_ms = 100;
    int left = ms_total;
    while (left > 0) {
        feed_task_wdt_best_effort();
        const int d = (left > step_ms) ? step_ms : left;
        vTaskDelay(pdMS_TO_TICKS(d));
        left -= d;
    }
}

inline void notify_progress(int downloaded_bytes, int total_bytes,
                            const char* phase) {
    if (g_progress_cb) {
        g_progress_cb(downloaded_bytes, total_bytes, phase, g_progress_user_data);
    }
}

esp_err_t set_boot_partition_unverified(const esp_partition_t* update_partition) {
    if (!update_partition || update_partition->type != ESP_PARTITION_TYPE_APP ||
        update_partition->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MIN ||
        update_partition->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t* otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
    if (!otadata) return ESP_ERR_NOT_FOUND;

    esp_ota_select_entry_t two[2];
    esp_err_t err = ESP_OK;
    const size_t esz = otadata->erase_size;
    if ((err = esp_partition_read(otadata, 0, &two[0], sizeof(two[0]))) != ESP_OK) {
        return err;
    }
    if ((err = esp_partition_read(otadata, esz, &two[1], sizeof(two[1]))) != ESP_OK) {
        return err;
    }

    const int active = bootloader_common_get_active_otadata(two);
    const int app_count = (int)esp_ota_get_app_partition_count();
    if (app_count <= 0) return ESP_ERR_NOT_FOUND;

    const int target_index =
        (int)update_partition->subtype - (int)ESP_PARTITION_SUBTYPE_APP_OTA_MIN;
    const uint32_t base = (uint32_t)((target_index + 1) % app_count);
    const uint32_t cur_seq = (active >= 0) ? two[active].ota_seq : 0;
    uint32_t i = 0;
    while (cur_seq > base + i * (uint32_t)app_count) {
        ++i;
    }
    const uint32_t new_seq =
        (active >= 0) ? (base + i * (uint32_t)app_count)
                      : (uint32_t)(target_index + 1);
    const int next = (active >= 0) ? ((~active) & 1) : 0;

    esp_ota_select_entry_t new_entry = two[next];
    new_entry.ota_seq = new_seq;
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    new_entry.ota_state = ESP_OTA_IMG_NEW;
#else
    new_entry.ota_state = ESP_OTA_IMG_UNDEFINED;
#endif
    new_entry.crc = bootloader_common_ota_select_crc(&new_entry);

    if ((err = esp_partition_erase_range(otadata, (size_t)next * esz, esz)) != ESP_OK) {
        return err;
    }
    if ((err = esp_partition_write(otadata, (size_t)next * esz, &new_entry,
                                   sizeof(new_entry))) != ESP_OK) {
        return err;
    }
    return ESP_OK;
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
        feed_task_wdt_best_effort();
        vTaskDelay(1);
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

// Download firmware and reboot into updated slot.
esp_err_t do_ota(const std::string& bin_url_in) {
    auto download_and_write = [&](const std::string& url) -> esp_err_t {
        auto init_http_client = [&](esp_http_client_config_t& hc) {
            hc = {};
            hc.url = url.c_str();
            hc.timeout_ms = 30000;
            hc.keep_alive_enable = false;
            hc.buffer_size = 16 * 1024;
            const bool is_https = url.rfind("https://", 0) == 0;
#if HAVE_CRT_BUNDLE
            if (is_https) hc.crt_bundle_attach = esp_crt_bundle_attach;
            else hc.crt_bundle_attach = nullptr;
            hc.cert_pem = nullptr;
#else
            hc.cert_pem = is_https ? (const char*)ca_cert_pem_start : nullptr;
#endif
        };
        ESP_LOGI(TAG_OTA, "OTA from %s", url.c_str());
        ESP_LOGI(TAG_OTA, "Partial HTTP download enabled: chunk=%d bytes",
                 kRangeChunkSize);
        ESP_LOGI(TAG_OTA, "Heap before OTA: free_int=%u largest_int=%u free_psram=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        feed_task_wdt_best_effort();

        // 1) Determine total image length with HEAD first.
        int64_t content_len = -1;
        {
            esp_http_client_config_t hc;
            init_http_client(hc);
            esp_http_client_handle_t c = esp_http_client_init(&hc);
            if (!c) return ESP_FAIL;
            esp_http_client_set_method(c, HTTP_METHOD_HEAD);
            esp_err_t herr = esp_http_client_perform(c);
            int hstatus = esp_http_client_get_status_code(c);
            if (herr == ESP_OK && hstatus == 200) {
                content_len = esp_http_client_get_content_length(c);
            }
            esp_http_client_cleanup(c);
            if (content_len <= 0) {
                ESP_LOGE(TAG_OTA, "HEAD failed (status=%d, len=%lld)", hstatus,
                         (long long)content_len);
                return ESP_FAIL;
            }
        }
        ESP_LOGI(TAG_OTA, "HTTP content_length=%lld", (long long)content_len);
        notify_progress(0, (int)content_len, "downloading");

        const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
        if (!update_partition) {
            ESP_LOGE(TAG_OTA, "No OTA partition available");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG_OTA, "Writing to <%s> partition at offset 0x%lx",
                 update_partition->label, (unsigned long)update_partition->address);

        esp_ota_handle_t ota_handle = 0;
        esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_OTA, "esp_ota_begin failed: %s", esp_err_to_name(err));
            return err;
        }

        constexpr size_t BUF_SZ = 16 * 1024;
        std::unique_ptr<uint8_t[], void(*)(void*)> buf(
            (uint8_t*)heap_caps_malloc(BUF_SZ, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
            free);
        if (!buf) {
            buf.reset((uint8_t*)heap_caps_malloc(BUF_SZ, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT));
        }
        if (!buf) {
            buf.reset((uint8_t*)malloc(BUF_SZ));
        }
        if (!buf) {
            esp_ota_abort(ota_handle);
            return ESP_ERR_NO_MEM;
        }

        int last_progress_kb = -1;
        int read_total = 0;
        int64_t offset = 0;
        int no_progress_retries = 0;
        err = ESP_OK;
        while (offset < content_len) {
            const int64_t end = std::min<int64_t>(offset + kRangeChunkSize - 1,
                                                  content_len - 1);
            std::string range = "bytes=" + std::to_string((long long)offset) +
                                "-" + std::to_string((long long)end);

            esp_http_client_config_t hc;
            init_http_client(hc);
            esp_http_client_handle_t c = esp_http_client_init(&hc);
            if (!c) {
                err = ESP_FAIL;
                break;
            }
            esp_http_client_set_method(c, HTTP_METHOD_GET);
            esp_http_client_set_header(c, "Range", range.c_str());
            esp_http_client_set_header(c, "Connection", "close");

            err = esp_http_client_open(c, 0);
            if (err != ESP_OK) {
                esp_http_client_cleanup(c);
                if (++no_progress_retries > kRangeNoProgressMaxRetries) break;
                ESP_LOGW(TAG_OTA, "Range open failed at %lld (retry %d/%d): %s",
                         (long long)offset, no_progress_retries,
                         kRangeNoProgressMaxRetries, esp_err_to_name(err));
                err = ESP_OK;
                delay_with_wdt(300 * no_progress_retries);
                continue;
            }
            (void)esp_http_client_fetch_headers(c);
            const int status = esp_http_client_get_status_code(c);
            if (!(status == 206 || (status == 200 && offset == 0))) {
                esp_http_client_close(c);
                esp_http_client_cleanup(c);
                err = ESP_FAIL;
                if (++no_progress_retries > kRangeNoProgressMaxRetries) break;
                ESP_LOGW(TAG_OTA, "Range status %d at %lld (retry %d/%d)", status,
                         (long long)offset, no_progress_retries,
                         kRangeNoProgressMaxRetries);
                err = ESP_OK;
                delay_with_wdt(300 * no_progress_retries);
                continue;
            }

            int bytes_before = read_total;
            while (true) {
                int want = (int)std::min<int64_t>((int64_t)BUF_SZ, end - offset + 1);
                int r = esp_http_client_read(c, (char*)buf.get(), want);
                if (r < 0) {
                    err = ESP_FAIL;
                    break;
                }
                if (r == 0) {
                    if (offset <= end) {
                        err = ESP_FAIL;
                    }
                    break;
                }
                err = esp_ota_write(ota_handle, buf.get(), r);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG_OTA, "esp_ota_write failed at %d: %s", read_total,
                             esp_err_to_name(err));
                    break;
                }
                read_total += r;
                int progress_kb = read_total / 1024;
                if (progress_kb / 128 != last_progress_kb / 128) {
                    ESP_LOGI(TAG_OTA, "OTA progress: %d KB", progress_kb);
                    last_progress_kb = progress_kb;
                }
                notify_progress(read_total, (int)content_len, "downloading");
                offset += r;
                feed_task_wdt_best_effort();
                vTaskDelay(1);
            }
            esp_http_client_close(c);
            esp_http_client_cleanup(c);
            if (err == ESP_OK && offset > end) {
                no_progress_retries = 0;
                continue;
            }
            if (read_total == bytes_before) {
                if (++no_progress_retries > kRangeNoProgressMaxRetries) break;
                ESP_LOGW(TAG_OTA, "Range stalled at %lld (retry %d/%d)",
                         (long long)offset, no_progress_retries,
                         kRangeNoProgressMaxRetries);
                err = ESP_OK;
                delay_with_wdt(300 * no_progress_retries);
                continue;
            }
            no_progress_retries = 0;
            err = ESP_OK;
        }

        if (read_total == 0 || offset < content_len) {
            notify_progress(read_total, (int)content_len, "failed");
            esp_ota_abort(ota_handle);
            return ESP_FAIL;
        }
        if (err != ESP_OK) {
            notify_progress(read_total, (int)content_len, "failed");
            esp_ota_abort(ota_handle);
            return err;
        }

        err = esp_ota_end(ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_OTA, "esp_ota_end failed: %s", esp_err_to_name(err));
            if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGW(TAG_OTA,
                         "Validation mmap failed; switching boot slot as unverified");
                notify_progress(read_total, (int)content_len, "validating_failed");
                esp_err_t set_err = set_boot_partition_unverified(update_partition);
                if (set_err == ESP_OK) {
                    notify_progress(read_total, (int)content_len, "rebooting");
                    ESP_LOGI(TAG_OTA, "Boot slot updated (unverified). Rebooting.");
                    esp_restart();
                }
                ESP_LOGE(TAG_OTA, "set_boot_partition_unverified failed: %s",
                         esp_err_to_name(set_err));
            }
            return err;
        }
        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_OTA, "esp_ota_set_boot_partition failed: %s",
                     esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG_OTA, "OTA write complete; boot partition set to %s; rebooting",
                 update_partition->label);
        notify_progress((int)content_len, (int)content_len, "rebooting");
        esp_restart();
        return ESP_OK; // not reached
    };

    std::string url = rewrite_dev_url_http(bin_url_in);
    esp_err_t err = ESP_FAIL;
    wifi_ps_type_t ps_prev = WIFI_PS_MIN_MODEM;
    bool ps_changed = false;
    if (esp_wifi_get_ps(&ps_prev) == ESP_OK) {
        if (esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK) {
            ps_changed = true;
            ESP_LOGI(TAG_OTA, "Wi-Fi power save disabled during OTA");
        }
    }
    for (int attempt = 1; attempt <= kOtaMaxAttempts; ++attempt) {
        err = download_and_write(url);
        if (err == ESP_OK) return err; // rebooted already
        notify_progress(0, 0, "retry");
        ESP_LOGW(TAG_OTA, "OTA attempt %d/%d failed: %s", attempt,
                 kOtaMaxAttempts, esp_err_to_name(err));
        delay_with_wdt(1000 * attempt);
    }

    bool was_https = bin_url_in.rfind("https://", 0) == 0;
    std::string alt = rewrite_dev_url_http(bin_url_in);
    if (was_https && alt != bin_url_in) {
        ESP_LOGW(TAG_OTA, "Retry OTA via HTTP: %s", alt.c_str());
        for (int attempt = 1; attempt <= kOtaMaxAttempts; ++attempt) {
            err = download_and_write(alt);
            if (err == ESP_OK) return err; // rebooted already
            notify_progress(0, 0, "retry");
            ESP_LOGW(TAG_OTA, "OTA(HTTP) attempt %d/%d failed: %s", attempt,
                     kOtaMaxAttempts, esp_err_to_name(err));
            delay_with_wdt(1000 * attempt);
        }
    }
    if (ps_changed) {
        (void)esp_wifi_set_ps(ps_prev);
        ESP_LOGI(TAG_OTA, "Wi-Fi power save restored");
    }
    return err;
}
}

namespace ota_client {

void set_progress_callback(progress_callback_t cb, void* user_data) {
    g_progress_cb = cb;
    g_progress_user_data = user_data;
}

esp_err_t check_and_update_once() {
    bool wdt_removed_here = false;
    if (esp_task_wdt_status(nullptr) == ESP_OK) {
        if (esp_task_wdt_delete(nullptr) == ESP_OK) {
            wdt_removed_here = true;
            ESP_LOGI(TAG_OTA, "Temporarily removed current task from TWDT during OTA");
        }
    }

    auto finish = [&](esp_err_t r) -> esp_err_t {
        if (wdt_removed_here) {
            (void)esp_task_wdt_add(nullptr);
        }
        return r;
    };

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
    if (http_get(url, body) != ESP_OK) return finish(ESP_FAIL);
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
        return finish(ESP_OK);
    }
    return finish(do_ota(bin));
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
