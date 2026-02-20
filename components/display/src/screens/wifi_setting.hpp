#pragma once
#include "ui/wifi/setting_mvp.hpp"

class WiFiSetting {
   public:
    static bool running_flag;
    static constexpr uint32_t kTaskStackWords = 8096;

    void start_wifi_setting_task() {
        printf("Start WiFi Setting Task...");
        if (task_handle_) {
            ESP_LOGW(TAG, "wifi_setting_task already running");
            return;
        }
        if (!allocate_internal_stack(task_stack_, kTaskStackWords,
                                     "WiFiSetting")) {
            size_t bytes = kTaskStackWords * sizeof(StackType_t);
            task_stack_ = static_cast<StackType_t *>(
                heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!task_stack_) {
                ESP_LOGE(TAG, "Failed to alloc wifi setting stack");
                running_flag = false;
                return;
            }
            ESP_LOGW(TAG, "[OLED] stack alloc in PSRAM (WiFiSetting, bytes=%u)",
                     static_cast<unsigned>(bytes));
        }
        task_handle_ = xTaskCreateStaticPinnedToCore(
            &wifi_setting_task, "wifi_setting_task", kTaskStackWords, NULL, 6,
            task_stack_, &task_buffer_, 1);
        if (!task_handle_) {
            ESP_LOGE(TAG, "Failed to start wifi_setting_task (free_heap=%u)",
                     static_cast<unsigned>(heap_caps_get_free_size(
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
            running_flag = false;
        } else {
            running_flag = true;
        }
    }

    static void preload_scan_cache() {
        WiFi &wifi = WiFi::shared();
        uint16_t count = DEFAULT_SCAN_LIST_SIZE;
        wifi_ap_record_t tmp[DEFAULT_SCAN_LIST_SIZE] = {};
        wifi.wifi_scan(&count, tmp);
        if (count > DEFAULT_SCAN_LIST_SIZE) {
            count = DEFAULT_SCAN_LIST_SIZE;
        }
        for (uint16_t i = 0; i < count; ++i) {
            tmp[i].ssid[32] = '\0';
        }
        scan_cache_count_ = count;
        memset(scan_cache_, 0, sizeof(scan_cache_));
        if (count > 0) {
            memcpy(scan_cache_, tmp, sizeof(wifi_ap_record_t) * count);
        }
        scan_cache_valid_ = true;
        ESP_LOGI(TAG, "Wi-Fi scan cache preloaded: %u entries", count);
    }

    static std::string char_to_string_ssid(uint8_t *uint_ssid) {
        if (uint_ssid == nullptr) return std::string("");
        char char_ssid[33] = {0};
        const char *src = reinterpret_cast<const char *>(uint_ssid);
        const size_t len = strnlen(src, sizeof(char_ssid) - 1);
        memcpy(char_ssid, src, len);
        char_ssid[len] = '\0';
        return std::string(char_ssid);
    }

    static std::string get_omitted_ssid(uint8_t *uint_ssid) {
        if (uint_ssid == 0) {
            return "";
        }

        std::string ssid = char_to_string_ssid(uint_ssid);

        // SSIDが12文字以内に収まるように加工
        if (ssid.length() >= 12) {
            return ssid.substr(0, 6) + "..." + ssid.substr(ssid.length() - 3);
        }
        return ssid;
    }

    static std::string input_info(std::string input_type = "SSID",
                                  std::string type_text = "") {
        ui::wifi::TextInputViewState view_state;
        view_state.title = input_type;
        view_state.text = type_text;
        ui::wifi::TextInputPresenter presenter(view_state);
        ui::wifi::TextInputRenderer renderer;

        // 文字列の配列を作成
        char char_set[8][35];

        sprintf(char_set[0], "0123456789");
        sprintf(char_set[1], "abcdefghijklm");
        sprintf(char_set[2], "nopqrstuvwxyz");
        sprintf(char_set[3], "ABCDEFGHIJKLM");
        sprintf(char_set[4], "NOPQRSTUVWXYZ");
        sprintf(char_set[5], "!\"#$%%&\\'()");
        sprintf(char_set[6], "-_./:;<=>?@[]");
        sprintf(char_set[7], "*+,`{|}~^");

        // int font_ = 13; // unused
        // int margin = 3; // unused

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        const ui::wifi::TextInputRenderApi render_api{
            .begin_frame =
                [&]() {
                    sprite.fillRect(0, 0, 128, 64, 0);
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                },
            .draw_title =
                [&](const std::string &title) {
                    sprite.setCursor(0, 0);
                    sprite.print(title.c_str());
                    sprite.drawFastHLine(0, 14, 128, 0xFFFF);
                    sprite.drawFastHLine(0, 45, 128, 0xFFFF);
                },
            .draw_value =
                [&](const std::string &value) {
                    sprite.setCursor(0, 15);
                    sprite.print(value.c_str());
                },
            .draw_selector =
                [&](int select_x, int select_y) {
                    draw_char_selector_row(char_set[select_y], 46, select_x, true);
                },
            .present = [&]() { push_sprite_safe(0, 0); }};

        while (1) {
            Joystick::joystick_state_t joystick_state =
                joystick.get_joystick_state();
            Button::button_state_t type_button_state =
                type_button.get_button_state();
            Button::button_state_t back_button_state =
                back_button.get_button_state();
            Button::button_state_t enter_button_state =
                enter_button.get_button_state();

            ui::InputSnapshot input{};
            input.left_edge = joystick_state.pushed_left_edge;
            input.right_edge = joystick_state.pushed_right_edge;
            input.up_edge = joystick_state.pushed_up_edge;
            input.down_edge = joystick_state.pushed_down_edge;
            input.type_pressed = type_button_state.pushed;
            input.back_pressed = back_button_state.pushed;
            input.enter_pressed = enter_button_state.pushed;

            const auto cmd = presenter.handle_input(input);
            if (cmd == ui::wifi::TextInputCommand::Cancel ||
                cmd == ui::wifi::TextInputCommand::Finish) {
                break;
            } else if (cmd == ui::wifi::TextInputCommand::Type) {
                presenter.normalize_row_count(
                    static_cast<int>(sizeof(char_set) / sizeof(char_set[0])));
                const char *row_chars = char_set[view_state.select_y];
                presenter.normalize_col_count(
                    static_cast<int>(strlen(row_chars)) + 1);
                (void)apply_char_or_delete(view_state.text, row_chars,
                                           view_state.select_x,
                                           0, true);
                type_button.clear_button_state();
                type_button.reset_timer();
            }

            // 文字種のスクロールの設定
            presenter.normalize_row_count(
                static_cast<int>(sizeof(char_set) / sizeof(char_set[0])));
            (void)wrap_char_index(view_state.select_x,
                                  char_set[view_state.select_y], true);
            renderer.render(view_state, render_api);
            // Avoid starving the watchdog while waiting for input
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        return view_state.text;
    }

    static void set_wifi_info(uint8_t *ssid = 0) {
        WiFi &wifi = WiFi::shared();

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        ui::wifi::WifiMenuViewState menu_state;
        menu_state.selected = 0;
        menu_state.max_index = 2;
        ui::wifi::WifiMenuPresenter menu_presenter(menu_state);
        int font_height = 13;
        int margin = 3;
        std::string input_ssid = char_to_string_ssid(ssid);
        std::string input_pass = "";

        while (1) {
            sprite.fillRect(0, 0, 128, 64, 0);

            Joystick::joystick_state_t joystick_state =
                joystick.get_joystick_state();
            Button::button_state_t type_button_state =
                type_button.get_button_state();
            Button::button_state_t back_button_state =
                back_button.get_button_state();

            ui::InputSnapshot menu_input{};
            menu_input.left_edge = joystick_state.left;
            menu_input.up_edge = joystick_state.pushed_up_edge;
            menu_input.down_edge = joystick_state.pushed_down_edge;
            menu_input.type_pressed = type_button_state.pushed;
            menu_input.back_pressed = back_button_state.pushed;
            const auto menu_cmd = menu_presenter.handle_input(menu_input);
            if (menu_cmd == ui::wifi::WifiMenuCommand::Exit) {
                break;
            }

            sprite.setCursor(0, 0);
            if (menu_state.selected == 0) {
                sprite.fillRect(0, (font_height + margin) * menu_state.selected, 128,
                                font_height + 3, 0xFFFF);
                sprite.setTextColor(0x000000u, 0xFFFFFFu);
            } else {
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
            }
            std::string disp_ssid = "SSID: " + get_omitted_ssid(ssid);
            sprite.print(disp_ssid.c_str());

            sprite.setCursor(0, font_height + margin);
            if (menu_state.selected == 1) {
                sprite.fillRect(0, (font_height + margin) * menu_state.selected, 128,
                                font_height + 3, 0xFFFF);
                sprite.setTextColor(0x000000u, 0xFFFFFFu);
            } else {
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
            }
            sprite.print("PASSWORD: ****");

            sprite.setCursor(35, 40);
            if (menu_state.selected == 2) {
                sprite.setTextColor(0x000000u, 0xFFFFFFu);
            } else {
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
            }
            sprite.print("CONNECT");

            push_sprite_safe(0, 0);

            // 個別の情報入力画面へ遷移
            if (menu_cmd == ui::wifi::WifiMenuCommand::Select) {
                type_button.clear_button_state();
                type_button.reset_timer();
                back_button.clear_button_state();
                back_button.reset_timer();
                joystick.reset_timer();

                if (menu_state.selected == 0) {
                    input_ssid = input_info("SSID", char_to_string_ssid(ssid));
                } else if (menu_state.selected == 1) {
                    input_pass = input_info("PASSWORD", input_pass);
                } else if (menu_state.selected == 2) {
                    wifi.wifi_set_sta(input_ssid, input_pass);
                    // wifi.wifi_set_sta("elecom-3e6943_24","7ku65wjwx8fv");
                    sprite.fillRect(0, 0, 128, 64, 0);
                    sprite.setFont(&fonts::Font2);
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    sprite.drawCenterString("Connecting...", 64, 22);
                    push_sprite_safe(0, 0);

                    EventBits_t bits = xEventGroupWaitBits(
                        s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                        pdMS_TO_TICKS(10000));

                    if (bits & WIFI_CONNECTED_BIT) {
                        // Save credentials to NVS (max 5)
                        save_wifi_credential(input_ssid, input_pass);
                        sprite.fillRect(0, 0, 128, 64, 0);
                        sprite.setFont(&fonts::Font2);
                        sprite.drawCenterString("Connected!", 64, 22);
                        push_sprite_safe(0, 0);
                        vTaskDelay(2000 / portTICK_PERIOD_MS);
                        return;
                    } else {
                        sprite.fillRect(0, 0, 128, 64, 0);
                        sprite.setFont(&fonts::Font2);
                        sprite.drawCenterString("Connection Failed!", 64, 22);
                        push_sprite_safe(0, 0);
                        vTaskDelay(2000 / portTICK_PERIOD_MS);
                        ESP_LOGW(TAG, "Wi-Fi Connection Timeout");
                    }
                }

                type_button.clear_button_state();
                type_button.reset_timer();
                back_button.clear_button_state();
                back_button.reset_timer();
                joystick.reset_timer();
            }

            // チャタリング防止用に100msのsleep
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    static void wifi_setting_task(void *pvParameters) {
        run_wifi_setting_flow(false);

        running_flag = false;
        task_handle_ = nullptr;
        vTaskDelete(NULL);
    }

    static void run_wifi_setting_flow(bool auto_exit_on_connected = false) {
        if (!ensure_sprite_surface(128, 64, 8,
                                   "WiFiSetting::run_wifi_setting_flow")) {
            ESP_LOGE(TAG, "Wi-Fi setting flow aborted: sprite unavailable");
            return;
        }
        sprite.setFont(&fonts::Font2);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止

        sprite.fillRect(0, 0, 128, 64, 0);
        sprite.setCursor(30, 20);
        sprite.print("Scanning...");
        push_sprite_safe(0, 0);

        auto is_connected_bit_set = []() -> bool {
            if (!s_wifi_event_group) return false;
            EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
            return (bits & WIFI_CONNECTED_BIT) != 0;
        };
        if (auto_exit_on_connected && is_connected_bit_set()) {
            ESP_LOGI(TAG, "Wi-Fi already connected; skip scan in auto-exit flow");
            return;
        }

        WiFi &wifi = WiFi::shared();

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        auto is_wifi_manual_off = []() -> bool {
            return get_nvs((char *)"wifi_manual_off") == std::string("1");
        };
        auto set_wifi_manual_off = [](bool off) {
            save_nvs((char *)"wifi_manual_off", off ? "1" : "0");
        };

        uint16_t ssid_n = 0;
        wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE] = {};
        ssid_n = DEFAULT_SCAN_LIST_SIZE;
        wifi.wifi_scan(&ssid_n, ap_info);
        if (ssid_n > DEFAULT_SCAN_LIST_SIZE) {
            ssid_n = DEFAULT_SCAN_LIST_SIZE;
        }
        for (uint16_t i = 0; i < ssid_n; ++i) {
            ap_info[i].ssid[32] = '\0';
        }

        // Prefer fresh scan results; fall back to cache only on scan failure/empty.
        if (ssid_n == 0 && scan_cache_valid_ && scan_cache_count_ > 0) {
            uint16_t cached_n = scan_cache_count_;
            if (cached_n > DEFAULT_SCAN_LIST_SIZE) {
                cached_n = DEFAULT_SCAN_LIST_SIZE;
            }
            memcpy(ap_info, scan_cache_, sizeof(wifi_ap_record_t) * cached_n);
            for (uint16_t i = 0; i < cached_n; ++i) {
                ap_info[i].ssid[32] = '\0';
            }
            ssid_n = cached_n;
            ESP_LOGW(TAG, "Wi-Fi scan returned 0; using cache (%u entries)", ssid_n);
        } else {
            scan_cache_count_ = ssid_n;
            memset(scan_cache_, 0, sizeof(scan_cache_));
            if (ssid_n > 0) {
                memcpy(scan_cache_, ap_info, sizeof(wifi_ap_record_t) * ssid_n);
            }
            scan_cache_valid_ = true;
        }
        if (ssid_n == 0) {
            ESP_LOGW(TAG, "Wi-Fi scan returned 0 APs; use manual input (Other)");
        }

        ui::wifi::WifiMenuViewState menu_state;
        menu_state.selected = 0;
        int font_height = 13;
        int margin = 3;

        while (true) {
            if (auto_exit_on_connected && is_connected_bit_set()) {
                return;
            }
            sprite.fillRect(0, 0, 128, 64, 0);

            Joystick::joystick_state_t joystick_state =
                joystick.get_joystick_state();
            Button::button_state_t type_button_state =
                type_button.get_button_state();
            Button::button_state_t back_button_state =
                back_button.get_button_state();

            int max_index =
                ssid_n + 1;  // 0: Wi-Fi toggle, 1..ssid_n: SSIDs, last: Other
            menu_state.max_index = max_index;
            ui::wifi::WifiMenuPresenter menu_presenter(menu_state);
            ui::InputSnapshot menu_input{};
            menu_input.left_edge = joystick_state.left;
            menu_input.up_edge = joystick_state.pushed_up_edge;
            menu_input.down_edge = joystick_state.pushed_down_edge;
            menu_input.type_pressed = type_button_state.pushed;
            menu_input.back_pressed = back_button_state.pushed;
            const auto menu_cmd = menu_presenter.handle_input(menu_input);
            if (menu_cmd == ui::wifi::WifiMenuCommand::Exit) break;

            if (type_button_state.pushed) {
                sprite.setFont(&fonts::Font2);
                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
            }

            for (int i = 0; i <= ssid_n + 1; i++) {
                sprite.setCursor(10, (font_height + margin) * i);

                if (i > 0 && i <= ssid_n) {
                    int idx = i - 1;
                    (void)idx;
                }

                if (i == menu_state.selected) {
                    sprite.setTextColor(0x000000u, 0xFFFFFFu);
                    sprite.fillRect(0, (font_height + margin) * menu_state.selected,
                                    128, font_height + 3, 0xFFFF);
                } else {
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                }

                if (i == 0) {
                    std::string disp_wifi =
                        std::string("Wi-Fi: ") +
                        (is_wifi_manual_off() ? "OFF" : "ON");
                    sprite.print(disp_wifi.c_str());
                } else if (ssid_n + 1 == i) {
                    // 手動入力のためのOtherを表示
                    std::string disp_ssid = "Other";
                    sprite.print(disp_ssid.c_str());
                } else {
                    // スキャンの結果取得できたSSIDを表示
                    int idx = i - 1;
                    sprite.print(get_omitted_ssid(ap_info[idx].ssid).c_str());
                }
            }

            push_sprite_safe(0, 0);

            // 個別のWiFi設定画面へ遷移
            if (menu_cmd == ui::wifi::WifiMenuCommand::Select) {
                if (menu_state.selected == 0) {
                    bool off = is_wifi_manual_off();
                    if (off) {
                        set_wifi_manual_off(false);
                        esp_err_t err = esp_wifi_start();
                        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN &&
                            err != ESP_ERR_WIFI_NOT_STOPPED) {
                            ESP_LOGW(TAG, "esp_wifi_start returned %s",
                                     esp_err_to_name(err));
                        }
                        err = esp_wifi_connect();
                        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
                            ESP_LOGW(TAG, "esp_wifi_connect returned %s",
                                     esp_err_to_name(err));
                        }
                    } else {
                        set_wifi_manual_off(true);
                        esp_err_t err = esp_wifi_disconnect();
                        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED &&
                            err != ESP_ERR_WIFI_CONN) {
                            ESP_LOGW(TAG, "esp_wifi_disconnect returned %s",
                                     esp_err_to_name(err));
                        }
                        err = esp_wifi_stop();
                        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
                            ESP_LOGW(TAG, "esp_wifi_stop returned %s",
                                     esp_err_to_name(err));
                        }
                    }
                } else if (ssid_n + 1 == menu_state.selected) {
                    set_wifi_info();
                } else {
                    int idx = menu_state.selected - 1;
                    set_wifi_info(ap_info[idx].ssid);
                }
                if (auto_exit_on_connected && s_wifi_event_group) {
                    EventBits_t bits = xEventGroupWaitBits(
                        s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                        pdMS_TO_TICKS(1500));
                    if (bits & WIFI_CONNECTED_BIT) {
                        return;
                    }
                }
                type_button.clear_button_state();
                type_button.reset_timer();
                back_button.clear_button_state();
                back_button.reset_timer();
                joystick.reset_timer();
            }

            // チャタリング防止用に100msのsleep
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    };

   private:
    static TaskHandle_t task_handle_;
    static StaticTask_t task_buffer_;
    static StackType_t *task_stack_;
    static bool scan_cache_valid_;
    static uint16_t scan_cache_count_;
    static wifi_ap_record_t scan_cache_[DEFAULT_SCAN_LIST_SIZE];
};
bool WiFiSetting::running_flag = false;
TaskHandle_t WiFiSetting::task_handle_ = nullptr;
StaticTask_t WiFiSetting::task_buffer_;
StackType_t *WiFiSetting::task_stack_ = nullptr;
bool WiFiSetting::scan_cache_valid_ = false;
uint16_t WiFiSetting::scan_cache_count_ = 0;
wifi_ap_record_t WiFiSetting::scan_cache_[DEFAULT_SCAN_LIST_SIZE] = {};

// Define proxy after WiFiSetting is fully defined
inline std::string wifi_input_info_proxy(std::string input_type,
                                         std::string type_text) {
    return WiFiSetting::input_info(input_type, type_text);
}
