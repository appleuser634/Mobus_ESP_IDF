static void play_morse_message(const std::string &text,
                               const std::string &header = "", int cx = 64,
                               int cy = 32);
#include "ui/talk/input_mvp.hpp"

static constexpr char text[] = "MoBus!!";
static constexpr size_t textlen = sizeof(text) / sizeof(text[0]);
size_t textpos = 0;

class TalkDisplay {
   public:
    static int cursor_point;

    static std::string morse_text;
    static std::string message_text;
    static std::string alphabet_text;

    static std::string long_push_text;
    static std::string short_push_text;

    static std::map<std::string, std::string> morse_code;
    static std::vector<std::pair<std::string, std::string>> romaji_kana;

    static int release_time;
    // -1:EN 1:JP
    static int input_lang;

    static void SendAnimation() {
        sprite.fillRect(0, 0, 128, 64, 0);
        sprite.drawBitmap(55, 27, small_elekey_1, 18, 10, TFT_WHITE, TFT_BLACK);
        sprite.fillRect(73, 36, 55, 1, 0xFFFF);
        push_sprite_safe(0, 0);
        vTaskDelay(250 / portTICK_PERIOD_MS);
        sprite.fillRect(0, 0, 128, 64, 0);
        sprite.drawBitmap(55, 27, small_elekey_2, 18, 10, TFT_WHITE, TFT_BLACK);
        sprite.fillRect(73, 36, 55, 1, 0xFFFF);
        push_sprite_safe(0, 0);
        vTaskDelay(250 / portTICK_PERIOD_MS);
        sprite.fillRect(0, 0, 128, 64, 0);
        sprite.drawBitmap(55, 27, small_elekey_1, 18, 10, TFT_WHITE, TFT_BLACK);
        sprite.fillRect(73, 36, 55, 1, 0xFFFF);
        push_sprite_safe(0, 0);

        for (int i = 0; i < 48; i++) {
            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.drawBitmap(55, 27, small_elekey_1, 18, 10, TFT_WHITE,
                              TFT_BLACK);
            sprite.fillRect(73, 36, 55, 1, 0xFFFF);
            sprite.fillRect(80 + i, 34, 2, 2, 0xFFFF);
            push_sprite_safe(0, 0);
            vTaskDelay(15 / portTICK_PERIOD_MS);
        }
        vTaskDelay(250 / portTICK_PERIOD_MS);
    };

    static bool running_flag;

