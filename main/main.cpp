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

static const char* TAG = "Mobus v3.14";

#include <map>
#include <string.h>

#include <LovyanGFX.hpp>
#include <joystick.h>
#include <power_monitor.h>
#include <button.h>
#include <haptic_motor.hpp>

#include <nvs_rw.hpp>
#include <wifi.hpp>
#include <http_client.hpp>
#include "wasm_game_runtime.hpp"

extern "C" {
#include "m3_env.h"
#include "m3_exception.h"
}
#include <neopixel.hpp>
Neopixel neopixel;
#include <oled.hpp>
#include <ntp.hpp>
#include <max98357a.h>

namespace wasm_runtime {
namespace {

constexpr const char* LOG_TAG = "WASM_GAME";

bool ensure_spiffs_mounted() {
    static bool initialized = false;
    static bool mounted = false;
    if (initialized) {
        return mounted;
    }
    initialized = true;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 8,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(
            LOG_TAG,
            "SPIFFS partition label 'spiffs' not found; trying auto-detect");
        conf.partition_label = nullptr;
        err = esp_vfs_spiffs_register(&conf);
    }

    if (err != ESP_OK) {
        ESP_LOGE(LOG_TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(err));
        return false;
    }

    size_t total = 0;
    size_t used = 0;
    const char* info_label = conf.partition_label ? conf.partition_label : "";
    err = esp_spiffs_info(info_label[0] ? info_label : nullptr, &total, &used);
    if (err == ESP_OK) {
        ESP_LOGI(LOG_TAG, "SPIFFS mounted (total=%u, used=%u)",
                 static_cast<unsigned>(total), static_cast<unsigned>(used));
    } else {
        ESP_LOGW(LOG_TAG, "Failed to query SPIFFS info: %s",
                 esp_err_to_name(err));
    }

    mounted = true;
    return mounted;
}

struct HostContext {
    Joystick joystick;
    Button type_button;
    Button back_button;
    Button enter_button;

    HostContext()
        : joystick(),
          type_button(GPIO_NUM_46),
          back_button(GPIO_NUM_3),
          enter_button(GPIO_NUM_5) {
        type_button.reset_timer();
        back_button.reset_timer();
        enter_button.reset_timer();
    }
};

inline HostContext* get_context(IM3Runtime runtime) {
    return reinterpret_cast<HostContext*>(runtime->userdata);
}

m3ApiRawFunction(host_clear_screen) {
    sprite.fillRect(0, 0, 128, 64, 0);
    m3ApiSuccess();
}

m3ApiRawFunction(host_present) {
    sprite.pushSprite(&lcd, 0, 0);
    m3ApiSuccess();
}

m3ApiRawFunction(host_fill_rect) {
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, w);
    m3ApiGetArg(int32_t, h);
    m3ApiGetArg(int32_t, color);

    sprite.fillRect(x, y, w, h, static_cast<uint16_t>(color));
    m3ApiSuccess();
}

m3ApiRawFunction(host_draw_text) {
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArgMem(const uint8_t*, text_ptr);
    m3ApiGetArg(int32_t, len);
    m3ApiGetArg(int32_t, invert);

    std::string text(reinterpret_cast<const char*>(text_ptr),
                     static_cast<size_t>(len));

    sprite.setFont(&fonts::Font2);
    if (invert) {
        sprite.setTextColor(0x0000, 0xFFFF);
    } else {
        sprite.setTextColor(0xFFFF, 0x0000);
    }
    sprite.setCursor(x, y);
    sprite.print(text.c_str());
    m3ApiSuccess();
}

m3ApiRawFunction(host_draw_sprite) {
    m3ApiGetArg(int32_t, sprite_id);
    m3ApiGetArg(int32_t, frame);
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);

    const unsigned char* bitmap = nullptr;
    int width = 0;
    int height = 0;

    switch (sprite_id) {
        case 0:  // kuina (16x16)
            bitmap = (frame & 1) ? kuina_2 : kuina_1;
            width = 16;
            height = 16;
            break;
        case 1:  // mongoose (8x8)
            bitmap = (frame & 1) ? mongoose_2 : mongoose_1;
            width = 8;
            height = 8;
            break;
        case 2:  // grass (8x8)
            bitmap = grass_1;
            width = 8;
            height = 8;
            break;
        case 3:  // pineapple (16x16)
            bitmap = pineapple_1;
            width = 16;
            height = 16;
            break;
        case 4:  // mongoose facing left (16x8)
            bitmap = (frame & 1) ? mongoose_left_2 : mongoose_left_1;
            width = 16;
            height = 8;
            break;
        case 5:  // earthworm (8x8)
            bitmap = (frame & 1) ? earthworm_2 : earthworm_1;
            width = 8;
            height = 8;
            break;
        case 6:  // hart (8x8)
            bitmap = hart;
            width = 8;
            height = 8;
            break;
        default:
            break;
    }

    if (bitmap) {
        sprite.drawBitmap(x, y, bitmap, width, height, TFT_WHITE, TFT_BLACK);
    }

    m3ApiSuccess();
}

