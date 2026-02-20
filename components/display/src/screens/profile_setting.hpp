#pragma once

#include "../runtime/prelude.hpp"
#include "wifi_setting.hpp"

class ProfileSetting {
   public:
    static constexpr uint32_t kProfileTaskStackWords = 16384;
    static constexpr uint32_t kAuthTaskStackWords = 16384;
    struct AuthTaskArgs {
        bool signup = false;
        std::string login_id;
        std::string password;
        std::string nickname;
        TaskHandle_t waiter = nullptr;
        esp_err_t *out_err = nullptr;
    };

    static std::string input_field(
        const std::string &header_top, const std::string &header_bottom,
        const std::string &label, std::string type_text = "",
        size_t max_len = 32, bool *canceled = nullptr,
        std::function<void(const std::string &, std::string &)> on_change =
            nullptr,
        std::function<bool(const std::string &, std::string &, std::string &)>
            on_enter_validate = nullptr,
        bool enable_delete_button = false) {
        int select_x_index = 0;
        int select_y_index = 0;
        std::string status_text;

        auto show_dialog = [&](const std::string &line1,
                               const std::string &line2) {
            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            if (!line1.empty()) sprite.drawCenterString(line1.c_str(), 64, 18);
            if (!line2.empty()) sprite.drawCenterString(line2.c_str(), 64, 34);
            push_sprite_safe(0, 0);
            vTaskDelay(900 / portTICK_PERIOD_MS);
        };

        // 文字列の配列を作成
        char char_set[7][35];

        sprintf(char_set[0], "0123456789");
        sprintf(char_set[1], "abcdefghijklmn");
        sprintf(char_set[2], "opqrstuvwxyz");
        sprintf(char_set[3], "ABCDEFGHIJKLMN");
        sprintf(char_set[4], "OPQRSTUVWXYZ");
        sprintf(char_set[5], "!\"#$%%&\\'()*+,");
        sprintf(char_set[6], "-./:;<=>?@[]^_`{|}~");

        int font_ = 13;
        int margin = 3;

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        if (max_len > 0 && type_text.capacity() < max_len) {
            type_text.reserve(max_len);
        }
        if (canceled) *canceled = false;
        if (on_change && !type_text.empty()) {
            on_change(type_text, status_text);
        }

        while (1) {
            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);

            if (!header_top.empty()) {
                sprite.drawCenterString(header_top.c_str(), 64, 0);
            }
            if (!header_bottom.empty()) {
                sprite.drawCenterString(header_bottom.c_str(), 64, 15);
            }

            Joystick::joystick_state_t joystick_state =
                joystick.get_joystick_state();
            Button::button_state_t type_button_state =
                type_button.get_button_state();
            Button::button_state_t back_button_state =
                back_button.get_button_state();
            Button::button_state_t enter_button_state =
                enter_button.get_button_state();

            // 入力イベント
            if (back_button_state.pushed) {
                if (canceled) *canceled = true;
                break;
            } else if (enter_button_state.pushed) {
                enter_button.clear_button_state();
                enter_button.reset_timer();
                if (on_enter_validate) {
                    std::string line1;
                    std::string line2;
                    if (!on_enter_validate(type_text, line1, line2)) {
                        show_dialog(line1, line2);
                        continue;
                    }
                }
                break;
            } else if (joystick_state.pushed_left_edge) {
                select_x_index -= 1;
            } else if (joystick_state.pushed_right_edge) {
                select_x_index += 1;
            } else if (joystick_state.pushed_up_edge) {
                select_y_index -= 1;
            } else if (joystick_state.pushed_down_edge) {
                select_y_index += 1;
            } else if (type_button_state.pushed) {
                int char_set_length = sizeof(char_set) / sizeof(char_set[0]);
                wrap_char_set_index(select_y_index, char_set_length);
                (void)wrap_char_index(select_x_index, char_set[select_y_index],
                                      enable_delete_button);
                if (apply_char_or_delete(type_text, char_set[select_y_index],
                                         select_x_index, max_len,
                                         enable_delete_button)) {
                    if (on_change) on_change(type_text, status_text);
                }
                type_button.clear_button_state();
                type_button.reset_timer();
            }

            // 文字種のスクロールの設定
            int char_set_length = sizeof(char_set) / sizeof(char_set[0]);
            wrap_char_set_index(select_y_index, char_set_length);
            (void)wrap_char_index(select_x_index, char_set[select_y_index],
                                  enable_delete_button);
            draw_char_selector_row(char_set[select_y_index], 46, select_x_index,
                                   enable_delete_button);

            // 入力された文字の表示
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            sprite.setCursor(0, 30);
            if (!label.empty()) {
                sprite.print(label.c_str());
            }
            sprite.print(type_text.c_str());
            sprite.drawFastHLine(0, 45, 128, 0xFFFF);
            if (!status_text.empty()) {
                sprite.drawCenterString(status_text.c_str(), 64, 24);
            }

            push_sprite_safe(0, 0);
            // Feed watchdog / yield to scheduler
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        return type_text;
    }

