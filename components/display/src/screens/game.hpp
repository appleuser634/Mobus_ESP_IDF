#pragma once

class Game {
   public:
    static constexpr uint32_t kTaskStackWords = 14288;  // 48 KB task stack
    static bool running_flag;

    static TaskHandle_t task_handle_;
    static StaticTask_t task_buffer_;
    static StackType_t task_stack_[kTaskStackWords];
    static bool wdt_registered_;

    void start_game_task() {
        printf("Start Game Task...");
        if (task_handle_) {
            ESP_LOGW("GAME", "Task already running");
            return;
        }
        running_flag = true;
        task_handle_ = xTaskCreateStaticPinnedToCore(
            &game_task, "game_task", kTaskStackWords, NULL, 6, task_stack_,
            &task_buffer_, 1);
        if (!task_handle_) {
            ESP_LOGE("GAME", "Failed to start game task (err=%d)",
                     (int)errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY);
            running_flag = false;
        }
    }

    static std::map<std::string, std::string> morse_code;
    static std::map<std::string, std::string> morse_code_reverse;
    static void game_task(void *pvParameters) {
        (void)pvParameters;
        bool wdt_registered = false;
        esp_err_t wdt_add_err = esp_task_wdt_add(NULL);
        if (wdt_add_err == ESP_OK) {
            wdt_registered = true;
        } else {
            ESP_LOGW("GAME", "esp_task_wdt_add failed: %s",
                     esp_err_to_name(wdt_add_err));
        }
        wdt_registered_ = wdt_registered;

        lcd.init();
        lcd.setRotation(2);
        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font2);
        sprite.setTextWrap(false);
        sprite.createSprite(lcd.width(), lcd.height());

        Joystick joystick;
        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        reset_inputs(joystick, type_button, back_button, enter_button);

        bool exit_task = false;
        while (!exit_task) {
            feed_wdt();
            bool exit_requested = run_morse_trainer(joystick, type_button,
                                                    back_button, enter_button);
            reset_inputs(joystick, type_button, back_button, enter_button);
            if (exit_requested) {
                exit_task = true;
            }
        }

        UBaseType_t watermark_words = uxTaskGetStackHighWaterMark(nullptr);
        ESP_LOGI(
            "GAME", "game_task stack high watermark: %u words (%u bytes)%s",
            static_cast<unsigned int>(watermark_words),
            static_cast<unsigned int>(watermark_words * sizeof(StackType_t)),
            watermark_words == 0 ? " [LOW]" : "");

