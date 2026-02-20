#pragma once
#include "ui/open_chat/open_chat_mvp.hpp"

class OpenChat {
   public:
    static bool running_flag;
    static constexpr uint32_t kTaskStackWords = 9216;

    void start_open_chat_task();

   private:
    struct RoomOption {
        const char *label;
        const char *topic_suffix;
    };

    struct ChatMessage {
        std::string user;
        std::string short_id;
        std::string text;
        uint64_t ts = 0;
        bool mine = false;
    };

    static TaskHandle_t task_handle_;
    static StaticTask_t task_buffer_;
    static StackType_t *task_stack_;

    static bool compose_morse_message(std::string &out,
                                      const std::string &header);
    static bool recreate_room_sprite();
    static void open_chat_task(void *pvParameters);
};

bool OpenChat::running_flag = false;
TaskHandle_t OpenChat::task_handle_ = nullptr;
StaticTask_t OpenChat::task_buffer_;
StackType_t *OpenChat::task_stack_ = nullptr;

bool OpenChat::recreate_room_sprite() {
    return ensure_sprite_surface(lcd.width(), lcd.height(), 8, "OpenChat");
}

bool OpenChat::compose_morse_message(std::string &out,
                                     const std::string &header) {
    Button type_button(GPIO_NUM_46);
    Button back_button(GPIO_NUM_3);
    Button enter_button(GPIO_NUM_5);
    Joystick joystick;

    auto &buzzer = audio::speaker();
    buzzer.init();
    bool tone_playing = false;

    ui::openchat::ComposerViewState view_state;
    view_state.header = header;
    ui::openchat::ComposerPresenter presenter(view_state);
    ui::openchat::ComposerRenderer renderer;

    const std::string &short_push_text = TalkDisplay::short_push_text;
    const std::string &long_push_text = TalkDisplay::long_push_text;
    const auto &morse_map = TalkDisplay::morse_code;

    if (!recreate_room_sprite()) {
        buzzer.deinit();
        return false;
    }

    const ui::openchat::ComposerRenderApi render_api{
        .begin_frame =
            [&]() {
                sprite.fillRect(0, 0, lcd.width(), lcd.height(), 0);
                sprite.setFont(&fonts::Font2);
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
            },
        .draw_header =
            [&](const std::string &text) {
                sprite.drawCenterString(text.c_str(), 64, 0);
                sprite.drawFastHLine(0, 12, 128, 0xFFFF);
            },
        .draw_message =
            [&](const std::string &message_text) {
                sprite.setCursor(0, 16);
                sprite.setTextWrap(true);
                sprite.print(message_text.c_str());
                sprite.setTextWrap(false);
            },
        .draw_morse =
            [&](const std::string &morse_text, const std::string &preview) {
                sprite.setCursor(0, 44);
                if (!morse_text.empty()) sprite.print(morse_text.c_str());
                if (!preview.empty()) {
                    sprite.setCursor(0, 52);
                    sprite.print(preview.c_str());
                }
            },
        .draw_footer =
            [&](const std::string &footer) {
                sprite.setCursor(0, 56);
                sprite.print(footer.c_str());
            },
        .present = [&]() { push_sprite_safe(0, 0); }};

    renderer.render(view_state, render_api);
    bool result = false;
    while (true) {
        auto joystick_state = joystick.get_joystick_state();
        auto type_state = type_button.get_button_state();
        auto back_state = back_button.get_button_state();
        auto enter_state = enter_button.get_button_state();

        if (type_state.push_edge && !back_state.pushing) {
            if (!tone_playing) {
                if (buzzer.start_tone(2300.0f, 0.6f) == ESP_OK) {
                    tone_playing = true;
                }
            }
        }

        if (!type_state.pushing && tone_playing) {
            buzzer.stop_tone();
            tone_playing = false;
        }

        if (joystick_state.left && type_state.pushed) {
            presenter.handle_delete();
            type_button.clear_button_state();
            renderer.render(view_state, render_api);
        } else if (type_state.pushed && !back_state.pushing) {
            presenter.handle_type_push(type_state.push_type, back_state.pushing,
                                       short_push_text, long_push_text);
            type_button.clear_button_state();
            renderer.render(view_state, render_api);
        }

        if (presenter.resolve_morse_release(type_state.pushing,
                                            type_state.release_sec, morse_map)) {
            type_button.reset_timer();
            renderer.render(view_state, render_api);
        }

        if (joystick_state.pushed_down_edge) {
            presenter.handle_down();
            renderer.render(view_state, render_api);
        }
        if (joystick_state.pushed_up_edge) {
            presenter.handle_up();
            renderer.render(view_state, render_api);
        }

        if (back_state.pushing && type_state.pushed) {
            presenter.handle_delete();
            type_button.clear_button_state();
            renderer.render(view_state, render_api);
        } else if (back_state.pushed) {
            ui::InputSnapshot back_input{};
            back_input.back_pressed = true;
            if (presenter.resolve_command(back_input) !=
                ui::openchat::ComposerCommand::Cancel) {
                presenter.handle_delete();
                back_button.clear_button_state();
                renderer.render(view_state, render_api);
                continue;
            }
            back_button.clear_button_state();
            buzzer.stop_tone();
            buzzer.deinit();
            return false;
        }

        if (enter_state.pushed) {
            ui::InputSnapshot enter_input{};
            enter_input.enter_pressed = true;
            if (presenter.resolve_command(enter_input) !=
                ui::openchat::ComposerCommand::Confirm) {
                enter_button.clear_button_state();
                continue;
            }
            out = view_state.message_text;
            result = true;
            enter_button.clear_button_state();
            break;
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    buzzer.stop_tone();
    buzzer.deinit();
    return result;
}

void OpenChat::open_chat_task(void *pvParameters) {
    (void)pvParameters;
    HttpClient &http_client = HttpClient::shared();
    http_client.start_notifications();
    mqtt_rt_resume();

    const auto creds = chatapi::load_credentials_from_nvs();
    std::string username = creds.username;
    if (username.empty()) username = "Anonymous";
    std::string short_id = get_nvs((char *)"short_id");
    if (!short_id.empty() && short_id.back() == '\0') short_id.pop_back();

    constexpr RoomOption rooms[] = {
        {"430.10", "430_10"},
        {"430.20", "430_20"},
        {"144.64", "144_64"},
        {"HF", "hf_general"},
    };

    Button type_button(GPIO_NUM_46);
    Button back_button(GPIO_NUM_3);
    Button enter_button(GPIO_NUM_5);
    Joystick joystick;

    bool exit_all = false;
    ui::openchat::RoomSelectorViewState selector_state;
    selector_state.rooms.reserve(sizeof(rooms) / sizeof(RoomOption));
    for (const auto &room : rooms) {
        selector_state.rooms.push_back(room.label);
    }
    ui::openchat::RoomSelectorPresenter selector_presenter(selector_state);
    ui::openchat::RoomSelectorRenderer selector_renderer;
    const ui::openchat::RoomSelectorRenderApi selector_api{
        .begin_frame =
            [&]() {
                if (!recreate_room_sprite()) return;
                sprite.fillRect(0, 0, lcd.width(), lcd.height(), 0);
                sprite.setFont(&fonts::Font2);
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
            },
        .draw_header =
            [&](const std::string &title) {
                sprite.drawCenterString(title.c_str(), 64, 0);
                sprite.drawFastHLine(0, 12, 128, 0xFFFF);
            },
        .draw_row =
            [&](int index, const std::string &label, bool selected_row) {
                const int room_height = 12;
                const int y = 16 + index * room_height;
                if (selected_row) {
                    sprite.fillRect(0, y - 2, 128, room_height, 0xFFFF);
                    sprite.setTextColor(0x000000u, 0xFFFFFFu);
                } else {
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                }
                sprite.setCursor(4, y);
                sprite.print(label.c_str());
            },
        .draw_footer =
            [&](const std::string &footer) {
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                sprite.setCursor(0, 56);
                sprite.print(footer.c_str());
            },
        .present = [&]() { push_sprite_safe(0, 0); }};

    while (!exit_all && running_flag) {
        // Room selection
        while (running_flag) {
            selector_renderer.render(selector_state, selector_api);
            auto js = joystick.get_joystick_state();
            auto tb = type_button.get_button_state();
            auto bb = back_button.get_button_state();

            ui::InputSnapshot selector_input{};
            selector_input.up_edge = js.pushed_up_edge;
            selector_input.down_edge = js.pushed_down_edge;
            selector_input.type_pressed = tb.pushed;
            selector_input.back_pressed = bb.pushed;
            selector_presenter.move(selector_input);

            const auto selector_cmd =
                selector_presenter.resolve_command(selector_input);
            if (selector_cmd == ui::openchat::RoomSelectorCommand::EnterRoom) {
                type_button.clear_button_state();
                break;
            }

            if (selector_cmd == ui::openchat::RoomSelectorCommand::Exit) {
                back_button.clear_button_state();
                exit_all = true;
                break;
            }

            vTaskDelay(60 / portTICK_PERIOD_MS);
        }

        if (exit_all || !running_flag) break;

        std::string topic =
            std::string("chat/open/") + rooms[selector_state.selected].topic_suffix;
        int listener_id = mqtt_rt_add_listener(topic.c_str());
        if (listener_id < 0) {
            recreate_room_sprite();
            sprite.setFont(&fonts::Font2);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            sprite.drawCenterString("Subscribe failed", 64, 26);
            push_sprite_safe(0, 0);
            vTaskDelay(1200 / portTICK_PERIOD_MS);
            continue;
        }

        std::vector<ChatMessage> messages;
        std::unordered_set<std::string> seen_ids;
        bool stay_in_room = true;

        auto add_message = [&](ChatMessage msg, const std::string &id) {
            if (!id.empty()) {
                if (!seen_ids.insert(id).second) return;
            }
            messages.push_back(std::move(msg));
            if (messages.size() > 30) {
                messages.erase(messages.begin());
            }
        };

        auto draw_room = [&]() {
            if (!recreate_room_sprite()) return;
            sprite.fillRect(0, 0, lcd.width(), lcd.height(), 0);
            sprite.setFont(&fonts::Font2);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            sprite.drawCenterString(rooms[selector_state.selected].label, 64, 0);
            sprite.drawFastHLine(0, 12, 128, 0xFFFF);

            constexpr int kBodyTop = 16;
            constexpr int kBodyBottom = 52;
            constexpr int kBodyWidth = 128;
            const int kLineHeight =
                std::max(12, static_cast<int>(sprite.fontHeight()) + 2);
            const int max_rows =
                std::max(1, (kBodyBottom - kBodyTop + 1) / kLineHeight);

            struct DisplayRow {
                std::string text;
                bool mine = false;
            };

            auto wrap_text = [&](const std::string &src,
                                 int max_width_px) -> std::vector<std::string> {
                std::vector<std::string> lines;
                std::string current;
                for (size_t p = 0; p < src.size();) {
                    int char_len =
                        utf8_char_length(static_cast<unsigned char>(src[p]));
                    if (char_len <= 0) char_len = 1;
                    if (p + static_cast<size_t>(char_len) > src.size()) break;
                    std::string ch =
                        src.substr(p, static_cast<size_t>(char_len));
                    std::string cand = current + ch;
                    if (!current.empty() &&
                        sprite.textWidth(cand.c_str()) > max_width_px) {
                        lines.push_back(current);
                        current = ch;
                    } else {
                        current = cand;
                    }
                    p += static_cast<size_t>(char_len);
                }
                if (!current.empty()) lines.push_back(current);
                if (lines.empty()) lines.push_back("");
                return lines;
            };

            auto normalize_single_line =
                [](const std::string &src) -> std::string {
                std::string out;
                out.reserve(src.size());
                for (char c : src) {
                    if (c == '\r' || c == '\n' || c == '\t') {
                        out.push_back(' ');
                    } else {
                        out.push_back(c);
                    }
                }
                return out;
            };

            std::vector<DisplayRow> rows;
            rows.reserve(max_rows);
            for (int i = static_cast<int>(messages.size()) - 1;
                 i >= 0 && static_cast<int>(rows.size()) < max_rows; --i) {
                const auto &msg = messages[static_cast<size_t>(i)];
                std::string sender =
                    msg.user.empty() ? (msg.short_id.empty() ? std::string("?")
                                                             : msg.short_id)
                                     : msg.user;
                std::string line =
                    normalize_single_line(sender + ": " + msg.text);
                std::vector<std::string> wrapped = wrap_text(line, kBodyWidth);
                int remain = max_rows - static_cast<int>(rows.size());
                if (remain <= 0) break;
                int take = std::min(remain, static_cast<int>(wrapped.size()));
                // Keep message head lines to avoid showing only the tail part.
                for (int w = take - 1; w >= 0; --w) {
                    rows.insert(rows.begin(),
                                {wrapped[static_cast<size_t>(w)], msg.mine});
                }
            }

            int y = kBodyTop;
            for (size_t r = 0; r < rows.size(); ++r) {
                if (y + kLineHeight - 1 > kBodyBottom) {
                    break;
                }
                const auto &row = rows[r];
                if (row.mine) {
                    sprite.setTextColor(0x000000u, 0xFFFFu);
                    sprite.fillRect(0, y - 2, 128, kLineHeight, 0xFFFF);
                } else {
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                }
                sprite.setCursor(0, y);
                sprite.setTextWrap(false);
                sprite.print(row.text.c_str());
                y += kLineHeight;
            }
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            sprite.setCursor(0, 56);
            sprite.print("Enter:Send  Back:Leave");
            push_sprite_safe(0, 0);
        };

        draw_room();

        while (stay_in_room && running_flag) {
            // pump incoming messages
            char buf[512];
            while (mqtt_rt_listener_pop(listener_id, buf, sizeof(buf))) {
                DynamicJsonDocument doc(768);
                if (deserializeJson(doc, buf) != DeserializationError::Ok) {
                    continue;
                }
                ChatMessage msg;
                msg.user = doc["user"].as<const char *>()
                               ? doc["user"].as<const char *>()
                               : "";
                msg.short_id = doc["short_id"].as<const char *>()
                                   ? doc["short_id"].as<const char *>()
                                   : "";
                msg.text = doc["message"].as<const char *>()
                               ? doc["message"].as<const char *>()
                               : "";
                msg.ts = doc["ts"].as<uint64_t>();
                std::string msg_id = doc["id"].as<const char *>()
                                         ? doc["id"].as<const char *>()
                                         : std::string();
                msg.mine = (!short_id.empty() && msg.short_id == short_id);
                if (!msg.mine && !msg.text.empty()) {
                    std::string header =
                        msg.user.empty()
                            ? (msg.short_id.empty() ? std::string("Open")
                                                    : msg.short_id)
                            : msg.user;
                    play_morse_message(msg.text, header);
                }
                add_message(std::move(msg), msg_id);
            }

            draw_room();

            auto js = joystick.get_joystick_state();
            auto tb = type_button.get_button_state();
            auto eb = enter_button.get_button_state();
            auto bb = back_button.get_button_state();

            if (bb.pushed) {
                back_button.clear_button_state();
                stay_in_room = false;
                break;
            }

            bool trigger_compose = false;
            if (eb.pushed) {
                enter_button.clear_button_state();
                trigger_compose = true;
            } else if (tb.pushed) {
                type_button.clear_button_state();
                trigger_compose = true;
            }

            if (trigger_compose) {
                std::string message;
                std::string header =
                    std::string("TX ") + rooms[selector_state.selected].label;
                if (compose_morse_message(message, header) &&
                    !message.empty()) {
                    DynamicJsonDocument payload(256);
                    payload["room"] = rooms[selector_state.selected].topic_suffix;
                    payload["user"] = username;
                    payload["short_id"] = short_id;
                    payload["message"] = message;
                    uint64_t ts = esp_timer_get_time();
                    payload["ts"] = ts;
                    std::string msg_id =
                        short_id.empty() ? std::to_string(ts)
                                         : short_id + "_" + std::to_string(ts);
                    payload["id"] = msg_id;
                    std::string json;
                    serializeJson(payload, json);
                    int mid =
                        mqtt_rt_publish(topic.c_str(), json.c_str(), 1, false);
                    if (mid < 0) {
                        ESP_LOGW("OPEN_CHAT",
                                 "Publish failed (topic=%s err=%d)",
                                 topic.c_str(), mid);
                    }
                    ChatMessage local;
                    local.user = username;
                    local.short_id = short_id;
                    local.text = message;
                    local.ts = ts;
                    local.mine = true;
                    add_message(std::move(local), msg_id);
                    draw_room();
                }
            }

            if (js.pushed_left_edge || js.pushed_right_edge) {
                stay_in_room = false;
                break;
            }

            vTaskDelay(60 / portTICK_PERIOD_MS);
        }

        mqtt_rt_remove_listener(listener_id);
    }

    running_flag = false;
    task_handle_ = nullptr;
    vTaskDelete(NULL);
}

void OpenChat::start_open_chat_task() {
    if (task_handle_) {
        ESP_LOGW("OPEN_CHAT", "Task already running");
        return;
    }
    if (!allocate_internal_stack(task_stack_, kTaskStackWords, "OpenChat")) {
        ESP_LOGE("OPEN_CHAT", "Stack alloc failed");
        running_flag = false;
        return;
    }
    running_flag = true;
    task_handle_ = xTaskCreateStaticPinnedToCore(
        &open_chat_task, "open_chat_task", kTaskStackWords, NULL, 6,
        task_stack_, &task_buffer_, 1);
    if (!task_handle_) {
        ESP_LOGE("OPEN_CHAT", "Failed to start open chat task");
        running_flag = false;
    }
}
