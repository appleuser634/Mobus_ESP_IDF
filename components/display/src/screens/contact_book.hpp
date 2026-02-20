class ContactBook {
   public:
    static bool running_flag;
    static constexpr uint32_t kTaskStackWords = 6192;

    void start_message_menue_task() {
        printf("Start ContactBook Task...");
        if (task_handle_) {
            ESP_LOGD("CONTACT", "Task already running");
            return;
        }
        if (!allocate_internal_stack(task_stack_, kTaskStackWords,
                                     "ContactBook")) {
            ESP_LOGE("CONTACT", "Stack alloc failed");
            running_flag = false;
            return;
        }
        running_flag = true;
        task_handle_ = xTaskCreateStaticPinnedToCore(
            &message_menue_task, "message_menue_task", kTaskStackWords, NULL, 6,
            task_stack_, &task_buffer_, 1);
        if (!task_handle_) {
            ESP_LOGE("CONTACT", "Failed to start contact task (stack=%u)",
                     kTaskStackWords);
            running_flag = false;
        }
    }
    static void message_menue_task(void *pvParameters) {
        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        bool wdt_registered = false;
        if (esp_task_wdt_add(NULL) == ESP_OK) {
            wdt_registered = true;
        } else {
            ESP_LOGD("CONTACT", "esp_task_wdt_add failed");
        }
        auto feed_wdt = [&]() {
            if (!wdt_registered) return;
            esp_err_t err = esp_task_wdt_reset();
            if (err != ESP_OK) {
                ESP_LOGD("CONTACT", "esp_task_wdt_reset failed: %s",
                         esp_err_to_name(err));
            }
        };
        auto finish_task = [&]() {
            running_flag = false;
            task_handle_ = nullptr;

            UBaseType_t watermark_words = uxTaskGetStackHighWaterMark(nullptr);
            ESP_LOGI(
                "CONTACT", "stack high watermark: %u words (%u bytes)",
                static_cast<unsigned>(watermark_words),
                static_cast<unsigned>(watermark_words * sizeof(StackType_t)));
            if (wdt_registered) {
                esp_err_t del_err = esp_task_wdt_delete(NULL);
                if (del_err != ESP_OK) {
                    ESP_LOGD("CONTACT", "esp_task_wdt_delete failed: %s",
                             esp_err_to_name(del_err));
                }
            }
            vTaskDelete(NULL);
        };

        HttpClient &http_client = HttpClient::shared();
        ui::StatusPanelApi status_panel_api;
        status_panel_api.begin_frame = [&]() {
            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.setFont(&fonts::Font2);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
        };
        status_panel_api.draw_center_text = [&](const std::string &line, int y) {
            sprite.drawCenterString(line.c_str(), 64, y);
        };
        status_panel_api.present = [&]() { push_sprite_safe(0, 0); };

        auto recreate_contact_sprite = [&](int width, int height) -> bool {
            struct Attempt {
                uint8_t depth;
                bool use_psram;
            };
            constexpr Attempt attempts[] = {
                {8, true},  {8, false}, {4, true},
                {4, false}, {1, true},  {1, false},
            };

            sprite.deleteSprite();
            for (const auto &attempt : attempts) {
                sprite.setPsram(attempt.use_psram);
                sprite.setColorDepth(attempt.depth);
                sprite.setFont(&fonts::Font2);
                sprite.setTextWrap(true);
                if (sprite.createSprite(width, height)) {
                    ESP_LOGD("CONTACT",
                             "Sprite created %dx%d depth=%u psram=%s (free=%u "
                             "largest=%u)",
                             width, height, attempt.depth,
                             attempt.use_psram ? "true" : "false",
                             (unsigned)heap_caps_get_free_size(
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                             (unsigned)heap_caps_get_largest_free_block(
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                    return true;
                }
                ESP_LOGD("CONTACT",
                         "createSprite(%d,%d) depth=%u psram=%s failed "
                         "(free=%u largest=%u)",
                         width, height, attempt.depth,
                         attempt.use_psram ? "true" : "false",
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                           MALLOC_CAP_8BIT),
                         (unsigned)heap_caps_get_largest_free_block(
                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                sprite.deleteSprite();
            }
            return false;
        };
        if (!recreate_contact_sprite(lcd.width(), lcd.height())) {
            finish_task();
            return;
        }

        int MAX_CONTACTS = 20;
        int CONTACT_PER_PAGE = 4;

        // Fetch friends list via BLE (if connected) or HTTP( Wi‑Fi )
        std::vector<app::contactbook::ContactEntry> contacts;
        bool got_from_ble = false;
        if (!wifi_is_connected() && ble_uart_is_ready()) {
            std::string username = get_nvs((char *)"user_name");
            app::contactbook::FetchLoopHooks hooks;
            hooks.on_tick = [&]() {
                ui::render_status_panel(status_panel_api, "Waiting phone...",
                                        "Back to exit");
            };
            hooks.should_cancel = [&]() {
                return back_button.get_button_state().pushed;
            };
            got_from_ble = app::contactbook::fetch_contacts_via_ble(
                username, contacts, 6000, hooks);
            if (!got_from_ble && hooks.should_cancel && hooks.should_cancel()) {
                finish_task();
                return;
            }
        }

        if (!got_from_ble) {
            // Keep BLE connection alive and fall back to HTTP in parallel.
            // With NimBLE using PSRAM, Wi‑Fi coexists without disabling BLE.
            // HTTP fallback (Wi‑Fi). Ensure Wi‑Fi is connected.
            int64_t last_connect_try_us = 0;
            int64_t last_ap_check_us = 0;
            while (true) {
                feed_wdt();
                bool connected = false;
                if (s_wifi_event_group) {
                    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
                    connected = bits & WIFI_CONNECTED_BIT;
                }
                // Event bits can lag; check link state periodically.
                const int64_t now_us = esp_timer_get_time();
                if (!connected && (now_us - last_ap_check_us) >= 1000000LL) {
                    last_ap_check_us = now_us;
                    wifi_ap_record_t ap = {};
                    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                        connected = true;
                    }
                }
                // Ensure connect is actually requested (start alone is not
                // enough).
                if (!connected && (now_us - last_connect_try_us) >= 3000000LL) {
                    last_connect_try_us = now_us;
                    esp_err_t serr = esp_wifi_start();
                    if (serr != ESP_OK && serr != ESP_ERR_WIFI_CONN &&
                        serr != ESP_ERR_WIFI_NOT_STOPPED) {
                        ESP_LOGW("CONTACT", "esp_wifi_start returned %s",
                                 esp_err_to_name(serr));
                    }
                    esp_err_t cerr = esp_wifi_connect();
                    if (cerr != ESP_OK && cerr != ESP_ERR_WIFI_CONN) {
                        ESP_LOGW("CONTACT", "esp_wifi_connect returned %s",
                                 esp_err_to_name(cerr));
                    }
                }
                if (connected) break;

                ui::render_status_panel(status_panel_api, "Connecting Wi-Fi...",
                                        "Press Back to exit");
                if (back_button.get_button_state().pushed) {
                    finish_task();
                    return;
                }
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }

            const auto creds = chatapi::load_credentials_from_nvs();
            std::string username = creds.username;
            if (recreate_contact_sprite(lcd.width(), lcd.height())) {
                ui::render_status_panel(status_panel_api, "Loading contacts...",
                                        "via Wi-Fi", 24, 40);
            }
            feed_wdt();
            auto friends_resp = app::contactbook::fetch_contacts_via_http(
                http_client, username, contacts, 15000);
            feed_wdt();
            if (!recreate_contact_sprite(lcd.width(), lcd.height())) {
                ESP_LOGE("CONTACT", "Sprite recreate failed after HTTP");
                finish_task();
                return;
            }
            if (!(friends_resp.err == ESP_OK && friends_resp.status >= 200 &&
                  friends_resp.status < 300)) {
                ESP_LOGW("CONTACT", "Friends fetch failed (err=%s status=%d)",
                         esp_err_to_name(friends_resp.err),
                         friends_resp.status);
            }
        }

        ui::contactbook::ViewState contact_view_state;
        contact_view_state.select_index = 0;
        contact_view_state.contact_per_page = CONTACT_PER_PAGE;
        contact_view_state.font_height = 13;
        contact_view_state.margin = 3;
        ui::contactbook::Presenter contact_presenter(contact_view_state);
        ui::contactbook::Renderer contact_renderer;
        ui::contactbook::RenderApi contact_render_api;
        contact_render_api.begin_frame = [&]() {
            sprite.fillScreen(0);
            sprite.setFont(&fonts::Font2);
        };
        contact_render_api.draw_row =
            [&](int y, const ui::contactbook::RowData &row, bool selected) {
                sprite.setCursor(10, y);
                if (selected) {
                    sprite.setTextColor(0x000000u, 0xFFFFFFu);
                    sprite.fillRect(0, y, 128, contact_view_state.font_height + 3,
                                    0xFFFF);
                } else {
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                }
                sprite.print(row.label.c_str());
                if (row.has_unread) {
                    const int badge_h = 10;
                    const int badge_y =
                        y + ((contact_view_state.font_height + 3 - badge_h) / 2);
                    int badge_x = 104;
                    int badge_w = 20;
                    char badge_text[5] = {0};
                    if (row.unread_count > 99) {
                        strcpy(badge_text, "99+");
                        badge_w = 22;
                        badge_x = 102;
                    } else {
                        snprintf(badge_text, sizeof(badge_text), "%d",
                                 row.unread_count);
                    }
                    const uint16_t badge_bg = selected ? 0x0000 : 0xFFFF;
                    const uint16_t badge_fg = selected ? 0xFFFF : 0x0000;
                    sprite.fillRoundRect(badge_x, badge_y, badge_w, badge_h, 3,
                                         badge_bg);
                    sprite.setTextColor(badge_fg, badge_bg);
                    sprite.drawCenterString(badge_text, badge_x + badge_w / 2,
                                            badge_y - 2);
                }
            };
        contact_render_api.present = [&]() { push_sprite_safe(0, 0); };

        // 通知の取得
        JsonDocument notif_res = http_client.get_notifications();

        MessageBox box;
        (void)box;
        ui::contactrunners::ActionContext contact_action_ctx{
            .sprite = sprite,
            .type_button = type_button,
            .enter_button = enter_button,
            .back_button = back_button,
            .joystick = joystick,
            .feed_wdt = feed_wdt,
            .present = [&]() { push_sprite_safe(0, 0); },
            .wifi_connected = wifi_is_connected,
            .input_text = [&](const std::string &title,
                              const std::string &seed) {
                return wifi_input_info_proxy(title, seed);
            },
        };
        while (1) {
            // Joystickの状態を取得
            Joystick::joystick_state_t joystick_state =
                joystick.get_joystick_state();

            // スイッチの状態を取得
            Button::button_state_t type_button_state =
                type_button.get_button_state();
            Button::button_state_t back_button_state =
                back_button.get_button_state();
            Button::button_state_t enter_button_state =
                enter_button.get_button_state();

            int base_count = (int)contacts.size();
            contact_view_state.rows = app::contactbookview::build_rows(contacts);

            ui::InputSnapshot nav_input;
            nav_input.up_edge = joystick_state.pushed_up_edge;
            nav_input.down_edge = joystick_state.pushed_down_edge;
            contact_presenter.handle_input(nav_input);
            contact_renderer.render(contact_view_state, contact_render_api);

            // ジョイスティック左を押されたらメニューへ戻る
            // 戻るボタンを押されたらメニューへ戻る
            if (joystick_state.left || back_button_state.pushed) {
                break;
            }

            if (type_button_state.pushed) {
                const int select_index = contact_view_state.select_index;
                const auto selection_kind =
                    app::contactbook::resolve_selection_kind(select_index,
                                                             base_count);
                if (selection_kind == app::contactbook::SelectionKind::AddFriend) {
                    ui::contactrunners::run_add_friend(contact_action_ctx);
                } else if (selection_kind ==
                           app::contactbook::SelectionKind::Pending) {
                    ui::contactrunners::run_pending_requests(contact_action_ctx);
                } else if (selection_kind ==
                           app::contactbook::SelectionKind::Contact &&
                           !contacts.empty()) {
                    box.running_flag = true;
                    // Set chat title as username for UI, pass identifier for
                    // API
                    MessageBox::chat_title =
                        contacts[select_index].display_name;
                    MessageBox::set_active_contact(
                        contacts[select_index].short_id,
                        contacts[select_index].friend_id);
                    box.start_box_task(contacts[select_index].identifier);
                    while (box.running_flag) {
                        feed_wdt();
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                    }
                    app::contactbookview::mark_read(contacts[select_index]);

                    if (!recreate_contact_sprite(lcd.width(), lcd.height())) {
                        ESP_LOGE("CONTACT",
                                 "Sprite recreate failed after MessageBox");
                        finish_task();
                        return;
                    }

                    // 通知の取得
                    notif_res = http_client.get_notifications();
                    notif_res = http_client.get_notifications();
                }

                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
            }

            feed_wdt();
            vTaskDelay(10);
        }

        finish_task();
    };

   private:
    static TaskHandle_t task_handle_;
    static StaticTask_t task_buffer_;
    static StackType_t *task_stack_;
};
bool ContactBook::running_flag = false;
TaskHandle_t ContactBook::task_handle_ = nullptr;
StaticTask_t ContactBook::task_buffer_;
StackType_t *ContactBook::task_stack_ = nullptr;