    bool start_talk_task(const std::string &chat_to) {
        if (running_flag) {
            ESP_LOGW(TAG, "talk_task already running");
            return false;
        }
        running_flag = true;
        ESP_LOGI(TAG, "[Talk] inline start (free=%u largest=%u)",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                   MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(
                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        bool ok = run_talk_session(chat_to);
        running_flag = false;
        return ok;
    }

    static bool run_talk_session(const std::string &chat_to) {
        ESP_LOGI(TAG, "[Talk] session start");
        lcd.init();
        lcd.setRotation(0);

        auto &buzzer = audio::speaker();
        buzzer.init();

        Joystick joystick;

        HttpClient &http_client = HttpClient::shared();
        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);
        // Enter long-press threshold: only for entering Save/Load (do not
        // change global default)
        enter_button.long_push_thresh = 300000;  // ~300ms
        const std::string server_chat_id = resolve_chat_backend_id(chat_to);

        lcd.setRotation(2);

        auto ensure_talk_sprite = [&](int width, int height) -> bool {
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
                    ESP_LOGI(TAG,
                             "[UI] talk sprite %dx%d depth=%u psram=%s created",
                             width, height, attempt.depth,
                             attempt.use_psram ? "true" : "false");
                    return true;
                }
                ESP_LOGW(TAG,
                         "[UI] talk sprite alloc fail %dx%d depth=%u psram=%s"
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

        if (!ensure_talk_sprite(lcd.width(), lcd.height())) {
            ESP_LOGE(TAG, "talk_task: sprite allocation failed");
            running_flag = false;
            buzzer.stop_tone();
            buzzer.deinit();
            return false;
        }

        // カーソル点滅制御用タイマー
        long long int t = esp_timer_get_time();

        size_t pos = 0;
        ui::talk::InputViewState input_state;
        input_state.morse_text = morse_text;
        input_state.message_text = message_text;
        input_state.alphabet_text = alphabet_text;
        input_state.input_lang = input_lang;
        input_state.input_switch_pos = 0;
        ui::talk::InputPresenter input_presenter(input_state);

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
            const bool left_plus_type_delete =
                joystick_state.left && type_button_state.pushed;

            if ((!type_button_state.pushing || back_button_state.pushing) &&
                tone_playing) {
                buzzer.stop_tone();
                tone_playing = false;
            }

            if (type_button_state.push_edge && !back_button_state.pushing &&
                !joystick_state.left) {
                if (!tone_playing) {
                    if (buzzer.start_tone(2300.0f, 0.6f) == ESP_OK) {
                        tone_playing = true;
                    }
                }
            }

            if (left_plus_type_delete) {
                input_presenter.delete_last_char();
                type_button.clear_button_state();
                buzzer.stop_tone();
                tone_playing = false;
            } else if (type_button_state.pushed and
                       !back_button_state.pushing) {
                printf("Button pushed!\n");
                printf("Pushing time:%lld\n", type_button_state.pushing_sec);
                printf("Push type:%c\n", type_button_state.push_type);
                input_presenter.handle_type_push(
                    type_button_state.push_type, back_button_state.pushing,
                    short_push_text, long_push_text);

                type_button.clear_button_state();
                buzzer.stop_tone();
                tone_playing = false;
            }

            // printf("Release time:%lld\n",button_state.release_sec);
            input_presenter.decode_release(type_button_state.release_sec,
                                           joystick_state.up, morse_code);
            if (joystick_state.down and type_button_state.pushed) {
                input_presenter.append_newline();
                type_button.clear_button_state();
            }
            if (back_button_state.pushing and type_button_state.pushed) {
                input_presenter.delete_last_char();
                back_button.pushed_same_time();
                type_button.clear_button_state();
            } else if (back_button_state.pushed and
                       !back_button_state.pushed_same_time and
                       !type_button_state.pushing) {
                break;
            } else if (joystick_state.pushed_right_edge) {
                input_presenter.toggle_language();

                sprite.fillRoundRect(52, 24, 24, 18, 2, 0);
                sprite.drawRoundRect(52, 24, 24, 18, 2, 0xFFFF);

                sprite.setFont(&fonts::Font2);
                if (input_state.input_lang == 1) {
                    sprite.drawCenterString("JP", 64, 25);
                } else {
                    sprite.drawCenterString("EN", 64, 25);
                }
                push_sprite_safe(0, 0);
                vTaskDelay(300 / portTICK_PERIOD_MS);
            } else if (back_button_state.pushed) {
                back_button.clear_button_state();
            }

            // Enter(送信)キーの判定ロジック
            if (enter_button_state.pushed and !input_state.message_text.empty()) {
                printf("Button pushed!\n");
                printf("Pushing time:%lld\n", enter_button_state.pushing_sec);
                printf("Push type:%c\n", enter_button_state.push_type);

                ESP_LOGI(TAG, "[Talk] send message to %s (bytes=%zu)",
                         server_chat_id.c_str(), input_state.message_text.size());

                http_client.post_message(server_chat_id, input_state.message_text);
                // Also relay via BLE to phone app if connected
                if (!wifi_is_connected() && ble_uart_is_ready()) {
                    auto esc = [](const std::string &s) {
                        std::string o;
                        o.reserve(s.size() + 8);
                        for (char c : s) {
                            if (c == '\\' || c == '"') {
                                o.push_back('\\');
                                o.push_back(c);
                            } else if (c == '\n') {
                                o += "\\n";
                            } else if (c == '\r') { /* skip */
                            } else {
                                o.push_back(c);
                            }
                        }
                        return o;
                    };
                    long long rid = esp_timer_get_time();
                    std::string json = std::string("{ \"id\":\"") +
                                       std::to_string(rid) +
                                       "\", \"type\": \"send_message\", "
                                       "\"payload\": { \"receiver_id\": \"" +
                                       esc(chat_to) + "\", \"content\": \"" +
                                       esc(input_state.message_text) + "\" } }\n";
                    ble_uart_send(
                        reinterpret_cast<const uint8_t *>(json.c_str()),
                        json.size());
                }
                input_state.message_text = "";
                pos = 0;
                input_state.input_switch_pos = 0;

                SendAnimation();

                enter_button.clear_button_state();
                // 送信後は履歴画面へ戻す
                break;
            }

            std::string display_text =
                input_presenter.display_text((esp_timer_get_time() - t) >= 500000);

            // カーソルの点滅制御用
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

                    sprite.setFont(select_chat_input_font(ch));

                    sprite.print(ch.c_str());
                    pos += char_len;
                } else {
                    break;
                }
            }

            push_sprite_safe(0, 0);

            input_presenter.commit_alphabet(romaji_kana);

            // チャタリング防止用に100msのsleep
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        // 実行フラグをfalseへ変更
        // Ensure I2S is released even if we delete the task (avoid missing C++
        // destructors) so that re-entering Game can re-initialize audio
        // properly.
        std::string().swap(morse_text);
        std::string().swap(message_text);
        std::string().swap(alphabet_text);
        sprite.deleteSprite();
        if (tone_playing) {
            buzzer.stop_tone();
            tone_playing = false;
        }
        buzzer.stop_tone();
        buzzer.deinit();
        ESP_LOGI(TAG, "[Talk] session exit");
        return true;
    };
};

