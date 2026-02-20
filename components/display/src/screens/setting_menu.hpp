#pragma once

class SettingMenu {
   public:
    static bool running_flag;
    static bool sound_dirty;

    static constexpr uint32_t kTaskStackWords = 6192;

    void start_message_menue_task() {
        printf("Start MessageMenue Task...");
        if (task_handle_) {
            ESP_LOGW("SETTING_MENU", "Task already running");
            return;
        }

        running_flag = true;
        task_handle_ = xTaskCreateStaticPinnedToCore(
            &message_menue_task, "message_menue_task", kTaskStackWords, NULL, 6,
            task_stack_, &task_buffer_, 1);
        if (!task_handle_) {
            ESP_LOGE("SETTING_MENU",
                     "Failed to start message menu task (err=%d)",
                     (int)errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY);
            running_flag = false;
        }
    }

    static void message_menue_task(void *pvParameters) {
        lcd.init();

        WiFiSetting wifi_setting;
        OpenChat open_chat;

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        bool wdt_registered = false;
        if (esp_task_wdt_add(NULL) == ESP_OK) {
            wdt_registered = true;
        } else {
            ESP_LOGW("SETTING_MENU", "esp_task_wdt_add failed");
        }
        auto feed_wdt = [&]() {
            if (!wdt_registered) return;
            esp_err_t err = esp_task_wdt_reset();
            if (err != ESP_OK) {
                ESP_LOGW("SETTING_MENU", "esp_task_wdt_reset failed: %s",
                         esp_err_to_name(err));
            }
        };
        auto finish_task = [&]() {
            running_flag = false;
            task_handle_ = nullptr;
            if (wdt_registered) {
                esp_err_t del_err = esp_task_wdt_delete(NULL);
                if (del_err != ESP_OK) {
                    ESP_LOGW("SETTING_MENU", "esp_task_wdt_delete failed: %s",
                             esp_err_to_name(del_err));
                }
            }
            vTaskDelete(NULL);
        };

        lcd.setRotation(2);
        // int MAX_SETTINGS = 20; // unused
        int ITEM_PER_PAGE = 4;

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font4);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height());

        const std::array<ui::Key, 16> setting_keys = {
            ui::Key::SettingsProfile,    ui::Key::SettingsWifi,
            ui::Key::SettingsBluetooth,  ui::Key::SettingsLanguage,
            ui::Key::SettingsSound,      ui::Key::SettingsVibration,
            ui::Key::SettingsBootSound,  ui::Key::SettingsRtc,
            ui::Key::SettingsOpenChat,   ui::Key::SettingsComposer,
            ui::Key::SettingsAutoUpdate, ui::Key::SettingsOtaManifest,
            ui::Key::SettingsUpdateNow,  ui::Key::SettingsFirmwareInfo,
            ui::Key::SettingsDevelop,    ui::Key::SettingsFactoryReset,
        };

        ui::settingmenu::ViewState view_state;
        view_state.item_per_page = ITEM_PER_PAGE;
        view_state.font_height = 13;
        view_state.margin = 3;
        view_state.rows.reserve(setting_keys.size());
        ui::settingmenu::Presenter presenter(view_state);
        ui::settingmenu::Renderer renderer;
        auto reset_controls = [&](bool clear_enter) {
            type_button.clear_button_state();
            type_button.reset_timer();
            back_button.clear_button_state();
            back_button.reset_timer();
            if (clear_enter) {
                enter_button.clear_button_state();
                enter_button.reset_timer();
            }
            joystick.reset_timer();
        };
        auto show_status = [&](const std::string &line1,
                               const std::string &line2, int delay_ms) {
            ui::StatusPanelApi api{
                .begin_frame = [&]() { sprite.fillRect(0, 0, 128, 64, 0); },
                .draw_center_text =
                    [&](const std::string &text, int y) {
                        sprite.setFont(&fonts::Font2);
                        sprite.setTextColor(0xFFFFFFu, 0x000000u);
                        sprite.drawCenterString(text.c_str(), 64, y);
                    },
                .present = [&]() { push_sprite_safe(0, 0); }};
            ui::render_status_panel(api, line1, line2, 22, 40);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        };
        auto run_wifi_action = [&]() {
            wifi_setting.running_flag = true;
            wifi_setting.start_wifi_setting_task();
            app::settingtask::wait_while_running(wifi_setting.running_flag,
                                                 feed_wdt);
            type_button.clear_button_state();
            type_button.reset_timer();
            joystick.reset_timer();
        };
        auto run_vibration_action = [&]() {
            reset_controls(true);
            const bool on = app::settingaction::toggle_vibration();
            show_status(on ? "Vibration: ON" : "Vibration: OFF", "", 900);
        };
        auto run_update_now_action = [&]() {
            mqtt_rt_pause();
            show_status("Rebooting OTA...", "", 150);
            mobus_request_ota_minimal_mode();
        };
        auto run_auto_update_action = [&]() {
            bool on = app::settingaction::toggle_auto_update();
            show_status(on ? "Auto Update: ON" : "Auto Update: OFF", "", 800);
            if (on) {
                ota_client::start_background_task();
            }
            reset_controls(false);
        };
        auto run_develop_action = [&]() {
            const auto result = app::settingaction::toggle_develop_mode();
            show_status(result.on ? "Develop: ON" : "Develop: OFF",
                        result.endpoint, 1200);
            reset_controls(false);
        };
        auto run_profile_action = [&]() {
            Profile();
            reset_controls(false);
        };
        auto run_rtc_action = [&]() {
            reset_controls(true);
            P2P_Display p2p;
            p2p.morse_p2p();
            reset_controls(true);
        };
        auto run_open_chat_action = [&]() {
            reset_controls(true);
            open_chat.running_flag = true;
            open_chat.start_open_chat_task();
            app::settingtask::wait_while_running(open_chat.running_flag, feed_wdt);
            sprite.setFont(&fonts::Font2);
        };
        auto run_composer_action = [&]() {
            Composer comp;
            comp.running_flag = true;
            comp.start_composer_task();
            app::settingtask::wait_while_running(comp.running_flag, feed_wdt);
            sprite.setFont(&fonts::Font2);
            reset_controls(false);
        };
        ui::settingrunners::DialogContext dialog_ctx{
            .sprite = sprite,
            .type_button = type_button,
            .enter_button = enter_button,
            .back_button = back_button,
            .joystick = joystick,
            .feed_wdt = feed_wdt,
            .present = [&]() { push_sprite_safe(0, 0); },
            .delete_sprite = [&]() { sprite.deleteSprite(); },
            .recreate_sprite = [&]() { sprite.createSprite(lcd.width(), lcd.height()); },
            .mqtt_pause = mqtt_rt_pause,
            .mqtt_resume = mqtt_rt_resume,
            .wifi_connected = wifi_is_connected,
        };
        auto run_language_action = [&](ui::Lang lang_now) {
            ui::settingrunners::run_language(dialog_ctx, lang_now);
        };
        auto run_sound_action = [&]() {
            ui::settingrunners::run_sound(dialog_ctx, SettingMenu::sound_dirty);
        };
        auto run_boot_sound_action = [&]() {
            ui::settingrunners::run_boot_sound(dialog_ctx);
        };
        auto run_bluetooth_action = [&]() {
            ui::settingrunners::run_bluetooth_pairing(dialog_ctx);
        };
        auto run_ota_manifest_action = [&]() {
            ui::settingrunners::run_ota_manifest(dialog_ctx);
        };
        auto run_firmware_info_action = [&]() {
            ui::settingrunners::run_firmware_info(dialog_ctx);
        };
        auto run_factory_reset_action = [&]() {
            ui::settingrunners::run_factory_reset(dialog_ctx);
        };

        while (1) {
            feed_wdt();
            ui::Lang lang = ui::current_lang();
            // Joystickの状態を取得
            Joystick::joystick_state_t joystick_state =
                joystick.get_joystick_state();

            // スイッチの状態を取得
            Button::button_state_t type_button_state =
                type_button.get_button_state();
            Button::button_state_t back_button_state =
                back_button.get_button_state();

            sprite.fillScreen(0);

            const lgfx::IFont *menu_font =
                (lang == ui::Lang::Ja)
                    ? static_cast<const lgfx::IFont *>(
                          &mobus_fonts::MisakiGothic8())
                    : static_cast<const lgfx::IFont *>(&fonts::Font2);
            sprite.setFont(menu_font);
            view_state.rows = app::settingmenuview::build_rows(setting_keys, lang);
            presenter.clamp();

            ui::InputSnapshot list_input{};
            list_input.up_edge = joystick_state.pushed_up_edge;
            list_input.down_edge = joystick_state.pushed_down_edge;
            presenter.handle_input(list_input);

            ui::settingmenu::RenderApi render_api{
                .begin_frame = [&]() { sprite.fillScreen(0); },
                .draw_row =
                    [&](int y, const ui::settingmenu::RowData &row,
                        bool selected) {
                        sprite.setCursor(10, y);
                        if (selected) {
                            sprite.setTextColor(0x000000u, 0xFFFFFFu);
                            sprite.fillRect(0, y, 128, view_state.font_height + 3,
                                            0xFFFF);
                        } else {
                            sprite.setTextColor(0xFFFFFFu, 0x000000u);
                        }
                        sprite.print(row.label.c_str());
                    },
                .present = [&]() { push_sprite_safe(0, 0); }};
            renderer.render(view_state, render_api);

            // ジョイスティック左を押されたらメニューへ戻る
            // 戻るボタンを押されたらメニューへ戻る
            if (joystick_state.left || back_button_state.pushed) {
                break;
            }

            ui::Key selected_key = app::settingmenuview::selected_key_or_default(
                view_state, ui::Key::SettingsProfile);
            const auto action =
                app::settingmenuaction::resolve(selected_key,
                                                type_button_state.pushed);
            switch (action) {
                case app::settingmenuaction::Action::Wifi:
                    run_wifi_action();
                    break;
                case app::settingmenuaction::Action::Language:
                    run_language_action(lang);
                    break;
                case app::settingmenuaction::Action::Sound:
                    run_sound_action();
                    break;
                case app::settingmenuaction::Action::Vibration:
                    run_vibration_action();
                    break;
                case app::settingmenuaction::Action::BootSound:
                    run_boot_sound_action();
                    break;
                case app::settingmenuaction::Action::Bluetooth:
                    run_bluetooth_action();
                    break;
                case app::settingmenuaction::Action::OtaManifest:
                    run_ota_manifest_action();
                    break;
                case app::settingmenuaction::Action::UpdateNow:
                    run_update_now_action();
                    break;
                case app::settingmenuaction::Action::AutoUpdate:
                    run_auto_update_action();
                    break;
                case app::settingmenuaction::Action::FirmwareInfo:
                    run_firmware_info_action();
                    break;
                case app::settingmenuaction::Action::Develop:
                    run_develop_action();
                    break;
                case app::settingmenuaction::Action::Profile:
                    run_profile_action();
                    break;
                case app::settingmenuaction::Action::Rtc:
                    run_rtc_action();
                    break;
                case app::settingmenuaction::Action::OpenChat:
                    run_open_chat_action();
                    break;
                case app::settingmenuaction::Action::Composer:
                    run_composer_action();
                    break;
                case app::settingmenuaction::Action::FactoryReset:
                    run_factory_reset_action();
                    break;
                case app::settingmenuaction::Action::None:
                default:
                    break;
            }

            vTaskDelay(1);
        }

        UBaseType_t watermark_words = uxTaskGetStackHighWaterMark(nullptr);
        ESP_LOGI("SETTING_MENU", "stack high watermark: %u words (%u bytes)",
                 static_cast<unsigned>(watermark_words),
                 static_cast<unsigned>(watermark_words * sizeof(StackType_t)));

        task_handle_ = nullptr;
        finish_task();
    };

   private:
    static TaskHandle_t task_handle_;
    static StaticTask_t task_buffer_;
    static StackType_t task_stack_[kTaskStackWords];
};
bool SettingMenu::running_flag = false;
bool SettingMenu::sound_dirty = false;
TaskHandle_t SettingMenu::task_handle_ = nullptr;
StaticTask_t SettingMenu::task_buffer_;
StackType_t SettingMenu::task_stack_[SettingMenu::kTaskStackWords] = {};

