#pragma once

class P2P_Display {
   public:
    int cursor_point = 2;

    std::string morse_text = "";
    std::string message_text = "";
    std::string alphabet_text = "";
    std::string long_push_text = "_";
    std::string short_push_text = ".";

    static std::string received_text;

    // ESP NOW
    // 送信先のMACアドレス
    uint8_t peer_mac[6] = {0xFF, 0xFF, 0xFF,
                           0xFF, 0xFF, 0xFF};  // ブロードキャストも可

    // std::map<std::string, std::string> P2P_Display::morse_code = morse_code;
    int release_time = 0;
    int input_lang = -1;
    bool running_flag = false;

    void SendAnimation() {
        sprite.fillRect(0, 0, 128, 64, 0);

        sprite.setCursor(30, 20);
        sprite.setFont(&fonts::Font4);
        sprite.print("Send!");
        sprite.setFont(&fonts::Font2);
        push_sprite_safe(0, 0);

        vTaskDelay(2000 / portTICK_PERIOD_MS);
    };

    static void espnow_send_cb(const uint8_t *mac_addr,
                               esp_now_send_status_t status) {
        ESP_LOGI(TAG, "送信完了: %s",
                 status == ESP_NOW_SEND_SUCCESS ? "成功" : "失敗");
    }

    void p2p_init() {
        // esp_err_t result = esp_now_init();
        // ESP_LOGE(TAG, "esp_now_send result: %s", esp_err_to_name(result));
        // ESP_ERROR_CHECK(result);

        {
            esp_err_t err = esp_now_init();
            if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
                ESP_ERROR_CHECK(err);
            }
        }
        ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

        esp_now_peer_info_t peerInfo = {
            .channel = 0,
            .ifidx = WIFI_IF_STA,
            .encrypt = false,
        };

        memset(peerInfo.lmk, 0, ESP_NOW_KEY_LEN);  // ← これを追加
        memcpy(peerInfo.peer_addr, peer_mac, 6);
        {
            esp_err_t err = esp_now_add_peer(&peerInfo);
            if (err == ESP_ERR_ESPNOW_EXIST) {
                // Re-entering the realtime chat: peer may already exist.
                // Prefer modify; fall back to delete+add.
                esp_err_t m = esp_now_mod_peer(&peerInfo);
                if (m != ESP_OK) {
                    (void)esp_now_del_peer(peerInfo.peer_addr);
                    ESP_ERROR_CHECK(esp_now_add_peer(&peerInfo));
                }
            } else {
                ESP_ERROR_CHECK(err);
            }
        }

        esp_wifi_set_max_tx_power(84);