        running_flag = false;
        task_handle_ = nullptr;
        wdt_registered_ = false;
        if (wdt_registered) {
            esp_err_t del_err = esp_task_wdt_delete(NULL);
            if (del_err != ESP_OK) {
                ESP_LOGW("GAME", "esp_task_wdt_delete failed: %s",
                         esp_err_to_name(del_err));
            }
        }
        vTaskDelete(NULL);
    };

   private:
    static void feed_wdt();

    static bool run_morse_trainer(Joystick &joystick, Button &type_button,
                                  Button &back_button, Button &enter_button) {
        reset_inputs(joystick, type_button, back_button, enter_button);

        auto &buzzer = audio::speaker();
        buzzer.init();

        HapticMotor &haptic = HapticMotor::instance();

        lcd.setRotation(2);

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font4);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height());

        char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

        srand(esp_timer_get_time());
        char random_char = letters[rand() % 26];

        while (1) {
            feed_wdt();
            std::string morse_text = "";
            std::string message_text = "";
            std::string alphabet_text = "";

            std::string long_push_text = "_";
            std::string short_push_text = ".";

            // 問題数
            int n = 10;
            // クリア数
            int c = 0;

            // ゲーム開始時間
            long long int st = esp_timer_get_time();

            // ゲーム終了フラグ
            bool break_flag = false;

            // Play時間を取得
            float p_time = 0;

            // 問題を解き終わるまでループ
            bool tone_active = false;
            while (c < n) {
                feed_wdt();
                sprite.fillRect(0, 0, 128, 64, 0);

                // Joystickの状態を取得
                Joystick::joystick_state_t joystick_state =
                    joystick.get_joystick_state();

                // モールス信号打ち込みキーの判定ロジック
                Button::button_state_t type_button_state =
                    type_button.get_button_state();
                Button::button_state_t back_button_state =
                    back_button.get_button_state();
                Button::button_state_t enter_button_state =
                    enter_button.get_button_state();

                if (type_button_state.push_edge and
                    !back_button_state.pushing) {
                    buzzer.start_tone(2300.0f, 0.6f);
                    tone_active = true;
                }

                // Ensure tone is only active while the key is held.
                if (!type_button_state.pushing && tone_active) {
                    buzzer.stop_tone();
                    buzzer.disable();
                    tone_active = false;
                }

                if (type_button_state.pushed and !back_button_state.pushing) {
                    printf("Button pushed!\n");
                    printf("Pushing time:%lld\n",
                           type_button_state.pushing_sec);
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
                    // printf("Release
                    // time:%lld\n",button_state.release_sec);

                    if (morse_code.count(morse_text)) {
                        alphabet_text = morse_code.at(morse_text);
                    }
                    morse_text = "";
                }
                if (back_button_state.pushed and
                    !back_button_state.pushed_same_time and
                    !type_button_state.pushing) {
                    break_flag = true;
                    break;
                } else if (joystick_state.left) {
                    break_flag = true;
                    break;
                } else if (joystick_state.up and enter_button_state.pushed) {
                    esp_restart();
                } else if (back_button_state.pushed) {
                    back_button.clear_button_state();
                } else if (joystick_state.up) {
                    sprite.setFont(&fonts::Font2);
                    sprite.setCursor(52, 30);

                    std::string key(1, random_char);
                    std::string morse = morse_code_reverse.at(key);

                    sprite.print(morse.c_str());
                }

                printf("random_char is %c\n", random_char);  // 生の値を出力
                printf("random_char morse is %s\n",
                       std::to_string(random_char)
                           .c_str());  // 文字列化したキーを出力

                // 出題の文字と一緒であればcを++
                if (*message_text.c_str() == random_char) {
                    c += 1;
                    random_char = letters[rand() % 26];
                } else if (message_text != "") {
                    haptic.activate();
                    neopixel.set_color(10, 0, 0);
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                    haptic.deactivate();
                    neopixel.set_color(0, 0, 0);
                }

                message_text = "";

                std::string display_text =
                    message_text + morse_text + alphabet_text;

                std::string strN = std::to_string(n);
                std::string strC = std::to_string(c);

                std::string nPerC = strC + "/" + strN;

                // Play時間を取得
                p_time = round((esp_timer_get_time() - st) / 10000) / 100;

                char b_p_time[50];

                std::sprintf(b_p_time, "%.2f", p_time);
                std::string s_p_time(b_p_time);

                // Play時間を表示
                sprite.setFont(&fonts::Font2);
                sprite.setCursor(0, 0);
                sprite.print(s_p_time.c_str());

                // 回答進捗を表示
                sprite.setCursor(88, 0);
                sprite.print(nPerC.c_str());

                // sprite.setFont(&fonts::Font4);
                sprite.setFont(&fonts::FreeMono12pt7b);
                sprite.drawCenterString(display_text.c_str(), 64, 36,
                                        &fonts::FreeMono12pt7b);

                sprite.setCursor(55, 5);
                sprite.print(random_char);

                sprite.drawFastHLine(0, 16, 48, 0xFFFF);
                sprite.drawFastHLine(78, 16, 128, 0xFFFF);
                sprite.drawRect(50, 2, 26, 26, 0xFFFF);

                push_sprite_safe(0, 0);

                message_text += alphabet_text;
                alphabet_text = "";

                // チャタリング防止用に100msのsleep
                vTaskDelay(10 / portTICK_PERIOD_MS);

                printf("message_text:%s\n", message_text.c_str());
            }

            // break_flagが立ってたら終了
            if (break_flag) {
                if (tone_active) {
                    buzzer.stop_tone();
                    buzzer.disable();
                    tone_active = false;
                }
                buzzer.stop_tone();
                buzzer.deinit();
                break;
            }

            // Play時間を取得
            p_time = round((esp_timer_get_time() - st) / 10000) / 100;
            char b_p_time[50];
            std::sprintf(b_p_time, "%.2f", p_time);
            std::string s_p_time(b_p_time);

            std::string best_record = get_nvs("morse_score");
            if (best_record == "") {
                best_record = "1000";
            }
            float best_record_f = std::stof(best_record);

            sprite.fillRect(0, 0, 128, 64, 0);

            sprite.setFont(&fonts::Font4);
            sprite.setCursor(32, 0);
            sprite.print("Clear!");

            printf("best time: %s", best_record.c_str());
            printf("time: %s", s_p_time.c_str());

            if (std::stof(s_p_time) < best_record_f) {
                save_nvs("morse_score", s_p_time);

                sprite.setFont(&fonts::Font2);

                std::string t_text = "New Record!!";
                sprite.drawCenterString(t_text.c_str(), 64, 22);

                t_text = "Time: " + s_p_time + "s";
                sprite.drawCenterString(t_text.c_str(), 64, 38);
            } else {
                sprite.setFont(&fonts::Font2);

                sprite.setTextColor(0x000000u, 0xFFFFFFu);

                std::string t_text = " BestTime: " + best_record + "s ";
                sprite.drawCenterString(t_text.c_str(), 64, 38);

                sprite.setTextColor(0xFFFFFFu, 0x000000u);

                t_text = "Time: " + s_p_time + "s";
                sprite.drawCenterString(t_text.c_str(), 64, 22);
            }
            // Play時間を表示
            push_sprite_safe(0, 0);

            while (1) {
                feed_wdt();
                Joystick::joystick_state_t joystick_state =
                    joystick.get_joystick_state();
                Button::button_state_t type_button_state =
                    type_button.get_button_state();
                Button::button_state_t back_button_state =
                    back_button.get_button_state();

                // ジョイスティック左を押されたらメニューへ戻る
                // 戻るボタンを押されたらメニューへ戻る
                if (joystick_state.left || back_button_state.pushed) {
                    break_flag = true;
                    break;
                }

                // タイプボタンを押されたら再度ゲームを再開
                if (type_button_state.pushed) {
                    break_flag = false;
                    type_button.clear_button_state();
                    break;
                }

                vTaskDelay(10 / portTICK_PERIOD_MS);
            }

            if (break_flag) {
                break;
            }
        }

        buzzer.stop_tone();
        buzzer.deinit();
        reset_inputs(joystick, type_button, back_button, enter_button);
        return true;
    }

    static void reset_inputs(Joystick &joystick, Button &type_button,
                             Button &back_button, Button &enter_button) {
        type_button.clear_button_state();
        type_button.reset_timer();
        back_button.clear_button_state();
        back_button.reset_timer();
        enter_button.clear_button_state();
        enter_button.reset_timer();
        joystick.reset_timer();
    }

    static char random_letter() {
        static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        const size_t count = sizeof(letters) - 1;
        uint32_t r = esp_random();
        return letters[r % count];
    }
};
bool Game::running_flag = false;
TaskHandle_t Game::task_handle_ = nullptr;
StaticTask_t Game::task_buffer_;
StackType_t Game::task_stack_[Game::kTaskStackWords] = {};
bool Game::wdt_registered_ = false;

