class MessageBox {
   public:
    static bool running_flag;
    static std::string chat_title;  // display username for header
    static std::string active_short_id;
    static std::string active_friend_id;
    static constexpr uint32_t kTaskStackWords = 9216;
    static TaskHandle_t task_handle_;
    static StaticTask_t task_buffer_;
    static StackType_t task_stack_[kTaskStackWords];

    static void set_active_contact(const std::string &short_id,
                                   const std::string &friend_id) {
        active_short_id = short_id;
        active_friend_id = friend_id;
        ESP_LOGI(TAG,
                 "[BLE] Active contact identifiers set (short=%s friend_id=%s)",
                 active_short_id.c_str(), active_friend_id.c_str());
    }

    static std::string backend_identifier(const std::string &fallback) {
        if (!active_friend_id.empty()) return active_friend_id;
        if (!active_short_id.empty()) return active_short_id;
        return fallback;
    }

    void start_box_task(std::string chat_to) {
        printf("Start Box Task...");
        // Pass heap-allocated copy to avoid dangling pointer
        auto *arg = new std::string(chat_to);
        if (task_handle_) {
            ESP_LOGW(TAG, "box_task already running");
            delete arg;
            return;
        }
        task_handle_ = xTaskCreateStaticPinnedToCore(
            &box_task, "box_task", kTaskStackWords, arg, 6, task_stack_,
            &task_buffer_, 1);
        if (!task_handle_) {
            ESP_LOGE(TAG, "Failed to start box_task (free_heap=%u)",
                     static_cast<unsigned>(heap_caps_get_free_size(
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
            delete arg;
            running_flag = false;
        }
    }

    static void box_task(void *pvParameters) {
        // nvs_main(); // removed demo call
        lcd.init();

        TalkDisplay talk;
        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        sprite.fillRect(0, 0, 128, 64, 0);

        sprite.setFont(&fonts::Font4);
        sprite.setCursor(10, 20);
        sprite.print("Loading...");
        push_sprite_safe(0, 0);

        auto recreate_message_sprite = [&](int width, int height) -> bool {
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
                    ESP_LOGI(
                        TAG,
                        "[UI] message sprite %dx%d depth=%u psram=%s created",
                        width, height, attempt.depth,
                        attempt.use_psram ? "true" : "false");
                    return true;
                }
                ESP_LOGW(
                    TAG,
                    "[UI] message sprite alloc fail %dx%d depth=%u psram=%s"
                    " (free=%u largest=%u)",
                    width, height, attempt.depth,
                    attempt.use_psram ? "true" : "false",
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                      MALLOC_CAP_8BIT),
                    (unsigned)heap_caps_get_largest_free_block(
                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            }
            return false;
        };
        if (!recreate_message_sprite(lcd.width(), lcd.height())) {
            running_flag = false;
            task_handle_ = nullptr;
            vTaskDelete(NULL);
            return;
        }

        HttpClient &http_client = HttpClient::shared();

        // メッセージの取得（BLE優先、HTTPフォールバック）
        // Take ownership of heap arg and free after copy
        std::string chat_to = *(std::string *)pvParameters;
        delete (std::string *)pvParameters;
        ESP_LOGI(
            TAG,
            "[BLE] Opening message box (identifier=%s short=%s friend_id=%s)",
            chat_to.c_str(), active_short_id.c_str(), active_friend_id.c_str());
        JsonDocument res;
        int64_t last_history_poll_us = 0;
        constexpr int64_t kHistoryPollIntervalUs = 8LL * 1000LL * 1000LL;
        ui::messagebox::ViewState view_state;
        view_state.chat_to = chat_to;
        const std::string server_chat_id = resolve_chat_backend_id(chat_to);
        auto fetch_messages_via_ble = [&](const std::string &fid,
                                          int timeout_ms) -> bool {
            if (wifi_is_connected()) {
                ESP_LOGI(TAG,
                         "[BLE] fetch_messages skipped; Wi-Fi connected "
                         "(friend=%s)",
                         fid.c_str());
                return false;
            }
            if (!ble_uart_is_ready()) {
                ESP_LOGW(
                    TAG,
                    "[BLE] fetch_messages skipped; link inactive (friend=%s)",
                    fid.c_str());
                return false;
            }

            const std::string short_for_req =
                !active_short_id.empty() ? active_short_id : fid;
            const std::string friend_for_req =
                !active_friend_id.empty() ? active_friend_id : fid;

            long long rid = esp_timer_get_time();
            ESP_LOGI(TAG,
                     "[BLE] Requesting chat history (id=%s short=%s "
                     "friend_id=%s rid=%lld)",
                     fid.c_str(), short_for_req.c_str(), friend_for_req.c_str(),
                     rid);

            // Clear previous BLE messages so only fresh response is read
            save_nvs((char *)"ble_messages", std::string(""));
            ble_uart_clear_cached_messages();
            // Phone app should reply with a frame stored in NVS under
            // "ble_messages". Include redundant identifier fields to maximise
            // compatibility across app versions.
            StaticJsonDocument<256> doc;
            doc["id"] = std::to_string(rid);
            doc["type"] = "get_messages";
            JsonObject payload = doc.createNestedObject("payload");
            payload["friend_id"] = friend_for_req;
            payload["short_id"] = short_for_req;
            payload["friend"] = short_for_req;
            payload["limit"] = 20;
            std::string req;
            serializeJson(doc, req);
            req.push_back('\n');
            ESP_LOGI(TAG, "[BLE] Sync request payload: %s", req.c_str());
            int tx_res = ble_uart_send(
                reinterpret_cast<const uint8_t *>(req.c_str()), req.size());
            if (tx_res != 0) {
                ESP_LOGW(TAG, "[BLE] Failed to send sync request (err=%d)",
                         tx_res);
            }

            int waited = 0;
            while (waited < timeout_ms) {
                std::string js = ble_uart_get_cached_messages();
                if (js.empty()) {
                    js = get_nvs((char *)"ble_messages");
                } else {
                    ble_uart_clear_cached_messages();
                }
                if (!js.empty()) {
                    ESP_LOGI(TAG, "[BLE] Received cached response (%zu bytes)",
                             js.size());
                    DynamicJsonDocument in(4096);
                    DeserializationError err = deserializeJson(in, js);
                    if (err == DeserializationError::Ok) {
                        int count = 0;
                        // Prefer payload.messages when present.
                        if (in["payload"]["messages"].is<JsonArray>()) {
                            // Handle { type:..., payload: { messages:[...] } }
                            DynamicJsonDocument out(4096);
                            auto arr = out.createNestedArray("messages");
                            std::string my_id = get_nvs((char *)"user_id");
                            std::string my_name = get_nvs((char *)"user_name");
                            std::string my_short = get_nvs((char *)"short_id");
                            std::string my_login = get_nvs((char *)"login_id");
                            std::string my_username =
                                get_nvs((char *)"username");
                            if (my_name.empty()) my_name = my_login;
                            if (my_name.empty()) my_name = my_username;
                            if (my_name.empty()) my_name = my_short;
                            if (my_name.empty()) my_name = "me";
                            const std::string friend_id =
                                !active_friend_id.empty() ? active_friend_id
                                                          : fid;
                            const std::string friend_short =
                                !active_short_id.empty() ? active_short_id
                                                         : fid;
                            for (JsonObject m :
                                 in["payload"]["messages"].as<JsonArray>()) {
                                JsonObject o = arr.createNestedObject();
                                const char *content =
                                    m["content"].as<const char *>();
                                const char *msg =
                                    m["message"].as<const char *>();
                                o["message"] =
                                    content ? content : (msg ? msg : "");
                                const char *sender =
                                    m["sender_id"].as<const char *>();
                                const char *receiver =
                                    m["receiver_id"].as<const char *>();
                                const char *from_field =
                                    m["from"].as<const char *>();
                                bool is_friend = false;
                                bool is_me = false;
                                if (sender && !*sender) sender = nullptr;
                                if (receiver && !*receiver) receiver = nullptr;
                                if (sender && !friend_id.empty() &&
                                    friend_id == sender) {
                                    is_friend = true;
                                } else if (receiver && !friend_id.empty() &&
                                           friend_id == receiver) {
                                    is_me = true;
                                } else if (sender && !my_id.empty() &&
                                           my_id == sender) {
                                    is_me = true;
                                } else if (receiver && !my_id.empty() &&
                                           my_id == receiver) {
                                    is_friend = true;
                                } else if (from_field &&
                                           !friend_short.empty() &&
                                           friend_short == from_field) {
                                    is_friend = true;
                                } else if (from_field &&
                                           (!my_short.empty() &&
                                            my_short == from_field)) {
                                    is_me = true;
                                } else if (from_field &&
                                           (!my_login.empty() &&
                                            my_login == from_field)) {
                                    is_me = true;
                                } else if (from_field &&
                                           (!my_username.empty() &&
                                            my_username == from_field)) {
                                    is_me = true;
                                } else if (from_field &&
                                           (!my_name.empty() &&
                                            my_name == from_field)) {
                                    is_me = true;
                                }
                                o["from"] =
                                    is_me ? my_name.c_str() : fid.c_str();
                                const char *mid = m["id"].as<const char *>();
                                if (!mid)
                                    mid = m["message_id"].as<const char *>();
                                if (mid && mid[0] != '\0') o["id"] = mid;
                                const char *created =
                                    m["created_at"].as<const char *>();
                                if (created && created[0] != '\0')
                                    o["created_at"] = created;
                                if (m.containsKey("is_read")) {
                                    o["is_read"] = m["is_read"].as<bool>();
                                }
                            }
                            count = arr.size();
                            std::string outBuf;
                            serializeJson(out, outBuf);
                            deserializeJson(res, outBuf);
                        } else {
                            // Accept legacy shape only when sender/receiver IDs
                            // are absent.
                            bool legacy = false;
                            bool has_sender_receiver = false;
                            if (in["messages"].is<JsonArray>()) {
                                for (JsonObject m :
                                     in["messages"].as<JsonArray>()) {
                                    if (m.containsKey("sender_id") ||
                                        m.containsKey("receiver_id")) {
                                        has_sender_receiver = true;
                                    }
                                    if (m.containsKey("message") &&
                                        m.containsKey("from")) {
                                        legacy = true;
                                    }
                                }
                            }
                            if (legacy && !has_sender_receiver) {
                                std::string outBuf;
                                serializeJson(in, outBuf);
                                deserializeJson(res, outBuf);
                                count =
                                    res["messages"].is<JsonArray>()
                                        ? res["messages"].as<JsonArray>().size()
                                        : 0;
                            } else if (in["messages"].is<JsonArray>()) {
                                // Transform {content,sender_id,receiver_id} ->
                                // {message,from}
                                DynamicJsonDocument out(4096);
                                auto arr = out.createNestedArray("messages");
                                std::string my_id = get_nvs((char *)"user_id");
                                std::string my_name =
                                    get_nvs((char *)"user_name");
                                std::string my_short =
                                    get_nvs((char *)"short_id");
                                std::string my_login =
                                    get_nvs((char *)"login_id");
                                std::string my_username =
                                    get_nvs((char *)"username");
                                if (my_name.empty()) my_name = my_login;
                                if (my_name.empty()) my_name = my_username;
                                if (my_name.empty()) my_name = my_short;
                                if (my_name.empty()) my_name = "me";
                                const std::string friend_id =
                                    !active_friend_id.empty() ? active_friend_id
                                                              : fid;
                                const std::string friend_short =
                                    !active_short_id.empty() ? active_short_id
                                                             : fid;
                                for (JsonObject m :
                                     in["messages"].as<JsonArray>()) {
                                    JsonObject o = arr.createNestedObject();
                                    const char *content =
                                        m["content"].as<const char *>();
                                    const char *msg =
                                        m["message"].as<const char *>();
                                    o["message"] =
                                        content ? content : (msg ? msg : "");
                                    const char *sender =
                                        m["sender_id"].as<const char *>();
                                    const char *receiver =
                                        m["receiver_id"].as<const char *>();
                                    const char *from_field =
                                        m["from"].as<const char *>();
                                    bool is_friend = false;
                                    bool is_me = false;
                                    if (sender && !*sender) sender = nullptr;
                                    if (receiver && !*receiver)
                                        receiver = nullptr;
                                    if (sender && !friend_id.empty() &&
                                        friend_id == sender) {
                                        is_friend = true;
                                    } else if (receiver && !friend_id.empty() &&
                                               friend_id == receiver) {
                                        is_me = true;
                                    } else if (sender && !my_id.empty() &&
                                               my_id == sender) {
                                        is_me = true;
                                    } else if (receiver && !my_id.empty() &&
                                               my_id == receiver) {
                                        is_friend = true;
                                    } else if (from_field &&
                                               !friend_short.empty() &&
                                               friend_short == from_field) {
                                        is_friend = true;
                                    } else if (from_field &&
                                               (!my_short.empty() &&
                                                my_short == from_field)) {
                                        is_me = true;
                                    } else if (from_field &&
                                               (!my_login.empty() &&
                                                my_login == from_field)) {
                                        is_me = true;
                                    } else if (from_field &&
                                               (!my_username.empty() &&
                                                my_username == from_field)) {
                                        is_me = true;
                                    } else if (from_field &&
                                               (!my_name.empty() &&
                                                my_name == from_field)) {
                                        is_me = true;
                                    }
                                    o["from"] =
                                        is_me ? my_name.c_str() : fid.c_str();
                                    const char *mid =
                                        m["id"].as<const char *>();
                                    if (!mid)
                                        mid =
                                            m["message_id"].as<const char *>();
                                    if (mid && mid[0] != '\0') o["id"] = mid;
                                    const char *created =
                                        m["created_at"].as<const char *>();
                                    if (created && created[0] != '\0')
                                        o["created_at"] = created;
                                    if (m.containsKey("is_read")) {
                                        o["is_read"] = m["is_read"].as<bool>();
                                    }
                                }
                                count = arr.size();
                                std::string outBuf;
                                serializeJson(out, outBuf);
                                deserializeJson(res, outBuf);
                            } else {
                                ESP_LOGW(
                                    TAG,
                                    "[BLE] Unexpected JSON shape for messages");
                            }
                        }
                        ESP_LOGI(TAG, "[BLE] Parsed %d message(s) via BLE",
                                 count);
                        return true;
                    }
                    ESP_LOGW(TAG, "[BLE] JSON parse error: %s", err.c_str());
                }
                if ((waited % 1000) == 0) {
                    ESP_LOGI(TAG, "[BLE] Waiting for sync response... %d ms",
                             waited);
                }
                vTaskDelay(50 / portTICK_PERIOD_MS);
                waited += 50;
            }
            ESP_LOGW(TAG, "[BLE] Timeout awaiting history (friend=%s)",
                     fid.c_str());
            return false;
        };

        // Allow more time for phone app to prepare response
        bool got_ble = fetch_messages_via_ble(chat_to, 6000);
        if (!got_ble) {
            ESP_LOGW(TAG, "[BLE] Falling back to HTTP history fetch for %s",
                     chat_to.c_str());
            ESP_LOGI(TAG,
                     "[HTTP] Fetching history via Wi-Fi (free=%u largest=%u)",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                       MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            sprite.deleteSprite();
            res = http_client.get_message(server_chat_id);
            (void)recreate_message_sprite(lcd.width(), lcd.height());
        }

        std::string my_name = get_nvs((char *)"user_name");
        if (my_name.empty()) my_name = get_nvs((char *)"login_id");
        if (my_name.empty()) my_name = get_nvs((char *)"short_id");
        if (my_name.empty()) my_name = "me";
        std::string morse_header = !chat_title.empty() ? chat_title : chat_to;
        view_state.my_name = my_name;
        view_state.header_text = chat_title;
        bool mark_all_done = false;

        if (res["messages"].is<JsonArray>()) {
            for (JsonObject msg : res["messages"].as<JsonArray>()) {
                bool unread = true;
                if (msg.containsKey("is_read")) {
                    unread = !msg["is_read"].as<bool>();
                }
                if (!unread) continue;
                const char *from = msg["from"].as<const char *>();
                if (from && !my_name.empty() && my_name == from) {
                    continue;  // 自分の送信分はスキップ
                }
                const char *content = msg["message"].as<const char *>();
                if (!content || content[0] == '\0') {
                    continue;
                }
                if (!mark_all_done) {
                    mark_all_done =
                        http_client.mark_all_messages_read(server_chat_id);
                }
                play_morse_message(content, morse_header);
                msg["is_read"] = true;
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }

        // 通知を非表示
        // http.notif_flag = false;

        ui::messagebox::Presenter presenter(view_state);
        ui::messagebox::Renderer renderer;
        ui::messagebox::RenderApi render_api;
        render_api.screen_height = lcd.height();
        render_api.begin_frame = [&]() { sprite.fillRect(0, 0, 128, 64, 0); };
        render_api.draw_prefix = [&](int cursor_y, bool incoming, int font_height) {
            if (incoming) {
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                sprite.drawBitmap(0, cursor_y + 2, recv_icon2, 13, 12, TFT_BLACK,
                                  TFT_WHITE);
            } else {
                sprite.setTextColor(0x000000u, 0xFFFFFFu);
                sprite.fillRect(0, cursor_y, 128, font_height, 0xFFFF);
                sprite.drawBitmap(0, cursor_y + 2, send_icon2, 13, 12, TFT_WHITE,
                                  TFT_BLACK);
            }
            sprite.setCursor(14, cursor_y);
        };
        render_api.draw_text = [&](int, const std::string &message) {
            size_t pos = 0;
            while (pos < message.length()) {
                uint8_t c = static_cast<uint8_t>(message[pos]);
                size_t char_len = utf8_char_length(c);
                if (char_len == 0 || pos + char_len > message.size()) {
                    char_len = 1;
                }
                std::string ch = message.substr(pos, char_len);
                if (ch.size() >= 2 && (uint8_t)ch[0] == 0xE3 &&
                    ((uint8_t)ch[1] == 0x82 || (uint8_t)ch[1] == 0x83)) {
                    sprite.setFont(&fonts::lgfxJapanGothic_12);
                } else {
                    sprite.setFont(&fonts::Font2);
                }
                sprite.print(ch.c_str());
                pos += char_len;
            }
        };
        render_api.draw_header =
            [&](const std::string &header_text, const std::string &chat_to_text) {
                sprite.fillRect(0, 0, 128, 14, 0);
                sprite.setCursor(0, 0);
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                sprite.setFont(&fonts::Font2);
                if (!header_text.empty()) {
                    sprite.print(header_text.c_str());
                } else {
                    sprite.print(chat_to_text.c_str());
                }
                sprite.drawFastHLine(0, 14, 128, 0xFFFF);
                sprite.drawFastHLine(0, 15, 128, 0);
            };
        render_api.present = [&]() { push_sprite_safe(0, 0); };

        auto rebuild_message_view = [&](bool jump_to_bottom) {
            view_state.message_views.clear();
            if (res["messages"].is<JsonArray>()) {
                for (JsonObject msg : res["messages"].as<JsonArray>()) {
                    view_state.message_views.push_back({msg});
                }
                std::stable_sort(
                    view_state.message_views.begin(), view_state.message_views.end(),
                    [](const ui::messagebox::ViewEntry &a,
                       const ui::messagebox::ViewEntry &b) {
                        const char *created_a =
                            a.obj["created_at"] | static_cast<const char *>("");
                        const char *created_b =
                            b.obj["created_at"] | static_cast<const char *>("");
                        int cmp = strcmp(created_a, created_b);
                        if (cmp == 0) {
                            const char *id_a =
                                a.obj["id"] | static_cast<const char *>("");
                            const char *id_b =
                                b.obj["id"] | static_cast<const char *>("");
                            return strcmp(id_a, id_b) < 0;
                        }
                        return cmp < 0;
                    });
            }
            view_state.min_offset_y =
                (int)((int)view_state.font_height * 2 -
                      (int)view_state.message_views.size() * view_state.font_height);
            if (view_state.min_offset_y > 0) view_state.min_offset_y = 0;
            if (jump_to_bottom || view_state.offset_y < view_state.min_offset_y) {
                view_state.offset_y = view_state.min_offset_y;
            } else if (view_state.offset_y > view_state.max_offset_y) {
                view_state.offset_y = view_state.max_offset_y;
            }
        };

        auto newest_incoming_info = [&](const JsonDocument &doc)
            -> std::pair<std::string, std::string> {
            std::string latest_created;
            std::string latest_id;
            std::string latest_content;
            bool found = false;
            if (!doc["messages"].is<JsonArrayConst>()) {
                return {"", ""};
            }
            for (JsonObjectConst msg : doc["messages"].as<JsonArrayConst>()) {
                const char *from = msg["from"].as<const char *>();
                if (!from || my_name == from) continue;
                const char *created = msg["created_at"].as<const char *>();
                const char *id = msg["id"].as<const char *>();
                if (!id) id = msg["message_id"].as<const char *>();
                const char *content = msg["message"].as<const char *>();
                const std::string created_s = created ? created : "";
                const std::string id_s = id ? id : "";
                if (!found || created_s > latest_created ||
                    (created_s == latest_created && id_s > latest_id)) {
                    latest_created = created_s;
                    latest_id = id_s;
                    latest_content = content ? content : "";
                    found = true;
                }
            }
            if (!found) return {"", ""};
            return {latest_created + "|" + latest_id + "|" + latest_content,
                    latest_content};
        };

        auto refresh_history = [&](int ble_timeout_ms,
                                   bool animate_on_new) -> bool {
            JsonDocument refreshed;
            bool ok = false;
            if (fetch_messages_via_ble(chat_to, ble_timeout_ms)) {
                refreshed = res;
                ok = true;
            } else {
                if (!wifi_is_connected()) {
                    ESP_LOGW(TAG, "[HTTP] Skip refresh: Wi-Fi disconnected");
                    return false;
                }
                ESP_LOGI(TAG, "[HTTP] Refresh history via Wi-Fi");
                sprite.deleteSprite();
                refreshed = http_client.get_message(server_chat_id);
                if (!recreate_message_sprite(lcd.width(), lcd.height())) {
                    ESP_LOGE(TAG, "[UI] message sprite recreate failed after HTTP");
                    running_flag = false;
                    task_handle_ = nullptr;
                    vTaskDelete(NULL);
                    return false;
                }
                ok = true;
            }
            if (!ok) return false;

            const auto before_info = newest_incoming_info(res);
            const auto after_info = newest_incoming_info(refreshed);
            const std::string &before_sig = before_info.first;
            const std::string &after_sig = after_info.first;
            const std::string &after_content = after_info.second;
            res = std::move(refreshed);
            rebuild_message_view(/*jump_to_bottom=*/false);
            if (animate_on_new && !after_sig.empty() &&
                after_sig != before_sig) {
                play_morse_message(after_content.c_str(), morse_header);
                rebuild_message_view(/*jump_to_bottom=*/true);
            }
            last_history_poll_us = esp_timer_get_time();
            return true;
        };

        rebuild_message_view(/*jump_to_bottom=*/true);
        // 初回取得直後に即ポーリングしないよう、基準時刻を現在に揃える
        last_history_poll_us = esp_timer_get_time();

        while (true) {
            Joystick::joystick_state_t joystick_state =
                joystick.get_joystick_state();
            Button::button_state_t type_button_state =
                type_button.get_button_state();
            Button::button_state_t back_button_state =
                back_button.get_button_state();

            ui::InputSnapshot input;
            input.up_edge = joystick_state.pushed_up_edge;
            input.down_edge = joystick_state.pushed_down_edge;
            input.left_edge = joystick_state.pushed_left_edge;
            input.right_edge = joystick_state.pushed_right_edge;
            input.type_pressed = type_button_state.pushed;
            input.back_pressed = back_button_state.pushed;
            input.enter_pressed = false;

            ui::messagebox::Presenter::Command command =
                presenter.handle_input(input);
            if (command == ui::messagebox::Presenter::Command::Exit) {
                break;
            }
            if (command == ui::messagebox::Presenter::Command::Compose) {
                sprite.deleteSprite();
                res.clear();
                res = JsonDocument();
                const bool sent = talk.start_talk_task(chat_to);
                res = JsonDocument();
                if (!recreate_message_sprite(lcd.width(), lcd.height())) {
                    running_flag = false;
                    task_handle_ = nullptr;
                    vTaskDelete(NULL);
                    return;
                }
                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
                // 未取得の一覧を見せないため、再取得完了までローディング表示
                sprite.fillRect(0, 0, 128, 64, 0);
                sprite.setFont(&fonts::Font4);
                sprite.setCursor(10, 20);
                sprite.print("Loading...");
                push_sprite_safe(0, 0);
                // 再取得（BLE優先）。送信直後は必ず履歴画面へ戻り、末尾に追従。
                (void)refresh_history(4000, /*animate_on_new=*/false);
                if (sent) {
                    rebuild_message_view(/*jump_to_bottom=*/true);
                }
            }

            const int64_t now_us = esp_timer_get_time();
            if (presenter.should_poll(now_us, last_history_poll_us,
                                      kHistoryPollIntervalUs)) {
                last_history_poll_us = now_us;
                (void)refresh_history(2000, /*animate_on_new=*/true);
            }

            presenter.clamp_offset();
            renderer.render(view_state, render_api);

            // チャタリング防止用に100msのsleep2
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        running_flag = false;
        active_short_id.clear();
        active_friend_id.clear();
        task_handle_ = nullptr;
        vTaskDelete(NULL);
    };
};
bool MessageBox::running_flag = false;
std::string MessageBox::chat_title = "";
std::string MessageBox::active_short_id = "";
std::string MessageBox::active_friend_id = "";
TaskHandle_t MessageBox::task_handle_ = nullptr;
StaticTask_t MessageBox::task_buffer_;
StackType_t MessageBox::task_stack_[MessageBox::kTaskStackWords] = {};

inline std::string resolve_chat_backend_id(const std::string &fallback) {
    return MessageBox::backend_identifier(fallback);
}

// Forward declaration for helper proxy
std::string wifi_input_info_proxy(std::string input_type,
                                  std::string type_text = "");

#define CONTACT_SIZE 5