        espnow_recv();
    }

    void espnow_send(std::string message) {
        if (message == "") {
            return;
        }
        esp_err_t err = esp_now_send(peer_mac, (uint8_t *)message.c_str(),
                                     strlen(message.c_str()));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_now_send failed: %s", esp_err_to_name(err));
        }
    }

    static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                               const uint8_t *data, int len) {
        if (data && len > 0) {
            received_text =
                std::string(reinterpret_cast<const char *>(data), len);
            ESP_LOGI(TAG, "受信: %s", received_text.c_str());
        } else {
            ESP_LOGW(TAG, "無効なデータ受信");
        }
    }

    void espnow_recv(void) {
        esp_err_t err = esp_now_register_recv_cb(espnow_recv_cb);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_now_register_recv_cb failed: %s",
                     esp_err_to_name(err));
        }
    }

    void morse_p2p() {
        p2p_init();

        auto &buzzer = audio::speaker();
        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        auto clear_inputs = [&]() {
            type_button.clear_button_state();
            type_button.reset_timer();
            back_button.clear_button_state();
            back_button.reset_timer();
            enter_button.clear_button_state();
            enter_button.reset_timer();
            joystick.reset_timer();
        };

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font2);

        // sprite.setFont(&fonts::Font2);
        // sprite.setFont(&fonts::FreeMono9pt7b);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height());

        // カーソル点滅制御用タイマー
        long long int t = esp_timer_get_time();

        size_t input_switch_pos = 0;
        size_t pos = 0;
        bool tone_playing = false;

        while (true) {
            Joystick::joystick_state_t joystick_state =
                joystick.get_joystick_state();

            // モールス信号打ち込みキーの判定ロジック
            Button::button_state_t type_button_state =
                type_button.get_button_state();

            Button::button_state_t back_button_state =
                back_button.get_button_state();
            Button::button_state_t enter_button_state =
                enter_button.get_button_state();

            if (type_button_state.push_edge and !back_button_state.pushing) {
                buzzer.start_tone(2300.0f, 0.6f);
                tone_playing = true;
            }

            if (!type_button_state.pushing && tone_playing) {
                buzzer.stop_tone();
                buzzer.disable();
                tone_playing = false;
            }

            if (type_button_state.pushed and !back_button_state.pushing) {
                printf("Button pushed!\n");
                printf("Pushing time:%lld\n", type_button_state.pushing_sec);
                printf("Push type:%c\n", type_button_state.push_type);
                if (type_button_state.push_type == 's') {
                    morse_text += short_push_text;
                } else if (type_button_state.push_type == 'l') {
                    morse_text += long_push_text;
                }

                type_button.clear_button_state();
            }

            // printf("Release time:%lld\n",button_state.release_sec);
            if (type_button_state.release_sec > 200000) {
                // printf("Release time:%lld\n",button_state.release_sec);

                if (TalkDisplay::morse_code.count(morse_text)) {
                    alphabet_text = TalkDisplay::morse_code.at(morse_text);
                }
                if (joystick_state.up) {
                    std::transform(alphabet_text.begin(), alphabet_text.end(),
                                   alphabet_text.begin(), ::toupper);
                }
                morse_text = "";
            }
            if (joystick_state.down and type_button_state.pushed) {
                message_text += "\n";
                type_button.clear_button_state();
            }
            if (back_button_state.pushing and type_button_state.pushed) {
                if (message_text != "") {
                    remove_last_utf8_char(message_text);
                }
                input_switch_pos = message_text.size();
                back_button.pushed_same_time();
                type_button.clear_button_state();
            } else if (back_button_state.pushed and
                       !back_button_state.pushed_same_time and
                       !type_button_state.pushing) {
                if (tone_playing) {
                    buzzer.stop_tone();
                    buzzer.disable();
                    tone_playing = false;
                }
                clear_inputs();
                return;
            } else if (joystick_state.left) {
                if (tone_playing) {
                    buzzer.stop_tone();
                    buzzer.disable();
                    tone_playing = false;
                }
                clear_inputs();
                return;
            } else if (joystick_state.pushed_right_edge) {
                input_lang = input_lang * -1;

                sprite.fillRoundRect(52, 24, 24, 18, 2, 0);
                sprite.drawRoundRect(52, 24, 24, 18, 2, 0xFFFF);

                sprite.setFont(&fonts::Font2);
                if (input_lang == 1) {
                    sprite.drawCenterString("JP", 64, 25);
                } else {
                    sprite.drawCenterString("EN", 64, 25);
                }
                push_sprite_safe(0, 0);
                vTaskDelay(300 / portTICK_PERIOD_MS);

                input_switch_pos = message_text.size();
            } else if (back_button_state.pushed) {
                back_button.clear_button_state();
            }

            // Enter(送信)キーの判定ロジック
            if (enter_button_state.pushed and message_text != "") {
                message_text = "";
                pos = 0;
                input_switch_pos = 0;
                enter_button.clear_button_state();
            }

            std::string display_text =
                message_text + morse_text + alphabet_text;

            // カーソルの点滅制御用
            if (esp_timer_get_time() - t >= 500000) {
                display_text += "|";
            }
            if (esp_timer_get_time() - t > 1000000) {
                t = esp_timer_get_time();
            }

            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.setCursor(0, 0);

            pos = 0;
            while (pos < display_text.length()) {
                uint8_t c = display_text[pos];
                int char_len = 1;
                if ((c & 0xE0) == 0xC0)
                    char_len = 2;
                else if ((c & 0xF0) == 0xE0)
                    char_len = 3;

                if (pos + char_len <= display_text.length()) {
                    std::string ch = display_text.substr(pos, char_len);

                    sprite.setFont(select_display_font(&fonts::Font2, ch));

                    sprite.print(ch.c_str());
                    pos += char_len;
                } else {
                    break;
                }
            }

            // 受信したメッセージを描画
            sprite.drawFastHLine(0, 32, 128, 0xFFFF);
            sprite.setCursor(0, 35);
            {
                size_t rpos = 0;
                while (rpos < received_text.length()) {
                    uint8_t c = received_text[rpos];
                    int char_len = 1;
                    if ((c & 0xE0) == 0xC0)
                        char_len = 2;
                    else if ((c & 0xF0) == 0xE0)
                        char_len = 3;
                    if (rpos + char_len <= received_text.length()) {
                        std::string ch = received_text.substr(rpos, char_len);
                        sprite.setFont(select_display_font(&fonts::Font2, ch));
                        sprite.print(ch.c_str());
                        rpos += char_len;
                    } else {
                        break;
                    }
                }
            }

            push_sprite_safe(0, 0);

            message_text += alphabet_text;
            if (alphabet_text != "" && input_lang == 1) {
                size_t safe_pos = input_switch_pos;
                if (safe_pos > message_text.size())
                    safe_pos = message_text.size();
                std::string translate_targt = message_text.substr(safe_pos);
                for (const auto &pair : TalkDisplay::romaji_kana) {
                    std::cout << "Key: " << pair.first << std::endl;
                    size_t pos = translate_targt.find(pair.first);
                    if (pos != std::string::npos) {
                        translate_targt.replace(pos, pair.first.length(),
                                                pair.second);
                    }
                }
                message_text =
                    message_text.substr(0, safe_pos) + translate_targt;
            }
            alphabet_text = "";

            // チャタリング防止用に100msのsleep

            espnow_send(display_text);
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        buzzer.stop_tone();
        buzzer.deinit();

        // 実行フラグをfalseへ変更
        running_flag = false;
        vTaskDelete(NULL);
    };
};
std::string P2P_Display::received_text = "";