int TalkDisplay::cursor_point = 2;

std::string TalkDisplay::morse_text = "";
std::string TalkDisplay::message_text = "";
std::string TalkDisplay::alphabet_text = "";
std::string TalkDisplay::long_push_text = "_";
std::string TalkDisplay::short_push_text = ".";

std::map<std::string, std::string> TalkDisplay::morse_code = {
    {"._", "a"},     {"_...", "b"},   {"_._.", "c"},   {"_..", "d"},
    {".", "e"},      {".._.", "f"},   {"__.", "g"},    {"....", "h"},
    {"..", "i"},     {".___", "j"},   {"_._", "k"},    {"._..", "l"},
    {"__", "m"},     {"_.", "n"},     {"___", "o"},    {".__.", "p"},
    {"__._", "q"},   {"._.", "r"},    {"...", "s"},    {"_", "t"},
    {".._", "u"},    {"..._", "v"},   {".__", "w"},    {"_.._", "x"},
    {"_.__", "y"},   {"__..", "z"},

    {"._._", " "},

    {"._____", "1"}, {"..___", "2"},  {"...__", "3"},  {"...._", "4"},
    {".....", "5"},  {"_....", "6"},  {"__...", "7"},  {"___..", "8"},
    {"____.", "9"},  {"_____", "0"},

    {"..__..", "?"}, {"_._.__", "!"}, {"._._._", "."}, {"__..__", ","},
    {"_._._.", ";"}, {"___...", ":"}, {"._._.", "+"},  {"_...._", "-"},
    {"_.._.", "/"},  {"_..._", "="},
};