m3ApiRawFunction(host_get_input) {
    auto* ctx = get_context(runtime);

    uint32_t mask = 0;

    auto type_state = ctx->type_button.get_button_state();
    if (type_state.pushed) mask |= INPUT_ACTION;
    if (type_state.pushed) ctx->type_button.clear_button_state();

    auto enter_state = ctx->enter_button.get_button_state();
    if (enter_state.pushed) mask |= INPUT_ENTER;
    if (enter_state.pushed) ctx->enter_button.clear_button_state();

    auto back_state = ctx->back_button.get_button_state();
    if (back_state.pushed) mask |= INPUT_BACK;
    if (back_state.pushed) ctx->back_button.clear_button_state();

    auto joy_state = ctx->joystick.get_joystick_state();
    if (joy_state.pushed_left_edge) mask |= INPUT_JOY_LEFT;
    if (joy_state.pushed_right_edge) mask |= INPUT_JOY_RIGHT;
    if (joy_state.pushed_up_edge) mask |= INPUT_JOY_UP;
    if (joy_state.pushed_down_edge) mask |= INPUT_JOY_DOWN;

    m3ApiReturnType(uint32_t) m3ApiReturn(mask);
}

m3ApiRawFunction(host_random) {
    m3ApiGetArg(int32_t, max_val);
    if (max_val <= 0) {
        m3ApiReturnType(int32_t) m3ApiReturn(0);
    }
    uint32_t value = esp_random();
    int32_t result =
        static_cast<int32_t>(value % static_cast<uint32_t>(max_val));
    m3ApiReturnType(int32_t) m3ApiReturn(result);
}

m3ApiRawFunction(host_sleep) {
    m3ApiGetArg(int32_t, ms);
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
        taskYIELD();
    }
    esp_task_wdt_reset();
    m3ApiSuccess();
}

m3ApiRawFunction(host_time_ms) {
    uint32_t ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    m3ApiReturnType(uint32_t) m3ApiReturn(ms);
}

M3Result link_host_functions(IM3Module module) {
    M3Result result = m3Err_none;
    auto link = [&](const char* name, const char* sig, M3RawCall func) {
        if (result != m3Err_none) return;
        result = m3_LinkRawFunction(module, "env", name, sig, func);
        if (result == m3Err_functionLookupFailed) {
            ESP_LOGW(LOG_TAG, "Skipping unused host import %s", name);
            result = m3Err_none;
            return;
        }
        if (result != m3Err_none) {
            ESP_LOGE(LOG_TAG, "Failed to link %s: %s", name, result);
        }
    };

    link("host_clear_screen", "v()", host_clear_screen);
    link("host_present", "v()", host_present);
    link("host_fill_rect", "v(iiiii)", host_fill_rect);
    link("host_draw_text", "v(iiii i)", host_draw_text);
    link("host_draw_sprite", "v(iiii)", host_draw_sprite);
    link("host_get_input", "i()", host_get_input);
    link("host_random", "i(i)", host_random);
    link("host_sleep", "v(i)", host_sleep);
    link("host_time_ms", "i()", host_time_ms);

    return result;
}

}  // namespace