void Profile() {
    printf("Profile!!!\n");
    Joystick joystick;

    Button type_button(GPIO_NUM_46);
    Button back_button(GPIO_NUM_3);
    Button enter_button(GPIO_NUM_5);

    lcd.fillScreen(0x000000u);
    sprite.createSprite(lcd.width(), lcd.height());
    sprite.setTextColor(0xFFFFFFu, 0x000000u);
    sprite.setFont(&fonts::Font2);

    // Prepare data (tokenは表示しない)
    std::string user_name = get_nvs((char *)"user_name");
    std::string friend_code = get_nvs((char *)"friend_code");
    std::string short_id = get_nvs((char *)"short_id");

    // If friend_code is not saved, try to fetch via API and store
    if (friend_code == "") {
        auto &api = chatapi::shared_client(true);
        api.set_scheme("https");
        const auto creds = chatapi::load_credentials_from_nvs();
        if (!user_name.empty()) {
            (void)chatapi::ensure_authenticated(api, creds, false);
        }
        std::string code;
        int st = 0;
        if (api.refresh_friend_code(code, &st) == ESP_OK && !code.empty()) {
            friend_code = code;
        }
    }

    // Layout metrics (avoid label/value overlap across fonts)
    const int block_h = 28;       // total block height per item
    const int value_offset = 14;  // value text offset from label
    int offset_y = 0;
    std::vector<std::pair<std::string, std::string>> lines = {
        {"Name", user_name},
        {"Friend ID", friend_code == "" ? std::string("(none)") : friend_code},
        {"Short ID", short_id == "" ? std::string("(none)") : short_id}};
    auto draw = [&](int off) {
        sprite.fillRect(0, 0, 128, 64, 0);
        int y = 0;
        for (auto &kv : lines) {
            sprite.setCursor(0, y + off);
            sprite.print((kv.first + ":").c_str());
            sprite.setCursor(0, y + off + value_offset);
            std::string val = kv.second;
            if ((int)val.size() > 24) val = val.substr(0, 24) + "...";
            sprite.print(val.c_str());
            y += block_h;
        }
        push_sprite_safe(0, 0);
    };
    draw(offset_y);
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

        // スクロール
        if (joystick_state.pushed_up_edge) {
            offset_y += block_h;
        } else if (joystick_state.pushed_down_edge) {
            offset_y -= block_h;
        }
        int content_h = (int)lines.size() * block_h;
        int min_off = lcd.height() - content_h;
        if (min_off > 0) min_off = 0;
        if (offset_y > 0) offset_y = 0;
        if (offset_y < min_off) offset_y = min_off;
        draw(offset_y);

        // ジョイスティック左/戻るでメニューへ戻る
        if (joystick_state.left || back_button_state.pushed) {
            break;
        }

        type_button.clear_button_state();
        type_button.reset_timer();
        joystick.reset_timer();
        vTaskDelay(1);
    }
}

// Simple Game Boy-like step composer UI (2x Pulse + Noise)