    static std::string input_info(std::string type_text = "") {
        return input_field("HI, DE Mimoc.", "YOU ARE?", "Name: ", type_text, 20,
                           nullptr, nullptr, nullptr, true);
    }

    static std::string set_profile_info(uint8_t *ssid = 0) {
        std::string user_name = "";

        user_name = input_info();
        return user_name;
    }

    static void morse_greeting(const std::string &text,
                               const std::string &header = "", int cx = 64,
                               int cy = 32) {
        play_morse_message(text, header, cx, cy);
    }

    static bool select_auth_mode(bool &signup, ui::Lang lang) {
        const int selected = run_choice_dialog(
            ui::text(ui::Key::TitleAccount, lang),
            ui::text(ui::Key::TitleChoose, lang),
            {ui::text(ui::Key::ActionLogin, lang),
             ui::text(ui::Key::ActionSignup, lang)},
            0, lang);
        if (selected < 0) return false;
        signup = (selected == 1);
        return true;
    }

    static void profile_setting_task() {
        if (profile_task_handle_) return;
        if (!allocate_internal_stack(profile_task_stack_,
                                     kProfileTaskStackWords,
                                     "ProfileSetting")) {
            return;
        }
        TaskHandle_t waiter = xTaskGetCurrentTaskHandle();
        profile_task_handle_ = xTaskCreateStaticPinnedToCore(
            &profile_task_entry, "profile_setting_task", kProfileTaskStackWords,
            waiter, 5, profile_task_stack_, &profile_task_buffer_, 1);
        if (!profile_task_handle_) {
            return;
        }
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

   private:
    static StackType_t *profile_task_stack_;
    static StaticTask_t profile_task_buffer_;
    static TaskHandle_t profile_task_handle_;

    static void profile_task_entry(void *pvParameters) {
        TaskHandle_t waiter = static_cast<TaskHandle_t>(pvParameters);
        profile_setting_task_impl();
        UBaseType_t watermark_words = uxTaskGetStackHighWaterMark(nullptr);
        ESP_LOGI(TAG, "[Profile] stack watermark: %u words (%u bytes)",
                 static_cast<unsigned>(watermark_words),
                 static_cast<unsigned>(watermark_words * sizeof(StackType_t)));
        if (waiter) xTaskNotifyGive(waiter);
        profile_task_handle_ = nullptr;
        vTaskDelete(NULL);
    }

    static void profile_setting_task_impl() {
        auto show_message = [&](const char *line1, const char *line2,
                                int delay_ms, ui::Lang lang) {
            const lgfx::IFont *font =
                (lang == ui::Lang::Ja)
                    ? static_cast<const lgfx::IFont *>(
                          &mobus_fonts::MisakiGothic8())
                    : static_cast<const lgfx::IFont *>(&fonts::Font2);
            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.setFont(font);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            if (line1) sprite.drawCenterString(line1, 64, 18);
            if (line2) sprite.drawCenterString(line2, 64, 34);
            push_sprite_safe(0, 0);
            vTaskDelay(delay_ms / portTICK_PERIOD_MS);
        };

        ui::Lang lang = ui::current_lang();
        while (1) {
            lang = prompt_language();
            if (ensure_wifi_connected(lang)) break;
        }

        // Ask login or signup, then attempt auth; only persist on success
        while (true) {
            bool signup = false;
            if (!select_auth_mode(signup, lang)) {
                if (!ensure_wifi_connected(lang)) {
                    lang = prompt_language();
                }
                continue;
            }

            const char *title = signup ? "SIGN UP" : "LOGIN";
            bool canceled = false;
            auto &api = chatapi::shared_client(true);
            const char *status_used =
                (lang == ui::Lang::Ja) ? "使用中" : "In use";
            const char *status_err =
                (lang == ui::Lang::Ja) ? "確認失敗" : "Check failed";
            std::function<bool(const std::string &, std::string &,
                               std::string &)>
                login_id_validator = [&](const std::string &text,
                                         std::string &line1,
                                         std::string &line2) {
                    if (text.empty()) {
                        line1 = "Login ID required";
                        line2 = "Try again";
                        return false;
                    }
                    if (text.size() < 3) {
                        line1 = "Login ID min 3";
                        line2 = "Try again";
                        return false;
                    }
                    if (signup) {
                        bool available = false;
                        esp_err_t err =
                            api.login_id_available(text, &available);
                        if (err != ESP_OK) {
                            line1 = status_err;
                            line2 = "Try again";
                            return false;
                        }
                        if (!available) {
                            line1 = status_used;
                            line2 = "Try again";
                            return false;
                        }
                    }
                    return true;
                };

            std::string login_id = input_field(
                title, "Login ID", "ID: ", get_nvs((char *)"login_id"), 24,
                &canceled, nullptr, login_id_validator);
            if (canceled) {
                continue;
            }

            std::string password = input_field(
                title, "Password", "PW: ", "", 32, &canceled, nullptr,
                [&](const std::string &text, std::string &line1,
                    std::string &line2) {
                    if (signup && text.size() < 6) {
                        line1 = "Password min 6";
                        line2 = "Try again";
                        return false;
                    }
                    return true;
                });
            if (canceled) {
                continue;
            }
            if (password.empty()) {
                password = get_nvs("password");
                if (password.empty()) {
                    password = "password123";
                }
            }
            std::string nickname;
            if (signup) {
                nickname =
                    input_field(title, "Nickname", "Name: ", "", 20, &canceled);
                if (canceled) {
                    continue;
                }
                if (nickname.empty()) nickname = login_id;
            }

            esp_err_t err =
                run_auth_request(signup, login_id, password, nickname);
            if (err == ESP_OK) {
                save_nvs("login_id", login_id);
                if (signup) {
                    save_nvs("user_name", nickname);
                } else if (get_nvs("user_name").empty()) {
                    save_nvs("user_name", login_id);
                }
                if (!password.empty()) {
                    save_nvs((char *)"password", password);
                }
                show_message(ui::text(ui::Key::GreetingLine1, lang),
                             ui::text(ui::Key::GreetingLine2, lang), 1200,
                             lang);
                break;
            }

            show_message("Auth Failed", "Check creds & net", 1200, lang);
        }
    }

    static StackType_t *auth_task_stack_;
    static StaticTask_t auth_task_buffer_;
    static TaskHandle_t auth_task_handle_;
    static esp_err_t auth_task_result_;

    static void auth_task_entry(void *pvParameters) {
        std::unique_ptr<AuthTaskArgs> args(
            static_cast<AuthTaskArgs *>(pvParameters));
        ESP_LOGI(TAG, "[Auth] heap pre login: free=%u largest=%u",
                 static_cast<unsigned>(heap_caps_get_free_size(
                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(
                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        auto &api = chatapi::shared_client(true);
        api.set_scheme("https");
        esp_err_t result = ESP_FAIL;
        if (args->signup) {
            result = api.register_user(args->login_id, args->nickname,
                                       args->password);
        } else {
            result = api.login(args->login_id, args->password);
        }
        if (args->out_err) {
            *(args->out_err) = result;
        }
        UBaseType_t watermark_words = uxTaskGetStackHighWaterMark(nullptr);
        ESP_LOGI(TAG, "[Auth] stack watermark: %u words (%u bytes)",
                 static_cast<unsigned>(watermark_words),
                 static_cast<unsigned>(watermark_words * sizeof(StackType_t)));
        if (args->waiter) {
            xTaskNotifyGive(args->waiter);
        }
        auth_task_handle_ = nullptr;
        vTaskDelete(NULL);
    }

    static esp_err_t run_auth_request(bool signup, const std::string &login_id,
                                      const std::string &password,
                                      const std::string &nickname) {
        if (auth_task_handle_) return ESP_ERR_INVALID_STATE;
        if (!allocate_internal_stack(auth_task_stack_, kAuthTaskStackWords,
                                     "Auth")) {
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "[Auth] heap before task: free=%u largest=%u",
                 static_cast<unsigned>(heap_caps_get_free_size(
                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(
                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));

        auto *args = new AuthTaskArgs();
        args->signup = signup;
        args->login_id = login_id;
        args->password = password;
        args->nickname = nickname;
        args->waiter = xTaskGetCurrentTaskHandle();
        auth_task_result_ = ESP_FAIL;
        args->out_err = &auth_task_result_;

        auth_task_handle_ = xTaskCreateStaticPinnedToCore(
            &auth_task_entry, "auth_task", kAuthTaskStackWords, args, 5,
            auth_task_stack_, &auth_task_buffer_, 1);
        if (!auth_task_handle_) {
            delete args;
            return ESP_FAIL;
        }

        uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20000));
        if (notified == 0) {
            return ESP_ERR_TIMEOUT;
        }
        return auth_task_result_;
    }

    static ui::Lang prompt_language() {
        const ui::Lang current = ui::current_lang();
        const int initial = (current == ui::Lang::Ja) ? 1 : 0;
        while (1) {
            const int selected = run_choice_dialog(
                ui::text(ui::Key::TitleLanguage, current), "",
                {ui::text(ui::Key::LangEnglish, current),
                 ui::text(ui::Key::LangJapanese, current)},
                initial, current);
            if (selected < 0) continue;
            const ui::Lang chosen = (selected == 1) ? ui::Lang::Ja : ui::Lang::En;
            if (confirm_language(chosen)) {
                save_nvs("ui_lang", chosen == ui::Lang::Ja ? "ja" : "en");
                return chosen;
            }
        }
    }

    static bool confirm_language(ui::Lang lang) {
        const int selected = run_choice_dialog(
            ui::text(ui::Key::TitleLanguageConfirm, lang), "",
            {ui::text(ui::Key::LabelConfirm, lang),
             ui::text(ui::Key::LabelBack, lang)},
            0, lang);
        return selected == 0;
    }

    static bool ensure_wifi_connected(ui::Lang lang) {
        auto wait_wifi_connected = [](uint32_t timeout_ms) -> bool {
            if (!s_wifi_event_group) return false;
            EventBits_t bits =
                xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                    pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
            return (bits & WIFI_CONNECTED_BIT) != 0;
        };

        while (1) {
            if (wait_wifi_connected(0)) return true;

            WiFiSetting::running_flag = true;
            WiFiSetting::run_wifi_setting_flow(true);
            WiFiSetting::running_flag = false;

            // Give IP event handler a short window to set event bits after UI
            // flow returns.
            if (wait_wifi_connected(1500)) return true;

            if (!prompt_wifi_retry_or_back(lang)) {
                return false;
            }
        }
    }

    static bool prompt_wifi_retry_or_back(ui::Lang lang) {
        const int selected = run_choice_dialog(
            ui::text(ui::Key::TitleWifiSetup, lang), "",
            {ui::text(ui::Key::LabelRetry, lang),
             ui::text(ui::Key::LabelBack, lang)},
            0, lang);
        return selected == 0;
    }

    static const lgfx::IFont* dialog_font_for_lang(ui::Lang lang) {
        if (lang == ui::Lang::Ja) {
            return static_cast<const lgfx::IFont*>(&mobus_fonts::MisakiGothic8());
        }
        return static_cast<const lgfx::IFont*>(&fonts::Font2);
    }

    static int run_choice_dialog(const std::string& title_top,
                                 const std::string& title_bottom,
                                 const std::vector<std::string>& options,
                                 int initial_selected, ui::Lang lang) {
        if (options.empty()) return -1;

        Joystick joystick;
        Button type_button(GPIO_NUM_46);
        Button enter_button(GPIO_NUM_5);
        Button back_button(GPIO_NUM_3);
        type_button.clear_button_state();
        type_button.reset_timer();
        enter_button.clear_button_state();
        enter_button.reset_timer();
        back_button.clear_button_state();
        back_button.reset_timer();
        joystick.reset_timer();
        vTaskDelay(50 / portTICK_PERIOD_MS);

        ui::choice::ViewState state;
        state.title_top = title_top;
        state.title_bottom = title_bottom;
        state.options = options;
        state.selected = initial_selected;
        ui::choice::Presenter presenter(state);
        ui::choice::Renderer renderer;
        ui::choice::RenderApi render_api{
            .begin_frame = [&]() { sprite.fillRect(0, 0, 128, 64, 0); },
            .draw_title =
                [&](const std::string& top, const std::string& bottom) {
                    sprite.setFont(dialog_font_for_lang(lang));
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    if (!top.empty()) {
                        sprite.drawCenterString(top.c_str(), 64, 6);
                    }
                    if (!bottom.empty()) {
                        sprite.drawCenterString(bottom.c_str(), 64, 18);
                    }
                },
            .draw_option =
                [&](int i, const std::string& text, bool selected) {
                    const int y_base = title_bottom.empty() ? 24 : 32;
                    const int y_step = 16;
                    const int y = y_base + i * y_step;
                    if (selected) {
                        sprite.fillRect(8, y - 2, 112, 14, 0xFFFF);
                        sprite.setTextColor(0x000000u, 0xFFFFFFu);
                    } else {
                        sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    }
                    sprite.drawCenterString(text.c_str(), 64, y);
                },
            .present = [&]() { push_sprite_safe(0, 0); }};

        while (1) {
            ui::InputSnapshot input;
            auto js = joystick.get_joystick_state();
            auto tbs = type_button.get_button_state();
            auto ebs = enter_button.get_button_state();
            auto bbs = back_button.get_button_state();
            input.up_edge = js.pushed_up_edge;
            input.down_edge = js.pushed_down_edge;
            input.left_edge = js.left;
            input.back_pressed = bbs.pushed;
            input.type_pressed = tbs.pushed;
            input.enter_pressed = ebs.pushed;

            const auto cmd = presenter.handle_input(input);
            renderer.render(state, render_api);
            if (cmd == ui::choice::Presenter::Command::Confirm) {
                return state.selected;
            }
            if (cmd == ui::choice::Presenter::Command::Cancel) {
                return -1;
            }
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }
};

StackType_t *ProfileSetting::auth_task_stack_ = nullptr;
StaticTask_t ProfileSetting::auth_task_buffer_;
TaskHandle_t ProfileSetting::auth_task_handle_ = nullptr;
esp_err_t ProfileSetting::auth_task_result_ = ESP_FAIL;
StackType_t *ProfileSetting::profile_task_stack_ = nullptr;
StaticTask_t ProfileSetting::profile_task_buffer_;
TaskHandle_t ProfileSetting::profile_task_handle_ = nullptr;
