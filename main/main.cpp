#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>

#include "esp_psram.h"  // v4.4+（古いIDFは esp_spiram.h）

#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "sdkconfig.h"
#include <ArduinoJson.h>
#include "esp_now.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include <cstdio>
#include <vector>
#include <functional>
#include <limits>
#include <memory>

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <boot_sounds.hpp>

void check_heap() {
    ESP_LOGI("HEAP", "Largest Free Block: %d",
             heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
}

namespace {

constexpr bool kMemoryProfilerEnabled = true;

struct HeapSnapshot {
    size_t free_internal = 0;
    size_t largest_internal = 0;
    size_t allocated_internal = 0;
    size_t free_psram = 0;
    size_t largest_psram = 0;
};

static HeapSnapshot TakeHeapSnapshot() {
    HeapSnapshot snap;
    multi_heap_info_t info = {};
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    snap.free_internal = info.total_free_bytes;
    snap.largest_internal = info.largest_free_block;
    snap.allocated_internal = info.total_allocated_bytes;

    multi_heap_info_t psram_info = {};
    heap_caps_get_info(&psram_info, MALLOC_CAP_SPIRAM);
    snap.free_psram = psram_info.total_free_bytes;
    snap.largest_psram = psram_info.largest_free_block;
    return snap;
}

class MemoryProfiler {
   public:
    explicit MemoryProfiler(bool enabled) : enabled_(enabled) {
        if (enabled_) {
            reports_.reserve(16);
        }
    }

    template <typename Func>
    void run_step(const char* name, Func&& func) {
        if (!enabled_) {
            func();
            return;
        }
        HeapSnapshot before = TakeHeapSnapshot();
        func();
        HeapSnapshot after = TakeHeapSnapshot();
        reports_.push_back({std::string(name), before, after});
        log_step(reports_.back());
    }

    void report_summary() const {
        if (!enabled_ || reports_.empty()) return;
        constexpr const char* tag = "MEM_PROFILE";
        ESP_LOGI(tag, "===== Memory Profile Summary (%zu steps) =====",
                 reports_.size());
        const StepReport& first = reports_.front();
        const StepReport& last = reports_.back();
        size_t min_free = std::numeric_limits<size_t>::max();
        const StepReport* min_step = nullptr;
        for (const auto& r : reports_) {
            if (r.after.free_internal < min_free) {
                min_free = r.after.free_internal;
                min_step = &r;
            }
        }
        if (min_step) {
            ESP_LOGI(tag, "Lowest free internal heap: %zu bytes after '%s'",
                     min_step->after.free_internal, min_step->name.c_str());
        }
        double initial_usage = usage_percent(first.before);
        double final_usage = usage_percent(last.after);
        ESP_LOGI(tag,
                 "Initial internal usage: %.2f%%, Final internal usage: %.2f%%"
                 " (delta: %+0.2f%%)",
                 initial_usage, final_usage, final_usage - initial_usage);
        ESP_LOGI(tag, "Remaining internal free: %zu bytes",
                 last.after.free_internal);
        ESP_LOGI(tag, "Largest free internal block: %zu bytes",
                 last.after.largest_internal);
        if (last.after.free_psram > 0 || last.after.largest_psram > 0) {
            ESP_LOGI(tag, "Remaining PSRAM free: %zu bytes (largest block %zu)",
                     last.after.free_psram, last.after.largest_psram);
        }
        ESP_LOGI(tag, "===== End Memory Profile =====");
    }

    bool enabled() const { return enabled_; }

   private:
    struct StepReport {
        std::string name;
        HeapSnapshot before;
        HeapSnapshot after;
    };

    static double usage_percent(const HeapSnapshot& snap) {
        const size_t total = snap.free_internal + snap.allocated_internal;
        if (total == 0) return 0.0;
        return static_cast<double>(snap.allocated_internal) * 100.0 /
               static_cast<double>(total);
    }

    void log_step(const StepReport& report) {
        constexpr const char* tag = "MEM_PROFILE";
        long long delta_free =
            static_cast<long long>(report.after.free_internal) -
            static_cast<long long>(report.before.free_internal);
        long long delta_largest =
            static_cast<long long>(report.after.largest_internal) -
            static_cast<long long>(report.before.largest_internal);
        long long delta_psram =
            static_cast<long long>(report.after.free_psram) -
            static_cast<long long>(report.before.free_psram);
        long long delta_psram_largest =
            static_cast<long long>(report.after.largest_psram) -
            static_cast<long long>(report.before.largest_psram);
        double usage_before = usage_percent(report.before);
        double usage_after = usage_percent(report.after);
        ESP_LOGI(
            tag,
            "[%s] used=%.2f%% (%+.2f%%) free=%zu (%+lld) largest=%zu (%+lld) "
            "psram_free=%zu (%+lld) psram_largest=%zu (%+lld)",
            report.name.c_str(), usage_after, usage_after - usage_before,
            report.after.free_internal, delta_free,
            report.after.largest_internal, delta_largest,
            report.after.free_psram, delta_psram, report.after.largest_psram,
            delta_psram_largest);
    }

    bool enabled_ = false;
    std::vector<StepReport> reports_;
};

}  // namespace

static const char* TAG = "Mobus v3.14";

#include <map>
#include <string.h>

#include <LovyanGFX.hpp>
#include <joystick.h>
#include <power_monitor.h>
#include <button.h>
#include <haptic_motor.hpp>
#include <joystick_haptics.hpp>

#include <nvs_rw.hpp>
#include <wifi.hpp>
#include <http_client.hpp>
#include <notification_effects.hpp>
#include <neopixel.hpp>
#include <oled.hpp>
#include <ntp.hpp>
#include <max98357a.h>
#include "esp_attr.h"

extern "C" void mobus_request_factory_reset();
extern "C" void mobus_request_ota_minimal_mode();

namespace {
static constexpr uint32_t kFactoryResetMagic = 0x46525354u;  // 'FRST'
static constexpr uint32_t kOtaMinimalMagic = 0x4F54414Du;    // 'OTAM'
// Must survive esp_restart(); do not initialize at startup.
RTC_NOINIT_ATTR uint32_t s_factory_reset_magic;
RTC_NOINIT_ATTR uint32_t s_ota_minimal_magic;
int s_last_ota_progress_percent = -1;
int64_t s_last_ota_progress_draw_us = 0;

void draw_ota_progress_screen(int downloaded_bytes, int total_bytes,
                              const char* phase) {
    if (!ensure_sprite_surface(128, 64, 1, "OTAProgress")) return;

    int percent = 0;
    if (total_bytes > 0 && downloaded_bytes > 0) {
        percent = (downloaded_bytes * 100) / total_bytes;
        if (percent > 100) percent = 100;
    }

    const int64_t now_us = esp_timer_get_time();
    if (strcmp(phase, "downloading") == 0) {
        if (percent == s_last_ota_progress_percent &&
            (now_us - s_last_ota_progress_draw_us) < 250000) {
            return;
        }
        s_last_ota_progress_percent = percent;
    }
    s_last_ota_progress_draw_us = now_us;

    sprite.fillRect(0, 0, 128, 64, 0);
    sprite.setFont(&fonts::Font2);
    sprite.setTextColor(1, 0);
    sprite.drawCenterString("OTA Update", 64, 2);

    if (strcmp(phase, "retry") == 0) {
        sprite.drawCenterString("Retrying...", 64, 18);
    } else if (strcmp(phase, "validating_failed") == 0) {
        sprite.drawCenterString("Validate fallback", 64, 18);
    } else if (strcmp(phase, "rebooting") == 0) {
        sprite.drawCenterString("Rebooting...", 64, 18);
    } else if (strcmp(phase, "failed") == 0) {
        sprite.drawCenterString("Download failed", 64, 18);
    } else {
        sprite.drawCenterString("Downloading...", 64, 18);
    }

    const int bar_x = 10;
    const int bar_y = 36;
    const int bar_w = 108;
    const int bar_h = 12;
    sprite.drawRect(bar_x, bar_y, bar_w, bar_h, 1);
    const int fill_w = ((bar_w - 2) * percent) / 100;
    if (fill_w > 0) {
        sprite.fillRect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, 1);
    }

    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    sprite.setTextColor(1, 0);
    sprite.drawCenterString(pct, 64, 50);
    push_sprite_safe(0, 0);
}

void ota_progress_callback(int downloaded_bytes, int total_bytes,
                           const char* phase, void* user_data) {
    (void)user_data;
    draw_ota_progress_screen(downloaded_bytes, total_bytes, phase);
}

static void handle_factory_reset_if_requested() {
    if (s_factory_reset_magic != kFactoryResetMagic) return;
    s_factory_reset_magic = 0;

    ESP_LOGW(TAG, "[FactoryReset] requested; erasing NVS and rebooting");

    (void)nvs_flash_deinit();
    (void)nvs_flash_init();
    const esp_err_t erase_default = nvs_flash_erase();
    const esp_err_t erase_labeled = nvs_flash_erase_partition("nvs");
    if (erase_default != ESP_OK && erase_labeled != ESP_OK) {
        ESP_LOGE(TAG, "[FactoryReset] erase failed: default=%s labeled=%s",
                 esp_err_to_name(erase_default),
                 esp_err_to_name(erase_labeled));
    }
    (void)nvs_flash_init();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}
}  // namespace