void Game::feed_wdt() {
    if (!wdt_registered_) return;
    esp_err_t r = esp_task_wdt_reset();
    if (r != ESP_OK) {
        ESP_LOGW("GAME", "esp_task_wdt_reset failed: %s", esp_err_to_name(r));
    }
}

std::map<std::string, std::string> Game::morse_code = {
    {"._", "A"},     {"_...", "B"},   {"_._.", "C"},   {"_..", "D"},
    {".", "E"},      {".._.", "F"},   {"__.", "G"},    {"....", "H"},
    {"..", "I"},     {".___", "J"},   {"_._", "K"},    {"._..", "L"},
    {"__", "M"},     {"_.", "N"},     {"___", "O"},    {".__.", "P"},
    {"__._", "Q"},   {"._.", "R"},    {"...", "S"},    {"_", "T"},
    {".._", "U"},    {"..._", "V"},   {".__", "W"},    {"_.._", "X"},
    {"_.__", "Y"},   {"__..", "Z"},

    {"._._", " "},

    {"._____", "1"}, {"..___", "2"},  {"...__", "3"},  {"...._", "4"},
    {".....", "5"},  {"_....", "6"},  {"__...", "7"},  {"___..", "8"},
    {"____.", "9"},  {"_____", "0"},

    {"..__..", "?"}, {"_._.__", "!"}, {"._._._", "."}, {"__..__", ","},
    {"_._._.", ";"}, {"___...", ":"}, {"._._.", "+"},  {"_...._", "-"},
    {"_.._.", "/"},  {"_..._", "="},
};