std::vector<std::pair<std::string, std::string>> TalkDisplay::romaji_kana = {
    // 小さい「っ」パターン
    {"kka", "ッカ"},
    {"kki", "ッキ"},
    {"kku", "ック"},
    {"kke", "ッケ"},
    {"kko", "ッコ"},
    {"ssa", "ッサ"},
    {"ssh", "ッシ"},
    {"ssu", "ッス"},
    {"sse", "ッセ"},
    {"sso", "ッソ"},
    {"tta", "ッタ"},
    {"tchi", "ッチ"},
    {"ttsu", "ッツ"},
    {"tte", "ッテ"},
    {"tto", "ット"},
    {"ppa", "ッパ"},
    {"ppi", "ッピ"},
    {"ppu", "ップ"},
    {"ppe", "ッペ"},
    {"ppo", "ッポ"},
    {"cca", "ッカ"},
    {"cci", "ッチ"},
    {"ccu", "ック"},
    {"cce", "ッセ"},
    {"cco", "ッコ"},
    {"mma", "ッマ"},
    {"mmi", "ッミ"},
    {"mmu", "ッム"},
    {"mme", "ッメ"},
    {"mmo", "ッモ"},
    {"nna", "ッナ"},
    {"nni", "ッニ"},
    {"nnu", "ッヌ"},
    {"nne", "ッネ"},
    {"nno", "ッノ"},
    {"rra", "ッラ"},
    {"rri", "ッリ"},
    {"rru", "ッル"},
    {"rre", "ッレ"},
    {"rro", "ッロ"},
    {"bba", "ッバ"},
    {"bbi", "ッビ"},
    {"bbu", "ッブ"},
    {"bbe", "ッベ"},
    {"bbo", "ッボ"},
    {"gga", "ッガ"},
    {"ggi", "ッギ"},
    {"ggu", "ッグ"},
    {"gge", "ッゲ"},
    {"ggo", "ッゴ"},
    {"zza", "ッザ"},
    {"zzi", "ッジ"},
    {"zzu", "ッズ"},
    {"zze", "ッゼ"},
    {"zzo", "ッゾ"},
    {"dda", "ッダ"},
    {"ddi", "ッヂ"},
    {"ddu", "ッヅ"},
    {"dde", "ッデ"},
    {"ddo", "ッド"},
    {"yya", "ッヤ"},
    {"yyu", "ッユ"},
    {"yyo", "ッヨ"},
    {"wwa", "ッワ"},
    {"wwi", "ッウィ"},
    {"wwe", "ッウェ"},
    {"wwo", "ッヲ"},
    // 拗音との組み合わせ（チャチュチョ等）
    {"ccha", "ッチャ"},
    {"cchu", "ッチュ"},
    {"ccho", "ッチョ"},
    {"ssha", "ッシャ"},
    {"sshu", "ッシュ"},
    {"ssho", "ッショ"},
    {"ppya", "ッピャ"},
    {"ppyu", "ッピュ"},
    {"ppyo", "ッピョ"},
    {"kkya", "ッキャ"},
    {"kkyu", "ッキュ"},
    {"kkyo", "ッキョ"},
    {"gga", "ッガ"},
    {"ggya", "ッギャ"},
    {"ggyu", "ッギュ"},
    {"ggyo", "ッギョ"},

    {"kya", "キャ"},
    {"kyu", "キュ"},
    {"kyo", "キョ"},
    {"gya", "ギャ"},
    {"gyu", "ギュ"},
    {"gyo", "ギョ"},
    {"sha", "シャ"},
    {"shu", "シュ"},
    {"sho", "ショ"},
    {"sya", "シャ"},
    {"syu", "シュ"},
    {"syo", "ショ"},
    {"ja", "ジャ"},
    {"ju", "ジュ"},
    {"jo", "ジョ"},
    {"cha", "チャ"},
    {"chu", "チュ"},
    {"cho", "チョ"},
    {"nya", "ニャ"},
    {"nyu", "ニュ"},
    {"nyo", "ニョ"},
    {"hya", "ヒャ"},
    {"hyu", "ヒュ"},
    {"hyo", "ヒョ"},
    {"bya", "ビャ"},
    {"byu", "ビュ"},
    {"byo", "ビョ"},
    {"pya", "ピャ"},
    {"pyu", "ピュ"},
    {"pyo", "ピョ"},
    {"mya", "ミャ"},
    {"myu", "ミュ"},
    {"myo", "ミョ"},
    {"rya", "リャ"},
    {"ryu", "リュ"},
    {"ryo", "リョ"},

    {"ka", "カ"},
    {"ki", "キ"},
    {"ku", "ク"},
    {"ke", "ケ"},
    {"ko", "コ"},
    {"ga", "ガ"},
    {"gi", "ギ"},
    {"gu", "グ"},
    {"ge", "ゲ"},
    {"go", "ゴ"},
    {"sa", "サ"},
    {"si", "シ"},
    {"su", "ス"},
    {"se", "セ"},
    {"so", "ソ"},
    {"za", "ザ"},
    {"ji", "ジ"},
    {"zu", "ズ"},
    {"ze", "ゼ"},
    {"zo", "ゾ"},
    {"ta", "タ"},
    {"ti", "チ"},
    {"tu", "ツ"},
    {"te", "テ"},
    {"to", "ト"},
    {"da", "ダ"},
    {"xi", "ィ"},
    {"xu", "ゥ"},
    {"xe", "ェ"},
    {"de", "デ"},
    {"do", "ド"},
    {"na", "ナ"},
    {"ni", "ニ"},
    {"nu", "ヌ"},
    {"ne", "ネ"},
    {"no", "ノ"},
    {"ha", "ハ"},
    {"hi", "ヒ"},
    {"fu", "フ"},
    {"he", "ヘ"},
    {"ho", "ホ"},
    {"ba", "バ"},
    {"bi", "ビ"},
    {"bu", "ブ"},
    {"be", "ベ"},
    {"bo", "ボ"},
    {"pa", "パ"},
    {"pi", "ピ"},
    {"pu", "プ"},
    {"pe", "ペ"},
    {"po", "ポ"},
    {"ma", "マ"},
    {"mi", "ミ"},
    {"mu", "ム"},
    {"me", "メ"},
    {"mo", "モ"},
    {"ya", "ヤ"},
    {"yu", "ユ"},
    {"yo", "ヨ"},
    {"ra", "ラ"},
    {"ri", "リ"},
    {"ru", "ル"},
    {"re", "レ"},
    {"ro", "ロ"},
    {"wa", "ワ"},
    {"wo", "ヲ"},
    {"nn", "ン"},

    {"fa", "ファ"},
    {"fi", "フィ"},
    {"fe", "フェ"},
    {"fo", "フォ"},
    {"va", "ヴァ"},
    {"vi", "ヴィ"},
    {"ve", "ヴェ"},
    {"vo", "ヴォ"},
    {"tu", "トゥ"},
    {"ti", "ティ"},
    {"je", "ジェ"},
    {"che", "チェ"},
    {"th", "テャ"},

    {"a", "ア"},
    {"i", "イ"},
    {"u", "ウ"},
    {"e", "エ"},
    {"o", "オ"}};

// std::map<std::string, std::string> TalkDisplay::morse_code = morse_code;
int TalkDisplay::release_time = 0;
int TalkDisplay::input_lang = -1;
bool TalkDisplay::running_flag = false;