extern "C" void mobus_request_factory_reset() {
    s_factory_reset_magic = kFactoryResetMagic;
    esp_restart();
}

extern "C" void mobus_request_ota_minimal_mode() {
    s_ota_minimal_magic = kOtaMinimalMagic;
    esp_restart();
}

#define uS_TO_S_FACTOR 1000000ULL  // 秒→マイクロ秒

// Provide a strong definition for Wi‑Fi event group
extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
}
EventGroupHandle_t s_wifi_event_group = nullptr;

// #include <notification.hpp>

// #include <provisioning.h>
// OTA client (auto update)
#include "ota_client.hpp"

extern "C" {
void app_main();
}

void check_notification() {
    Oled oled;
    (void)oled;  // suppress unused warning
    auto& buzzer = audio::speaker();
    buzzer.init();
    HapticMotor& haptic = HapticMotor::instance();

    printf("通知チェック中...");
    HttpClient& http_client = HttpClient::shared();
    // 通知の取得
    http_client.start_notifications();

    auto trigger_notification = [&]() {
        printf("got notification!\n");
        for (int n = 0; n < 2; n++) {
            buzzer.start_tone(2600.0f, 0.6f);
            neopixel.set_color(0, 10, 100);
            haptic.pulse(HapticMotor::kDefaultFrequencyHz, 50);
            buzzer.stop_tone();
            neopixel.set_color(0, 0, 0);
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
        neopixel.set_color(0, 10, 10);
    };

    if (http_client.consume_unread_hint()) {
        trigger_notification();
        buzzer.stop_tone();
        buzzer.disable();
        return;
    }

    if (http_client.refresh_unread_count() == ESP_OK &&
        http_client.consume_unread_hint()) {
        trigger_notification();
        buzzer.stop_tone();
        buzzer.disable();
        return;
    }

    int timeout = 3;
    for (int i = 0; i < timeout; i++) {
        JsonDocument notif_res = http_client.get_notifications();

        if (http_client.consume_unread_hint()) {
            trigger_notification();
            buzzer.stop_tone();
            buzzer.disable();
            return;
        }

        for (int idx = 0; idx < notif_res["notifications"].size(); idx++) {
            std::string notification_flag(
                notif_res["notifications"][idx]["notification_flag"]);
            if (notification_flag == "true") {
                http_client.refresh_unread_count();
                trigger_notification();
                buzzer.stop_tone();
                buzzer.disable();
                return;
            }
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    buzzer.stop_tone();
    buzzer.disable();
}

void app_main(void) {
    printf("Hello world!!!!\n");
    handle_factory_reset_if_requested();
    const bool ota_minimal_mode = (s_ota_minimal_magic == kOtaMinimalMagic);
    if (ota_minimal_mode) {
        s_ota_minimal_magic = 0;
        ESP_LOGW(TAG, "Boot in OTA-minimal mode");

        (void)nvs_flash_init();
        save_nvs((char *)"wifi_manual_off", std::string("0"));
        mqtt_rt_pause();

        WiFi wifi;
        wifi.main();
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT, pdFALSE,
                                               pdTRUE, pdMS_TO_TICKS(30000));
        if (!(bits & WIFI_CONNECTED_BIT)) {
            ESP_LOGE(TAG, "OTA-minimal mode: Wi-Fi connect timeout");
            esp_restart();
        }

        s_last_ota_progress_percent = -1;
        s_last_ota_progress_draw_us = 0;
        ota_client::set_progress_callback(ota_progress_callback, nullptr);
        draw_ota_progress_screen(0, 0, "downloading");
        esp_err_t r = ota_client::check_and_update_once();
        ota_client::set_progress_callback(nullptr, nullptr);
        ESP_LOGI(TAG, "OTA-minimal mode finished: %s", esp_err_to_name(r));
        esp_restart();
    }

    bool mem_profile_enabled = kMemoryProfilerEnabled;
    if (mem_profile_enabled) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        ESP_LOGI("MEM_PROFILE", "Memory profiling enabled");
    }
    MemoryProfiler profiler(mem_profile_enabled);

    notification_effects::init();
    joystick_haptics_init();

    // Defer OTA validation (only if rollback is enabled): mark app valid after
    // system stabilizes
    auto start_deferred_ota_validation = []() {
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
        auto task = +[](void*) {
            // Give system a moment to initialize (Wi‑Fi, heap, etc.)
            vTaskDelay(pdMS_TO_TICKS(8000));
            const esp_partition_t* running = esp_ota_get_running_partition();
            if (!running) {
                vTaskDelete(nullptr);
                return;
            }
            esp_ota_img_states_t state;
            if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
                state == ESP_OTA_IMG_PENDING_VERIFY) {
                // To avoid flash writes while Wi‑Fi is actively running heavy
                // traffic, momentarily stop Wi‑Fi (best-effort) before marking
                // valid.
                wifi_mode_t mode = WIFI_MODE_NULL;
                bool wifi_inited = (esp_wifi_get_mode(&mode) == ESP_OK);
                if (wifi_inited) {
                    (void)esp_wifi_stop();
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                ESP_LOGI(TAG, "OTA image pending verify; marking as valid");
                esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to mark app valid: %s",
                             esp_err_to_name(err));
                }
                if (wifi_inited) {
                    (void)esp_wifi_start();
                }
            }
            vTaskDelete(nullptr);
        };
        xTaskCreate(task, "ota_mark_valid", 4096, nullptr, 5, nullptr);
#else
    // Rollback disabled: no pending verify flow, nothing to do.
#endif
    };

    profiler.run_step("Deferred OTA validation setup",
                      start_deferred_ota_validation);

    auto& speaker = audio::speaker();
    // Boot sound: always play cute at startup.
    auto play_boot_sound = [&]() {
        speaker.play_tone(2700.0f, 500, 1.0f);
    };
    Oled oled;
    MenuDisplay menu;
    ProfileSetting profile_setting;

    WiFi wifi;
    profiler.run_step("Wi-Fi init", [&]() { wifi.main(); });

    profiler.run_step("Initialize SNTP", []() { initialize_sntp(); });
    profiler.run_step("Start RTC task", []() { start_rtc_task(); });

    // Deep Sleep Config
    const gpio_num_t ext_wakeup_pin_0 = GPIO_NUM_3;

    printf("Enabling EXT0 wakeup on pin GPIO%d\n", ext_wakeup_pin_0);
    esp_sleep_enable_ext0_wakeup(ext_wakeup_pin_0, 1);

    rtc_gpio_pullup_dis(ext_wakeup_pin_0);
    rtc_gpio_pulldown_en(ext_wakeup_pin_0);

    // 次のDeep Sleepの設定
    const int sleep_time_sec = 30;
    esp_sleep_enable_timer_wakeup(sleep_time_sec * uS_TO_S_FACTOR);

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    auto boot_led_animation = [&]() {
        for (int i = 0; i < 50; i++) {
            neopixel.set_color(i, i, i);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        for (int i = 50; i > 0; i--) {
            neopixel.set_color(i, i, i);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
    };

    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        profiler.run_step("Boot sound", [&]() { play_boot_sound(); });
        profiler.run_step("Boot display", [&]() { oled.BootDisplay(); });
        profiler.run_step("Boot LED animation", boot_led_animation);

    } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        HttpClient& http_client = HttpClient::shared();
        esp_err_t unread_err = http_client.refresh_unread_count();
        if (unread_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to refresh unread count: %s",
                     esp_err_to_name(unread_err));
        }

        if (!http_client.has_unread_messages()) {
            printf("wake up from timer");
            EventBits_t bits =
                xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                    pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "Wi-Fi Connected");
                // check_notification();
            } else {
                ESP_LOGW(TAG, "Wi-Fi Connection Timeout");
            }
        } else {
            http_client.force_unread_hint();
        }
        esp_deep_sleep_start();
    } else {
        profiler.run_step("Boot sound", [&]() { play_boot_sound(); });
        profiler.run_step("Boot display", [&]() { oled.BootDisplay(); });
        profiler.run_step("Boot LED animation", boot_led_animation);
    }

    // Provisioning provisioning;
    // provisioning.main();

    std::string user_name = get_nvs("user_name");
    profiler.run_step("Ensure user profile", [&]() {
        if (user_name == "") {
            profile_setting.profile_setting_task();
        }
    });

    // Start OTA auto-update background if enabled
    {
        std::string auto_flag = get_nvs("ota_auto");
        if (auto_flag == "true") {
            ota_client::start_background_task();
        }
    }
    // TODO:menuから各機能の画面に遷移するように実装する
    profiler.run_step("Start menu task", [&]() { menu.start_menu_task(); });

    profiler.report_summary();

    // HttpClient http_client;
    // std::string chat_to = "hazuki";
    // http_client.get_message(chat_to);
    // http_client.post_message();

    // Start ota task.
    // Ota ota;
    // ota.main();

    // FIXME
    // for (int i = 0; i <= 10; i++) {
    //   WiFi::wifi_state_t wifi_state = wifi.get_wifi_state();
    //   printf("Wifi state:%c\n", wifi_state.state);
    //   vTaskDelay(1000 / portTICK_PERIOD_MS);
    // }

    // HttpClient http;
    // http.start_receiving_wait();

    // Notification notif;
    // notif.check_notification();

    // app_mainが終了しないように無限ループ
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    };

    for (int i = 10; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}