std::map<std::string, std::string> Game::morse_code_reverse = {
    {"A", "._"},     {"B", "_..."},   {"C", "_._."},   {"D", "_.."},
    {"E", "."},      {"F", ".._."},   {"G", "__."},    {"H", "...."},
    {"I", ".."},     {"J", ".___"},   {"K", "_._"},    {"L", "._.."},
    {"M", "__"},     {"N", "_."},     {"O", "___"},    {"P", ".__."},
    {"Q", "__._"},   {"R", "._."},    {"S", "..."},    {"T", "_"},
    {"U", ".._"},    {"V", "..._"},   {"W", ".__"},    {"X", "_.._"},
    {"Y", "_.__"},   {"Z", "__.."},   {"!", "_._.__"}, {".", "._._._"},

    {" ", "._._"},

    {"1", "._____"}, {"2", "..___"},  {"3", "...__"},  {"4", "...._"},
    {"5", "....."},  {"6", "_...."},  {"7", "__..."},  {"8", "___.."},
    {"9", "____."},  {"0", "_____"},

    {"?", "..__.."}, {"!", "_._.__"}, {".", "._._._"}, {",", "__..__"},
    {";", "_._._."}, {":", "___..."}, {"+", "._._."},  {"-", "_...._"},
    {"/", "_.._."},  {"=", "_..._"}};

