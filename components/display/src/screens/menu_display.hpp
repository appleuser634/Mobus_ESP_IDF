#pragma once

#include "../runtime/prelude.hpp"
#include "talk_display.hpp"
#include "message_box.hpp"
#include "contact_book.hpp"
#include "open_chat_wifi_p2p.hpp"
#include "composer_setting_game.hpp"
#include "app/menu/effects_service.hpp"

class MenuDisplay {
   public:
    static constexpr uint32_t kTaskStackWords = 4288;

    void start_menu_task() {
        printf("Start Menu Task...");
        if (task_handle_) {
            ESP_LOGW(TAG, "menu_task already running");
            return;
        }
        if (!allocate_internal_stack(task_stack_, kTaskStackWords,
                                     "MenuDisplay")) {
            ESP_LOGE(TAG, "Failed to alloc menu stack");
            return;
        }
        // Increase stack to avoid rare overflows during heavy UI & networking
        task_handle_ = xTaskCreateStaticPinnedToCore(
            &menu_task, "menu_task", kTaskStackWords, NULL, 6, task_stack_,
            &task_buffer_, 0);
        if (!task_handle_) {
            ESP_LOGE(TAG, "Failed to start menu_task (free_heap=%u)",
                     static_cast<unsigned>(heap_caps_get_free_size(
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        }
    }

    // static HttpClient http;

    static void menu_task(void *pvParameters) {
        HttpClient &http_client = HttpClient::shared();

        Joystick joystick;
        PowerMonitor power;

        Game game;
        ContactBook contactBook;
        SettingMenu settingMenu;

        Button type_button(GPIO_NUM_46);
        Button enter_button(GPIO_NUM_5);

        // TODO: Buttonクラスではなく別で実装する
        Button charge_stat(GPIO_NUM_8);

        lcd.setRotation(2);

        ui::menu::ViewState view_state;
        ui::menu::Presenter presenter(view_state);
        ui::menu::Renderer renderer;

        const int icon_pos_x[3] = {9, 51, 93};
        const int icon_pos_y[3] = {22, 22, 22};

        auto ensure_menu_sprite = [&]() -> bool {
            if (!ensure_sprite_surface(lcd.width(), lcd.height(), 8,
                                       "MenuDisplay")) {
                return false;
            }
            sprite.setFont(&fonts::Font4);
            sprite.setTextWrap(false);  // disable wrap at right edge
            return true;
        };

        if (!ensure_menu_sprite()) {
            ESP_LOGW(TAG,
                     "[OLED] MenuDisplay sprite unavailable; running headless");
        }

        int64_t last_status_update_us = esp_timer_get_time();
        bool needs_redraw = true;

        // 通知の取得はOTA検証が完了するまで少し待つ（フラッシュ書込みと競合を避ける）
        {
            const esp_partition_t *running = esp_ota_get_running_partition();
            esp_ota_img_states_t stp;
            int wait_ms = 0;
            while (running &&
                   esp_ota_get_state_partition(running, &stp) == ESP_OK &&
                   stp == ESP_OTA_IMG_PENDING_VERIFY && wait_ms < 12000) {
                vTaskDelay(pdMS_TO_TICKS(200));
                wait_ms += 200;
            }
        }
        http_client.start_notifications();
        JsonDocument notif_res = http_client.get_notifications();

        // バッテリー電圧の取得
        PowerMonitor::power_state_t power_state = power.get_power_state();
        const int clamped_voltage =
            app::menu::clamp_power_voltage(power_state.power_voltage);
        view_state.power_per_pix = app::menu::power_voltage_to_pixel(clamped_voltage);
        view_state.radio_level = 0;
        view_state.menu_count = 3;
        view_state.has_notification = app::menu::has_notification(notif_res);

        const int wake_pins[] = {static_cast<int>(type_button.gpio_num),
                                 static_cast<int>(enter_button.gpio_num),
                                 static_cast<int>(GPIO_NUM_3)};
        auto open_contact_book = [&]() {
            contactBook.running_flag = true;
            contactBook.start_message_menue_task();
            while (contactBook.running_flag) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
        };
        auto open_settings = [&]() {
            settingMenu.running_flag = true;
            settingMenu.start_message_menue_task();
            while (settingMenu.running_flag) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
        };
        auto open_game = [&]() {
            game.running_flag = true;
            game.start_game_task();
            while (game.running_flag) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
        };
        auto after_return = [&]() { sprite.setFont(&fonts::Font4); };

        while (1) {
            if (sprite.getBuffer() == nullptr) {
                if (!ensure_menu_sprite()) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
                needs_redraw = true;
            }
            const int64_t now_us = esp_timer_get_time();
            if ((now_us - last_status_update_us) >= 3000000LL) {
                // 電波強度を更新
                wifi_ap_record_t ap = {};
                if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                    view_state.radio_level = app::menu::rssi_to_bars(ap.rssi);
                } else {
                    view_state.radio_level = 0;
                }

                // バッテリー電圧を更新
                power_state = power.get_power_state();
                const int updated_voltage =
                    app::menu::clamp_power_voltage(power_state.power_voltage);
                view_state.power_per_pix =
                    app::menu::power_voltage_to_pixel(updated_voltage);

                // 通知情報を更新
                notif_res = http_client.get_notifications();
                view_state.has_notification = app::menu::has_notification(notif_res);
                last_status_update_us = now_us;
                needs_redraw = true;
            }

            Button::button_state_t type_charge_stat =
                charge_stat.get_button_state();
            charge_stat.clear_button_state();
            charge_stat.reset_timer();

            Joystick::joystick_state_t joystick_state =
                joystick.get_joystick_state();
            // printf("UP:%s\n", joystick_state.up ? "true" : "false");
            // printf("DOWN:%s\n", joystick_state.down ? "true" : "false");
            // printf("RIGHT:%s\n", joystick_state.right ? "true" :
            // "false"); printf("LEFT:%s\n", joystick_state.left ? "true" :
            // "false");

            Button::button_state_t type_button_state =
                type_button.get_button_state();

            Button::button_state_t enter_button_state =
                enter_button.get_button_state();

            ui::InputSnapshot input{};
            input.left_edge = joystick_state.pushed_left_edge;
            input.right_edge = joystick_state.pushed_right_edge;
            input.type_pressed = type_button_state.pushed;
            input.enter_pressed = enter_button_state.pushed;

            if (presenter.handle_input(input)) {
                needs_redraw = true;
            }

            app::menu::MenuAction action = presenter.resolve_action(input);
            if (app::menu::execute_menu_action(action, open_contact_book,
                                               open_settings, open_game,
                                               after_return)) {

                // 通知情報を更新
                notif_res = http_client.get_notifications();
                view_state.has_notification = app::menu::has_notification(notif_res);
                needs_redraw = true;

                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
            }

            // esp_task_wdt_reset();

            // 30秒操作がなければsleep
            int button_free_time = type_button_state.release_sec / 1000000;
            int joystick_free_time = joystick_state.release_sec / 1000000;

            view_state.charging = type_charge_stat.pushing;
            const bool idle_timeout = app::menu::is_idle_timeout(
                button_free_time, joystick_free_time);
            const bool should_sleep = app::menu::should_enter_sleep(
                button_free_time, joystick_free_time, input.enter_pressed);
            if (idle_timeout) {
                printf("button_free_time:%d\n", button_free_time);
                printf("joystick_free_time:%d\n", joystick_free_time);
            }
            if (should_sleep) {
                const char *sleep_reason =
                    idle_timeout ? "idle timeout" : "enter button";
                (void)sleep_reason;
                if (app::menu::execute_light_sleep(
                        wake_pins, sizeof(wake_pins) / sizeof(wake_pins[0]),
                        [&]() {
                            sprite.fillRect(0, 0, 128, 64, 0);
                            push_sprite_safe(0, 0);
                        },
                        [&](int pin) {
                            esp_err_t err = gpio_wakeup_enable(
                                static_cast<gpio_num_t>(pin),
                                GPIO_INTR_HIGH_LEVEL);
                            if (err != ESP_OK) {
                                ESP_LOGW(TAG,
                                         "Failed to enable wakeup on GPIO%d: %s",
                                         pin, esp_err_to_name(err));
                            }
                        },
                        [&]() {
                            esp_err_t gpio_wake_err = esp_sleep_enable_gpio_wakeup();
                            if (gpio_wake_err != ESP_OK) {
                                ESP_LOGW(TAG, "Failed to enable GPIO wakeup: %s",
                                         esp_err_to_name(gpio_wake_err));
                            }
                        },
                        [&]() { esp_light_sleep_start(); },
                        [&]() {
                            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
                        },
                        [&](int pin) {
                            gpio_wakeup_disable(static_cast<gpio_num_t>(pin));
                        },
                        [&]() {
                            type_button.clear_button_state();
                            type_button.reset_timer();
                            enter_button.clear_button_state();
                            enter_button.reset_timer();
                            joystick.reset_timer();
                        },
                        [&]() {
                            last_status_update_us = esp_timer_get_time();
                            needs_redraw = true;
                            g_oled_ready = false;
                            if (ensure_menu_sprite()) {
                                push_sprite_safe(0, 0);
                            }
                        })) {
                    continue;
                }
            }

            if (needs_redraw) {
                const ui::menu::RenderApi render_api{
                    .begin_frame =
                        [&]() {
                            sprite.fillRect(0, 0, 128, 64, 0);
                        },
                    .draw_status =
                        [&](int radio_level, int battery_pix, bool charging) {
                            sprite.drawFastHLine(0, 12, 128, 0xFFFF);
                            int rx = 4;
                            int ry = 6;
                            int rh = 4;
                            for (int r = radio_level; r > 0; --r) {
                                sprite.fillRect(rx, ry, 2, rh, 0xFFFF);
                                rx += 3;
                                ry -= 2;
                                rh += 2;
                            }
                            sprite.drawRoundRect(110, 0, 14, 8, 2, 0xFFFF);
                            sprite.fillRect(111, 0, battery_pix, 8, 0xFFFF);
                            if (charging) {
                                sprite.fillRect(105, 2, 2, 2, 0xFFFF);
                            }
                            sprite.fillRect(124, 2, 1, 4, 0xFFFF);
                        },
                    .draw_selection =
                        [&](int x, int y, int w, int h, int r) {
                            sprite.fillRoundRect(x, y, w, h, r, 0xFFFF);
                        },
                    .draw_menu_icon =
                        [&](int index, bool selected) {
                            const unsigned char *icon_image = mail_icon;
                            if (index == 1) {
                                icon_image = setting_icon;
                            } else if (index == 2) {
                                icon_image = game_icon;
                            }
                            if (selected) {
                                sprite.drawBitmap(icon_pos_x[index],
                                                  icon_pos_y[index], icon_image,
                                                  30, 30, TFT_WHITE, TFT_BLACK);
                            } else {
                                sprite.drawBitmap(icon_pos_x[index],
                                                  icon_pos_y[index], icon_image,
                                                  30, 30, TFT_BLACK, TFT_WHITE);
                            }
                        },
                    .draw_notification =
                        [&](bool selected_menu) {
                            sprite.fillCircle(37, 25, 4,
                                              selected_menu ? 0 : 0xFFFF);
                        },
                    .present = [&]() { push_sprite_safe(0, 0); }};
                renderer.render(view_state, render_api);
                needs_redraw = false;
                vTaskDelay(pdMS_TO_TICKS(20));
            } else {
                vTaskDelay(pdMS_TO_TICKS(40));
            }
        }

        UBaseType_t watermark_words = uxTaskGetStackHighWaterMark(nullptr);
        ESP_LOGI(TAG, "menu_task stack high watermark: %u words (%u bytes)",
                 static_cast<unsigned>(watermark_words),
                 static_cast<unsigned>(watermark_words * sizeof(StackType_t)));
        task_handle_ = nullptr;
        vTaskDelete(NULL);
    };

   private:
    static TaskHandle_t task_handle_;
    static StaticTask_t task_buffer_;
    static StackType_t *task_stack_;
};

TaskHandle_t MenuDisplay::task_handle_ = nullptr;
StaticTask_t MenuDisplay::task_buffer_;
StackType_t *MenuDisplay::task_stack_ = nullptr;