bool run_game(const char* path) {
    if (!ensure_spiffs_mounted()) {
        return false;
    }

    ESP_LOGI(LOG_TAG, "Launching Wasm game: %s", path);

    FILE* file = fopen(path, "rb");
    if (!file) {
        ESP_LOGE(LOG_TAG, "Failed to open Wasm file: %s", path);
        return false;
    }

    std::unique_ptr<FILE, decltype(&fclose)> close_file(file, &fclose);

    if (fseek(file, 0, SEEK_END) != 0) {
        ESP_LOGE(LOG_TAG, "Failed to seek Wasm file: %s", path);
        return false;
    }

    long file_size = ftell(file);
    if (file_size <= 0) {
        ESP_LOGE(LOG_TAG, "Invalid Wasm size (%ld): %s", file_size, path);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        ESP_LOGE(LOG_TAG, "Failed to rewind Wasm file: %s", path);
        return false;
    }

    size_t wasm_size = static_cast<size_t>(file_size);
    uint8_t* wasm_bytes = static_cast<uint8_t*>(
        heap_caps_malloc(wasm_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    bool used_heap_caps = true;
    if (!wasm_bytes) {
        used_heap_caps = false;
        wasm_bytes = static_cast<uint8_t*>(malloc(wasm_size));
    }
    if (!wasm_bytes) {
        ESP_LOGE(LOG_TAG, "Failed to allocate %zu bytes for Wasm module",
                 wasm_size);
        return false;
    }

    auto free_wasm = [&]() {
        if (wasm_bytes) {
            if (used_heap_caps) {
                heap_caps_free(wasm_bytes);
            } else {
                free(wasm_bytes);
            }
            wasm_bytes = nullptr;
        }
    };

    size_t read = fread(wasm_bytes, 1, wasm_size, file);
    if (read != wasm_size) {
        ESP_LOGE(LOG_TAG, "Short read when loading Wasm (%zu/%zu) from %s",
                 read, wasm_size, path);
        free_wasm();
        return false;
    }

    HostContext context;

    IM3Environment env = m3_NewEnvironment();
    if (!env) {
        ESP_LOGE(LOG_TAG, "m3_NewEnvironment failed");
        free_wasm();
        return false;
    }

    constexpr uint32_t kStackSizeBytes = 32 * 1024;
    IM3Runtime runtime = m3_NewRuntime(env, kStackSizeBytes, &context);
    if (!runtime) {
        ESP_LOGE(LOG_TAG, "m3_NewRuntime failed");
        m3_FreeEnvironment(env);
        free_wasm();
        return false;
    }

    IM3Module module = nullptr;
    M3Result result = m3_ParseModule(env, &module, wasm_bytes,
                                     static_cast<uint32_t>(wasm_size));
    if (result != m3Err_none) {
        ESP_LOGE(LOG_TAG, "m3_ParseModule failed: %s", result);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free_wasm();
        return false;
    }

    result = m3_LoadModule(runtime, module);
    if (result != m3Err_none) {
        ESP_LOGE(LOG_TAG, "m3_LoadModule failed: %s", result);
        m3_FreeModule(module);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free_wasm();
        return false;
    }

    result = link_host_functions(module);
    if (result != m3Err_none) {
        ESP_LOGE(LOG_TAG, "Failed to link host functions: %s", result);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free_wasm();
        return false;
    }

    IM3Function game_init = nullptr;
    result = m3_FindFunction(&game_init, runtime, "game_init");
    if (result != m3Err_none) {
        ESP_LOGE(LOG_TAG, "game_init not found: %s", result);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free_wasm();
        return false;
    }

    IM3Function game_update = nullptr;
    result = m3_FindFunction(&game_update, runtime, "game_update");
    if (result != m3Err_none) {
        ESP_LOGE(LOG_TAG, "game_update not found: %s", result);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free_wasm();
        return false;
    }

    result = m3_CallV(game_init);
    if (result != m3Err_none) {
        ESP_LOGE(LOG_TAG, "game_init trap: %s", result);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free_wasm();
        return false;
    }

    bool exited_normally = false;
    uint64_t last_tick_us = esp_timer_get_time();

    while (true) {
        uint64_t now_us = esp_timer_get_time();
        uint32_t dt_ms =
            static_cast<uint32_t>((now_us - last_tick_us) / 1000ULL);
        if (dt_ms == 0) {
            dt_ms = 1;
        }
        if (dt_ms > 200) {
            dt_ms = 200;
        }
        last_tick_us = now_us;

        result = m3_CallV(game_update, dt_ms);
        if (result != m3Err_none) {
            ESP_LOGE(LOG_TAG, "game_update trap: %s", result);
            break;
        }

        uint32_t exit_code = 0;
        result = m3_GetResultsV(game_update, &exit_code);
        if (result != m3Err_none) {
            ESP_LOGE(LOG_TAG, "Failed to read game_update result: %s", result);
            break;
        }

        if (exit_code != 0) {
            exited_normally = true;
            break;
        }

        taskYIELD();
        esp_task_wdt_reset();
    }

    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
    free_wasm();

    return exited_normally;
}

}  // namespace wasm_runtime

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
    HttpClient &http_client = HttpClient::shared();
    // 通知の取得
    http_client.start_notifications();

    int timeout = 3;
    for (int i = 0; i < timeout; i++) {
        JsonDocument notif_res = http_client.get_notifications();

        for (int i = 0; i < notif_res["notifications"].size(); i++) {
            std::string notification_flag(
                notif_res["notifications"][i]["notification_flag"]);
            if (notification_flag == "true") {
                printf("got notification!");

                for (int n = 0; n < 2; n++) {
                    buzzer.start_tone(2600.0f, 0.6f);
                    neopixel.set_color(0, 10, 100);
                    haptic.pulse(HapticMotor::kDefaultFrequencyHz, 50);
                    buzzer.stop_tone();
                    neopixel.set_color(0, 0, 0);
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                }
                neopixel.set_color(0, 10, 10);
                save_nvs("notif_flag", "true");
                buzzer.stop_tone();
                buzzer.disable();
                return;
            } else {
                printf("notofication not found");
                save_nvs("notif_flag", "false");
            }
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    buzzer.stop_tone();
    buzzer.disable();
}

void app_main(void) {
    printf("Hello world!!!!\n");

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

    start_deferred_ota_validation();

    auto& speaker = audio::speaker();
    // Helper to choose boot sound: cute (default) or majestic. NVS key:
    // boot_sound = "cute"|"majestic"|"random"
    auto play_boot_sound = [&]() {
        std::string sel = get_nvs((char*)"boot_sound");
        if (sel == "majestic") {
            boot_sounds::play_majestic(speaker, 0.55f);
        } else if (sel == "gb") {
            boot_sounds::play_gb(speaker, 0.9f);
        } else if (sel == "random") {
            uint64_t t = (uint64_t)esp_timer_get_time();
            uint32_t r = (uint32_t)((t ^ (t >> 7) ^ (t >> 15)) & 0x3);
            if (r == 0)
                boot_sounds::play_cute(speaker, 0.5f);
            else if (r == 1)
                boot_sounds::play_majestic(speaker, 0.55f);
            else
                boot_sounds::play_gb(speaker, 0.9f);
        } else if (sel == "song1") {
            boot_sounds::play_song(speaker, 1, 0.9f);
        } else if (sel == "song2") {
            boot_sounds::play_song(speaker, 2, 0.9f);
        } else if (sel == "song3") {
            boot_sounds::play_song(speaker, 3, 0.9f);
        } else {
            boot_sounds::play_cute(speaker, 0.5f);
        }
    };
    Oled oled;
    MenuDisplay menu;
    ProfileSetting profile_setting;

    WiFi wifi;
    wifi.main();

    initialize_sntp();
    start_rtc_task();

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
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        // 起動音（選択式）
        play_boot_sound();
        // 起動時のロゴを表示
        oled.BootDisplay();
        // LEDを光らす
        for (int i = 0; i < 50; i++) {
            neopixel.set_color(i, i, i);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        for (int i = 50; i > 0; i--) {
            neopixel.set_color(i, i, i);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }

    } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        std::string notif_flag = get_nvs("notif_flag");
        if (notif_flag == "false") {
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
        }
        esp_deep_sleep_start();
    } else {
        // 起動音（選択式）
        play_boot_sound();
        // 起動時のロゴを表示
        oled.BootDisplay();
        // LEDを光らす
        for (int i = 0; i < 50; i++) {
            neopixel.set_color(i, i, i);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        for (int i = 50; i > 0; i--) {
            neopixel.set_color(i, i, i);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
    }

    // Provisioning provisioning;
    // provisioning.main();

    std::string user_name = get_nvs("user_name");
    if (user_name == "") {
        profile_setting.profile_setting_task();
    }

    // Start OTA auto-update background if enabled
    {
        std::string auto_flag = get_nvs("ota_auto");
        if (auto_flag == "true") {
            ota_client::start_background_task();
        }
    }
    // TODO:menuから各機能の画面に遷移するように実装する
    save_nvs("notif_flag", "false");
    menu.start_menu_task();

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