static void play_morse_message(const std::string &text,
                               const std::string &header, int cx, int cy) {
    auto &buzzer = audio::speaker();
    buzzer.init();

    sprite.setColorDepth(8);
    sprite.setFont(&fonts::Font2);
    sprite.setTextWrap(true);
    sprite.createSprite(lcd.width(), lcd.height());

    const TickType_t dot_ticks = pdMS_TO_TICKS(60);
    const TickType_t dash_ticks = pdMS_TO_TICKS(180);
    const TickType_t intra_symbol_gap = pdMS_TO_TICKS(40);
    const TickType_t letter_gap = pdMS_TO_TICKS(120);

    auto draw_frame = [&](const std::string &morse_part,
                          const std::string &display) {
        sprite.fillRect(0, 0, 128, 64, 0);
        sprite.setFont(&fonts::Font2);
        sprite.setTextColor(0xFFFFFFu, 0x000000u);
        if (!header.empty()) {
            sprite.drawCenterString(header.c_str(), cx, 15);
        }
        const std::string line = display + morse_part;
        int total_w = 0;
        for (size_t p = 0; p < line.size();) {
            int char_len =
                utf8_char_length(static_cast<unsigned char>(line[p]));
            if (char_len <= 0) char_len = 1;
            if (p + (size_t)char_len > line.size()) break;
            std::string ch = line.substr(p, (size_t)char_len);
            sprite.setFont(select_display_font(&fonts::Font2, ch));
            total_w += sprite.textWidth(ch.c_str());
            p += (size_t)char_len;
        }
        int x = cx - (total_w / 2);
        if (x < 0) x = 0;
        sprite.setCursor(x, cy);
        sprite.setTextWrap(false);
        for (size_t p = 0; p < line.size();) {
            int char_len =
                utf8_char_length(static_cast<unsigned char>(line[p]));
            if (char_len <= 0) char_len = 1;
            if (p + (size_t)char_len > line.size()) break;
            std::string ch = line.substr(p, (size_t)char_len);
            sprite.setFont(select_display_font(&fonts::Font2, ch));
            sprite.print(ch.c_str());
            p += (size_t)char_len;
        }
        push_sprite_safe(0, 0);
    };

    auto lookup_morse = [](char c, std::string &out) -> bool {
        std::string key;
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalpha(uc)) {
            key.assign(1, static_cast<char>(std::toupper(uc)));
        } else if (std::isdigit(uc)) {
            key.assign(1, static_cast<char>(uc));
        } else if (std::isspace(uc)) {
            key = " ";
        } else {
            switch (c) {
                case '.':
                case ',':
                case '?':
                case '!':
                case '+':
                case '-':
                case '/':
                case '=':
                case ';':
                case ':':
                    key.assign(1, c);
                    break;
                default:
                    break;
            }
        }
        if (key.empty()) return false;
        auto it = Game::morse_code_reverse.find(key);
        if (it == Game::morse_code_reverse.end()) return false;
        out = it->second;
        return true;
    };

    auto kana_to_romaji =
        []() -> const std::vector<std::pair<std::string, std::string>> & {
        static const std::vector<std::pair<std::string, std::string>> table =
            [] {
                std::unordered_map<std::string, std::string> best;
                for (const auto &kv : TalkDisplay::romaji_kana) {
                    const std::string &romaji = kv.first;
                    const std::string &kana = kv.second;
                    auto it = best.find(kana);
                    if (it == best.end() || romaji.size() < it->second.size()) {
                        best[kana] = romaji;
                    }
                }
                std::vector<std::pair<std::string, std::string>> out;
                out.reserve(best.size());
                for (const auto &kv : best) out.push_back(kv);
                std::sort(out.begin(), out.end(),
                          [](const auto &a, const auto &b) {
                              return a.first.size() > b.first.size();
                          });
                return out;
            }();
        return table;
    };

    std::string display_accum;

    for (size_t idx = 0; idx < text.size();) {
        std::string raw_char;
        std::string romaji_token;
        bool matched_kana = false;
        for (const auto &kv : kana_to_romaji()) {
            if (kv.first.empty() || kv.second.empty()) continue;
            if (idx + kv.first.size() > text.size()) continue;
            if (text.compare(idx, kv.first.size(), kv.first) != 0) continue;
            raw_char = kv.first;
            romaji_token = kv.second;
            idx += kv.first.size();
            matched_kana = true;
            break;
        }
        if (!matched_kana) {
            size_t char_len =
                utf8_char_length(static_cast<unsigned char>(text[idx]));
            if (char_len == 0) char_len = 1;
            raw_char = text.substr(idx, char_len);
            idx += char_len;
        }

        if (raw_char == "\n" || raw_char == "\r") {
            display_accum.push_back(' ');
            draw_frame("", display_accum);
            vTaskDelay(letter_gap);
            continue;
        }

        std::vector<std::string> morse_units;
        if (!romaji_token.empty()) {
            for (char rc : romaji_token) {
                std::string m;
                if (lookup_morse(rc, m)) {
                    morse_units.push_back(m);
                }
            }
        } else if (raw_char.size() == 1) {
            std::string m;
            if (lookup_morse(raw_char[0], m)) morse_units.push_back(m);
        }

        if (morse_units.empty()) {
            display_accum += raw_char;
            draw_frame("", display_accum);
            vTaskDelay(letter_gap);
            continue;
        }

        for (const auto &unit : morse_units) {
            std::string morse_progress;
            for (char symbol : unit) {
                morse_progress.push_back(symbol);
                draw_frame(morse_progress, display_accum);
                TickType_t tone_ticks =
                    (symbol == '.') ? dot_ticks : dash_ticks;
                buzzer.start_tone(2300.0f, 0.6f);
                vTaskDelay(tone_ticks);
                buzzer.stop_tone();
                vTaskDelay(intra_symbol_gap);
            }
            vTaskDelay(letter_gap);
        }

        display_accum += raw_char;
        draw_frame("", display_accum);
    }

    buzzer.stop_tone();
    buzzer.deinit();
    draw_frame("", display_accum);
    vTaskDelay(pdMS_TO_TICKS(200));
}

