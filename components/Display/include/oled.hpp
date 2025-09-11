#include <cstdio>
#include <iterator>
#include <string>
#include <cstdlib>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <button.h>
#include <max98357a.h>
#include <images.hpp>
#include <led.hpp>
#include <ctype.h>

#pragma once

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_SSD1306
        _panel_instance;  // SSD1309 は SH110x ドライバで動作可能
    lgfx::Bus_SPI _bus_instance;

   public:
    LGFX(void) {
        {  // SPIバス設定
            auto cfg = _bus_instance.config();

            cfg.spi_host =
                SPI3_HOST;     // ESP32-S3では SPI2_HOST または SPI3_HOST
            cfg.spi_mode = 0;  // SSD1309のSPIモードは0

            cfg.freq_write = 20000000;  // 8MHz程度が安定（モジュールによる）
            cfg.freq_read = 0;          // dev/ 読み取り不要なため 0
            cfg.spi_3wire = false;      // DCピン使用するので false
            cfg.use_lock = true;

            // 接続ピン（あなたの配線に基づく）
            cfg.pin_sclk = 2;   // SCK
            cfg.pin_mosi = 13;  // MOSI
            cfg.pin_miso = -1;  // MISO 使用しない
            cfg.pin_dc = 1;     // DC

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {  // パネル設定
            auto cfg = _panel_instance.config();

            cfg.pin_cs = 11;    // CS
            cfg.pin_rst = 12;   // RST
            cfg.pin_busy = -1;  // BUSYピン未使用

            cfg.panel_width = 128;
            cfg.panel_height = 64;
            cfg.memory_width = 128;
            cfg.memory_height = 64;

            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 2;

            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;  // 読み取り機能なし
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;

            _panel_instance.config(cfg);
        }

        setPanel(&_panel_instance);
    }
};

static LGFX lcd;
static LGFX_Sprite sprite(
    &lcd);  // スプライトを使う場合はLGFX_Spriteのインスタンスを作成。

#include "mopping.h"
#include <chat_api.hpp>
#include <ota_client.hpp>
#include <mqtt_runtime.h>
#include <ble_uart.hpp>
#include "esp_app_desc.h"

// UTF-8 の1文字の先頭バイト数を調べる（UTF-8のみ対応）
int utf8_char_length(unsigned char ch) {
    if ((ch & 0x80) == 0x00) return 1;  // ASCII
    if ((ch & 0xE0) == 0xC0) return 2;  // 2バイト文字
    if ((ch & 0xF0) == 0xE0) return 3;  // 3バイト文字（例：カタカナ）
    if ((ch & 0xF8) == 0xF0) return 4;  // 4バイト文字（絵文字など）
    return 1;
}

void remove_last_utf8_char(std::string &str) {
    if (str.empty()) return;

    // 末尾の先頭バイトを探す
    size_t pos = str.size();
    while (pos > 0) {
        --pos;
        if ((str[pos] & 0xC0) != 0x80) break;  // UTF-8の先頭バイト検出
    }

    str.erase(pos);
}

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
        sprite.pushSprite(&lcd, 0, 0);
        vTaskDelay(250 / portTICK_PERIOD_MS);
        sprite.fillRect(0, 0, 128, 64, 0);
        sprite.drawBitmap(55, 27, small_elekey_2, 18, 10, TFT_WHITE, TFT_BLACK);
        sprite.fillRect(73, 36, 55, 1, 0xFFFF);
        sprite.pushSprite(&lcd, 0, 0);
        vTaskDelay(250 / portTICK_PERIOD_MS);
        sprite.fillRect(0, 0, 128, 64, 0);
        sprite.drawBitmap(55, 27, small_elekey_1, 18, 10, TFT_WHITE, TFT_BLACK);
        sprite.fillRect(73, 36, 55, 1, 0xFFFF);
        sprite.pushSprite(&lcd, 0, 0);

        for (int i = 0; i < 48; i++) {
            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.drawBitmap(55, 27, small_elekey_1, 18, 10, TFT_WHITE,
                              TFT_BLACK);
            sprite.fillRect(73, 36, 55, 1, 0xFFFF);
            sprite.fillRect(80 + i, 34, 2, 2, 0xFFFF);
            sprite.pushSprite(&lcd, 0, 0);
            vTaskDelay(15 / portTICK_PERIOD_MS);
        }
        vTaskDelay(250 / portTICK_PERIOD_MS);
    };

    static bool running_flag;

    void start_talk_task(std::string chat_to) {
        printf("Start Talk Task...");
        // xTaskCreate(&menu_task, "menu_task", 4096, NULL, 6, NULL, 1);
        xTaskCreatePinnedToCore(&talk_task, "talk_task", 4096, &chat_to, 6,
                                NULL, 1);
    }

    static void talk_task(void *pvParameters) {
        lcd.init();
        lcd.setRotation(0);

        Max98357A buzzer;

        Joystick joystick;

        HttpClient http_client;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        std::string chat_to = *(std::string *)pvParameters;

        lcd.setRotation(2);

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
                buzzer.stop_tone();
            }

            // printf("Release time:%lld\n",button_state.release_sec);
            if (type_button_state.release_sec > 200000) {
                // printf("Release time:%lld\n",button_state.release_sec);

                if (morse_code.count(morse_text)) {
                    alphabet_text = morse_code.at(morse_text);
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
                break;
            } else if (joystick_state.left) {
                break;
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
                sprite.pushSprite(&lcd, 0, 0);
                vTaskDelay(300 / portTICK_PERIOD_MS);

                input_switch_pos = message_text.size();
            } else if (back_button_state.pushed) {
                back_button.clear_button_state();
            }

            // Enter(送信)キーの判定ロジック
            if (enter_button_state.pushed and message_text != "") {
                printf("Button pushed!\n");
                printf("Pushing time:%lld\n", enter_button_state.pushing_sec);
                printf("Push type:%c\n", enter_button_state.push_type);

                std::string chat_to_data[] = {chat_to, message_text};
                // std::string chat_to_data = message_text;
                http_client.post_message(chat_to_data);
                // Also relay via BLE to phone app if connected
                if (ble_uart_is_ready()) {
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
                                       esc(message_text) + "\" } }\n";
                    ble_uart_send(
                        reinterpret_cast<const uint8_t *>(json.c_str()),
                        json.size());
                }
                message_text = "";
                pos = 0;
                input_switch_pos = 0;

                SendAnimation();

                enter_button.clear_button_state();
            }

            std::string display_text =
                message_text + morse_text + alphabet_text;

            // カーソルの点滅制御用
            if (esp_timer_get_time() - t >= 500000) {
                display_text += "|";
                printf("Timder!\n");
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

                    if ((uint8_t)ch[0] == 0xE3 &&
                        ((uint8_t)ch[1] == 0x82 || (uint8_t)ch[1] == 0x83)) {
                        sprite.setFont(&fonts::lgfxJapanGothic_12);
                    } else {
                        sprite.setFont(&fonts::Font2);
                    }

                    sprite.print(ch.c_str());
                    pos += char_len;
                } else {
                    break;
                }
            }

            sprite.pushSprite(&lcd, 0, 0);

            message_text += alphabet_text;
            if (alphabet_text != "" && input_lang == 1) {
                size_t safe_pos = input_switch_pos;
                if (safe_pos > message_text.size())
                    safe_pos = message_text.size();
                std::string translate_target = message_text.substr(safe_pos);
                // Longest-match transliteration
                auto transliterate = [](const std::string &src) -> std::string {
                    // Build sorted mapping by key length desc once
                    static std::vector<std::pair<std::string, std::string>>
                        sorted;
                    static bool inited = false;
                    if (!inited) {
                        sorted = romaji_kana;  // copy
                        std::stable_sort(
                            sorted.begin(), sorted.end(), [](auto &a, auto &b) {
                                return a.first.size() > b.first.size();
                            });
                        inited = true;
                    }
                    std::string out;
                    out.reserve(src.size() * 3);
                    size_t i = 0;
                    while (i < src.size()) {
                        bool matched = false;
                        for (auto &kv : sorted) {
                            const std::string &k = kv.first;
                            if (k.size() > 0 && i + k.size() <= src.size() &&
                                src.compare(i, k.size(), k) == 0) {
                                out += kv.second;
                                i += k.size();
                                matched = true;
                                break;
                            }
                        }
                        if (!matched) {
                            out += src[i++];
                        }
                    }
                    return out;
                };
                translate_target = transliterate(translate_target);
                message_text =
                    message_text.substr(0, safe_pos) + translate_target;
            }
            alphabet_text = "";

            // チャタリング防止用に100msのsleep
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        // 実行フラグをfalseへ変更
        running_flag = false;
        vTaskDelete(NULL);
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

class MessageBox {
   public:
    static bool running_flag;
    static std::string chat_title;  // display username for header

    void start_box_task(std::string chat_to) {
        printf("Start Box Task...");
        // xTaskCreate(&menu_task, "menu_task", 4096, NULL, 6, NULL, 1);
        xTaskCreatePinnedToCore(&box_task, "box_task", 8012, &chat_to, 6, NULL,
                                1);
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
        sprite.pushSprite(&lcd, 0, 0);

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font2);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止
        // 安定描画のため画面サイズのスプライトに統一
        sprite.createSprite(lcd.width(), lcd.height());

        HttpClient http_client;

        // メッセージの取得（BLE優先、HTTPフォールバック）
        std::string chat_to = *(std::string *)pvParameters;
        JsonDocument res;
        auto fetch_messages_via_ble = [&](const std::string &fid,
                                          int timeout_ms) -> bool {
            if (!ble_uart_is_ready()) return false;
            long long rid = esp_timer_get_time();
            // Phone app should reply with a frame:
            // {"type":"messages","messages":[...]} stored to NVS as
            // "ble_messages"
            std::string req = std::string("{ \"id\":\"") + std::to_string(rid) +
                              "\", \"type\": \"get_messages\", \"payload\": { "
                              "\"friend_id\": \"" +
                              fid + "\", \"limit\": 20 } }\n";
            ble_uart_send(reinterpret_cast<const uint8_t *>(req.c_str()),
                          req.size());

            int waited = 0;
            while (waited < timeout_ms) {
                std::string js = get_nvs((char *)"ble_messages");
                if (!js.empty()) {
                    StaticJsonDocument<8192> in;
                    if (deserializeJson(in, js) == DeserializationError::Ok) {
                        // Accept legacy shape: {messages:[{message,from},...]}
                        // Or transform server-like shape into legacy
                        bool legacy = false;
                        if (in["messages"].is<JsonArray>()) {
                            for (JsonObject m :
                                 in["messages"].as<JsonArray>()) {
                                if (m.containsKey("message") &&
                                    m.containsKey("from")) {
                                    legacy = true;
                                    break;
                                }
                            }
                        }
                        if (legacy) {
                            // Directly use
                            std::string outBuf;
                            serializeJson(in, outBuf);
                            deserializeJson(res, outBuf);
                        } else if (in["messages"].is<JsonArray>()) {
                            // Transform {content,sender_id,receiver_id} ->
                            // {message,from}
                            StaticJsonDocument<8192> out;
                            auto arr = out.createNestedArray("messages");
                            std::string my_id = get_nvs((char *)"user_id");
                            std::string my_name = get_nvs((char *)"user_name");
                            for (JsonObject m :
                                 in["messages"].as<JsonArray>()) {
                                JsonObject o = arr.createNestedObject();
                                const char *content =
                                    m["content"].as<const char *>();
                                o["message"] = content ? content : "";
                                const char *sender =
                                    m["sender_id"].as<const char *>();
                                if (sender && !my_id.empty() &&
                                    my_id == sender) {
                                    o["from"] = my_name.c_str();
                                } else {
                                    o["from"] = fid.c_str();
                                }
                            }
                            std::string outBuf;
                            serializeJson(out, outBuf);
                            deserializeJson(res, outBuf);
                        }
                        return true;
                    }
                }
                vTaskDelay(50 / portTICK_PERIOD_MS);
                waited += 50;
            }
            return false;
        };

        bool got_ble = fetch_messages_via_ble(chat_to, 2500);
        if (!got_ble) {
            res = http_client.get_message(chat_to);
        }

        // 通知を非表示
        // http.notif_flag = false;

        int font_height = 16;
        // When message count is small, avoid positive min_offset which causes
        // jitter.
        int max_offset_y = 0;
        int min_offset_y = (int)((int)font_height * 2 -
                                 (int)res["messages"].size() * font_height);
        if (min_offset_y > 0) min_offset_y = 0;
        int offset_y = 0;

        while (true) {
            sprite.fillRect(0, 0, 128, 64, 0);
            Joystick::joystick_state_t joystick_state =
                joystick.get_joystick_state();
            Button::button_state_t type_button_state =
                type_button.get_button_state();
            Button::button_state_t back_button_state =
                back_button.get_button_state();

            // 入力イベント
            if (back_button_state.pushed) {
                break;
            } else if (joystick_state.pushed_up_edge) {
                offset_y += font_height;
            } else if (joystick_state.pushed_down_edge) {
                offset_y -= font_height;
            }
            if (type_button_state.pushed) {
                talk.running_flag = true;
                talk.start_talk_task(chat_to);

                while (talk.running_flag) {
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                sprite.setColorDepth(8);
                sprite.setFont(&fonts::Font2);
                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
                // 再取得（BLE優先）
                if (!fetch_messages_via_ble(chat_to, 1500)) {
                    res = http_client.get_message(chat_to);
                }
            }

            if (offset_y > max_offset_y) offset_y = max_offset_y;
            if (offset_y < min_offset_y) offset_y = min_offset_y;

            // 描画処理
            int cursor_y = 0;
            for (int i = 0; i < res["messages"].size(); i++) {
                std::string message(res["messages"][i]["message"]);
                std::string message_from(res["messages"][i]["from"]);
                std::string my_name = get_nvs((char *)"user_name");

                // cursor_y = offset_y + sprite.getCursorY() + 20;
                cursor_y = offset_y + (font_height * (i + 1));
                int next_cursor_y = offset_y + (font_height * (i + 2));

                if (message_from != my_name) {
                    // Incoming from friend
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    sprite.drawBitmap(0, cursor_y + 2, recv_icon2, 13, 12,
                                      TFT_BLACK, TFT_WHITE);
                } else {
                    // Outgoing (mine)
                    sprite.setTextColor(0x000000u, 0xFFFFFFu);
                    sprite.fillRect(0, cursor_y, 128, font_height, 0xFFFF);
                    sprite.drawBitmap(0, cursor_y + 2, send_icon2, 13, 12,
                                      TFT_WHITE, TFT_BLACK);
                }

                sprite.setCursor(14, cursor_y);

                size_t pos = 0;
                while (pos < message.length()) {
                    // UTF-8の先頭バイトを調べる
                    uint8_t c = message[pos];
                    printf("pos:%d.c:0x%02X\n", pos, c);
                    int char_len = 1;
                    if ((c & 0xE0) == 0xC0)
                        char_len = 2;  // 2バイト文字
                    else if ((c & 0xF0) == 0xE0)
                        char_len = 3;  // 3バイト文字

                    std::string ch = message.substr(pos, char_len);

                    // カタカナ or ASCII 判定（UTF-8 →
                    // Unicodeへ変換するのが理想）
                    // 仮にカタカナ判定だけハードコーディングする例：
                    if ((uint8_t)ch[0] == 0xE3 &&
                        ((uint8_t)ch[1] == 0x82 || (uint8_t)ch[1] == 0x83)) {
                        sprite.setFont(&fonts::lgfxJapanGothic_12);
                    } else {
                        sprite.setFont(&fonts::Font2);
                    }

                    sprite.print(ch.c_str());
                    pos += char_len;
                }

                // sprite.print(message.c_str());
                //  sprite.drawFastHLine( 0, cursor_y, 128, 0xFFFF);
            }

            sprite.fillRect(0, 0, 128, 14, 0);
            sprite.setCursor(0, 0);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            sprite.setFont(&fonts::Font2);
            if (chat_title != "") {
                sprite.print(chat_title.c_str());
            } else {
                sprite.print(chat_to.c_str());
            }
            sprite.drawFastHLine(0, 14, 128, 0xFFFF);
            sprite.drawFastHLine(0, 15, 128, 0);

            sprite.pushSprite(&lcd, 0, 0);

            // チャタリング防止用に100msのsleep2
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        running_flag = false;
        vTaskDelete(NULL);
    };
};
bool MessageBox::running_flag = false;
std::string MessageBox::chat_title = "";

// Forward declaration for helper proxy
std::string wifi_input_info_proxy(std::string input_type,
                                  std::string type_text = "");

#define CONTACT_SIZE 5
class ContactBook {
   public:
    static bool running_flag;

    void start_message_menue_task() {
        printf("Start ContactBook Task...");
        xTaskCreatePinnedToCore(&message_menue_task, "message_menue_task", 8096,
                                NULL, 6, NULL, 1);
    }

    static void message_menue_task(void *pvParameters) {
        Max98357A buzzer;
        Led led;

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        int MAX_CONTACTS = 20;
        int CONTACT_PER_PAGE = 4;

        typedef struct {
            std::string display_name;  // username
            std::string identifier;    // short_id or uuid
        } contact_t;

        // Fetch friends list via BLE (if connected) or HTTP( Wi‑Fi )
        std::vector<contact_t> contacts;
        bool got_from_ble = false;
        if (ble_uart_is_ready()) {
            // Request friends list from phone app over BLE
            long long rid = esp_timer_get_time();
            std::string req = std::string("{ \"id\":\"") + std::to_string(rid) +
                              "\", \"type\": \"get_friends\" }\n";
            ble_uart_send(reinterpret_cast<const uint8_t *>(req.c_str()),
                          req.size());

            // Wait briefly for response; fallback to cached NVS
            const int timeout_ms = 2500;
            int waited = 0;
            while (waited < timeout_ms) {
                std::string js = get_nvs((char *)"ble_contacts");
                if (!js.empty()) {
                    StaticJsonDocument<6144> doc;
                    if (deserializeJson(doc, js) == DeserializationError::Ok) {
                        JsonArray arr = doc["friends"].as<JsonArray>();
                        if (!arr.isNull()) {
                            std::string username = get_nvs((char *)"user_name");
                            for (JsonObject f : arr) {
                                contact_t c;
                                c.display_name = std::string(
                                    f["username"].as<const char *>()
                                        ? f["username"].as<const char *>()
                                        : "");
                                const char *sid =
                                    f["short_id"].as<const char *>();
                                const char *fid =
                                    f["friend_id"].as<const char *>();
                                if (sid && strlen(sid) > 0)
                                    c.identifier = sid;
                                else if (fid)
                                    c.identifier = fid;
                                else
                                    c.identifier = c.display_name;
                                if (c.display_name != username &&
                                    !c.identifier.empty()) {
                                    contacts.push_back(c);
                                }
                            }
                            got_from_ble = true;
                            break;
                        }
                    }
                }
                // Show waiting UI
                sprite.fillRect(0, 0, 128, 64, 0);
                sprite.setFont(&fonts::Font2);
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                sprite.drawCenterString("Waiting phone...", 64, 22);
                sprite.drawCenterString("Back to exit", 64, 40);
                sprite.pushSprite(&lcd, 0, 0);
                // Allow exit during wait
                if (back_button.get_button_state().pushed) {
                    running_flag = false;
                    vTaskDelete(NULL);
                    return;
                }
                vTaskDelay(100 / portTICK_PERIOD_MS);
                waited += 100;
            }
        }

        if (!got_from_ble) {
            // HTTP fallback (Wi‑Fi). Ensure Wi‑Fi is connected.
            while (true) {
                bool connected = false;
                if (s_wifi_event_group) {
                    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
                    connected = bits & WIFI_CONNECTED_BIT;
                }
                if (connected) break;

                sprite.fillRect(0, 0, 128, 64, 0);
                sprite.setFont(&fonts::Font2);
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                sprite.drawCenterString("Connecting Wi-Fi...", 64, 22);
                sprite.drawCenterString("Press Back to exit", 64, 40);
                sprite.pushSprite(&lcd, 0, 0);
                if (back_button.get_button_state().pushed) {
                    running_flag = false;
                    vTaskDelete(NULL);
                    return;
                }
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }

            chatapi::ChatApiClient api;  // uses NVS server_host/port
            std::string username = get_nvs((char *)"user_name");
            std::string password = get_nvs((char *)"password");
            if (password == "") password = "password123";
            if (api.token().empty()) {
                api.login(username, password);
            }
            std::string resp;
            if (api.get_friends(resp) == ESP_OK) {
                StaticJsonDocument<4096> doc;
                if (deserializeJson(doc, resp) == DeserializationError::Ok) {
                    for (JsonObject f : doc["friends"].as<JsonArray>()) {
                        contact_t c;
                        c.display_name =
                            std::string(f["username"].as<const char *>()
                                            ? f["username"].as<const char *>()
                                            : "");
                        const char *sid = f["short_id"].as<const char *>();
                        const char *fid = f["friend_id"].as<const char *>();
                        if (sid && strlen(sid) > 0)
                            c.identifier = sid;
                        else if (fid)
                            c.identifier = fid;
                        else
                            c.identifier = c.display_name;
                        if (c.display_name != username &&
                            !c.identifier.empty()) {
                            contacts.push_back(c);
                        }
                    }
                }
            }
        }

        int select_index = 0;
        int font_height = 13;
        int margin = 3;
        int noti_circle_margin = 13;

        HttpClient http_client;
        // 通知の取得
        JsonDocument notif_res = http_client.get_notifications();

        MessageBox box; (void)box;
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

            sprite.fillScreen(0);

            sprite.setFont(&fonts::Font2);

            int base_count = (int)contacts.size();
            int last_index =
                base_count + 1;  // +1: Add Friend, +1: Pending Requests

            int page = select_index / CONTACT_PER_PAGE;
            int start = page * CONTACT_PER_PAGE;
            int end = start + CONTACT_PER_PAGE - 1;
            if (end > last_index) end = last_index;

            for (int i = start; i <= end; i++) {
                int row = i - start;
                int y = (font_height + margin) * row;
                sprite.setCursor(10, y);

                if (i == select_index) {
                    sprite.setTextColor(0x000000u, 0xFFFFFFu);
                    sprite.fillRect(0, y, 128, font_height + 3, 0xFFFF);
                } else {
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                }
                if (i < base_count) {
                    sprite.print(contacts[i].display_name.c_str());
                } else if (i == base_count) {
                    sprite.print("+ Add Friend");
                } else {
                    sprite.print("Pending Requests");
                }
            }

            if (joystick_state.pushed_up_edge) {
                select_index -= 1;
            } else if (joystick_state.pushed_down_edge) {
                select_index += 1;
            }

            if (select_index < 0) {
                select_index = 0;
            } else if (select_index > last_index) {
                select_index = last_index;
            }

            sprite.pushSprite(&lcd, 0, 0);

            // ジョイスティック左を押されたらメニューへ戻る
            // 戻るボタンを押されたらメニューへ戻る
            if (joystick_state.left || back_button_state.pushed) {
                break;
            }

            if (type_button_state.pushed) {
                // talk.running_flag = true;
                // talk.start_talk_task(contacts[i].name);
                if (select_index == base_count) {
                    // Add Friend flow: input friend code/ID, then send request
                    std::string friend_code =
                        wifi_input_info_proxy("Friend Code/ID", "");
                    if (friend_code != "") {
                        chatapi::ChatApiClient api;
                        std::string username = get_nvs((char *)"user_name");
                        std::string password = get_nvs((char *)"password");
                        if (password == "") password = "password123";
                        if (api.token().empty()) {
                            api.login(username, password);
                        }

                        sprite.fillRect(0, 0, 128, 64, 0);
                        sprite.setFont(&fonts::Font2);
                        sprite.setTextColor(0xFFFFFFu, 0x000000u);
                        sprite.drawCenterString("Sending request...", 64, 22);
                        sprite.pushSprite(&lcd, 0, 0);

                        std::string resp;
                        int status = 0;
                        auto err = api.send_friend_request(friend_code, &resp,
                                                           &status);
                        sprite.fillRect(0, 0, 128, 64, 0);
                        sprite.setFont(&fonts::Font2);
                        if (err == ESP_OK && status >= 200 && status < 300) {
                            sprite.drawCenterString("Request sent!", 64, 22);
                        } else if (err == ESP_OK && status >= 400) {
                            // Try to parse error from response
                            StaticJsonDocument<256> edoc;
                            const char *emsg = "Invalid ID";
                            if (deserializeJson(edoc, resp) ==
                                    DeserializationError::Ok &&
                                edoc["error"]) {
                                emsg = edoc["error"].as<const char *>();
                            }
                            sprite.drawCenterString("Error:", 64, 16);
                            sprite.drawCenterString(emsg, 64, 34);
                        } else {
                            sprite.drawCenterString("Failed to send", 64, 22);
                        }
                        sprite.pushSprite(&lcd, 0, 0);
                        vTaskDelay(1200 / portTICK_PERIOD_MS);
                    }
                } else if (select_index == base_count + 1) {
                    // Pending Requests UI
                    type_button.clear_button_state();
                    joystick.reset_timer();

                    chatapi::ChatApiClient api;
                    std::string username = get_nvs((char *)"user_name");
                    std::string password = get_nvs((char *)"password");
                    if (password == "") password = "password123";
                    if (api.token().empty()) {
                        api.login(username, password);
                    }

                    // Fetch pending
                    std::vector<std::pair<std::string, std::string>>
                        pending;  // {request_id, username}
                    {
                        std::string presp;
                        if (api.get_pending_requests(presp) == ESP_OK) {
                            StaticJsonDocument<2048> pdoc;
                            if (deserializeJson(pdoc, presp) ==
                                DeserializationError::Ok) {
                                for (JsonObject r :
                                     pdoc["requests"].as<JsonArray>()) {
                                    std::string rid =
                                        r["request_id"].as<const char *>()
                                            ? r["request_id"].as<const char *>()
                                            : "";
                                    std::string uname =
                                        r["username"].as<const char *>()
                                            ? r["username"].as<const char *>()
                                            : "";
                                    if (!rid.empty())
                                        pending.push_back({rid, uname});
                                }
                            }
                        }
                    }

                    int psel = 0;
                    while (1) {
                        sprite.fillRect(0, 0, 128, 64, 0);
                        sprite.setFont(&fonts::Font2);
                        sprite.setTextColor(0xFFFFFFu, 0x000000u);
                        if (pending.empty()) {
                            sprite.drawCenterString("No pending requests", 64,
                                                    22);
                            sprite.pushSprite(&lcd, 0, 0);
                        } else {
                            const int row_h = 20;
                            int start = (psel / 3) * 3;
                            int show = pending.size() - start;
                            if (show > 3) show = 3;
                            for (int i = 0; i < show; i++) {
                                int idx = start + i;
                                if (idx >= (int)pending.size()) break;
                                int y = i * row_h;
                                if (idx == psel) {
                                    sprite.fillRect(0, y, 128, row_h - 2,
                                                    0xFFFF);
                                    sprite.setTextColor(0x0000, 0xFFFF);
                                } else {
                                    sprite.setTextColor(0xFFFF, 0x0000);
                                }
                                sprite.setCursor(10, y);
                                std::string line = pending[idx].second;
                                sprite.print(line.c_str());
                            }
                            sprite.pushSprite(&lcd, 0, 0);
                        }

                        // Input
                        auto js = joystick.get_joystick_state();
                        auto tbs = type_button.get_button_state();
                        auto bbs = back_button.get_button_state();
                        auto ebs = enter_button.get_button_state();
                        if (js.left || bbs.pushed) break;
                        if (js.pushed_up_edge && psel > 0) psel -= 1;
                        if (js.pushed_down_edge &&
                            psel + 1 < (int)pending.size())
                            psel += 1;

                        // Accept/Reject dialog
                        if (!pending.empty() && (tbs.pushed || ebs.pushed)) {
                            bool accept = tbs.pushed;  // type_button=Accept,
                                                       // enter_button=Reject
                            // Clear states before dialog
                            type_button.clear_button_state();
                            enter_button.clear_button_state();
                            back_button.clear_button_state();
                            joystick.reset_timer();
                            // Confirm dialog
                            int selar = 0;  // 0:No 1:Yes
                            while (1) {
                                auto js2 = joystick.get_joystick_state();
                                if (js2.pushed_left_edge || js2.pushed_up_edge)
                                    selar = 0;
                                if (js2.pushed_right_edge ||
                                    js2.pushed_down_edge)
                                    selar = 1;

                                sprite.fillRect(0, 0, 128, 64, 0);
                                sprite.setTextColor(0xFFFF, 0x0000);
                                sprite.drawCenterString(
                                    accept ? "Accept?" : "Reject?", 64, 14);
                                uint16_t noFg = (selar == 0) ? 0x0000 : 0xFFFF,
                                         noBg = (selar == 0) ? 0xFFFF : 0x0000;
                                uint16_t ysFg = (selar == 1) ? 0x0000 : 0xFFFF,
                                         ysBg = (selar == 1) ? 0xFFFF : 0x0000;
                                sprite.fillRoundRect(12, 34, 40, 18, 3, noBg);
                                sprite.drawRoundRect(12, 34, 40, 18, 3, 0xFFFF);
                                sprite.setTextColor(noFg, noBg);
                                sprite.drawCenterString("No", 32, 36);
                                sprite.fillRoundRect(76, 34, 40, 18, 3, ysBg);
                                sprite.drawRoundRect(76, 34, 40, 18, 3, 0xFFFF);
                                sprite.setTextColor(ysFg, ysBg);
                                sprite.drawCenterString("Yes", 96, 36);
                                sprite.pushSprite(&lcd, 0, 0);

                                auto t2 = type_button.get_button_state();
                                auto e2 = enter_button.get_button_state();
                                auto b2 = back_button.get_button_state();
                                if (b2.pushed || js2.left)
                                    break;  // cancel dialog
                                if (t2.pushed || e2.pushed) {
                                    if (selar == 1) {
                                        // Send respond
                                        std::string rid = pending[psel].first;
                                        std::string rresp;
                                        int rstatus = 0;
                                        api.respond_friend_request(
                                            rid, accept, &rresp, &rstatus);
                                        // Show outcome
                                        sprite.fillRect(0, 0, 128, 64, 0);
                                        sprite.setTextColor(0xFFFF, 0x0000);
                                        if (rstatus >= 200 && rstatus < 300)
                                            sprite.drawCenterString("Done", 64,
                                                                    22);
                                        else
                                            sprite.drawCenterString("Failed",
                                                                    64, 22);
                                        sprite.pushSprite(&lcd, 0, 0);
                                        vTaskDelay(800 / portTICK_PERIOD_MS);
                                        // Refresh pending list
                                        pending.clear();
                                        std::string presp2;
                                        if (api.get_pending_requests(presp2) ==
                                            ESP_OK) {
                                            StaticJsonDocument<2048> pdoc2;
                                            if (deserializeJson(pdoc2,
                                                                presp2) ==
                                                DeserializationError::Ok) {
                                                for (JsonObject r :
                                                     pdoc2["requests"]
                                                         .as<JsonArray>()) {
                                                    std::string rid2 =
                                                        r["request_id"]
                                                                .as<const char
                                                                        *>()
                                                            ? r["request_id"]
                                                                  .as<const char
                                                                          *>()
                                                            : "";
                                                    std::string uname2 =
                                                        r["username"]
                                                                .as<const char
                                                                        *>()
                                                            ? r["username"]
                                                                  .as<const char
                                                                          *>()
                                                            : "";
                                                    if (!rid2.empty())
                                                        pending.push_back(
                                                            {rid2, uname2});
                                                }
                                            }
                                        }
                                    }
                                    break;  // close dialog regardless of Yes/No
                                }
                                vTaskDelay(10 / portTICK_PERIOD_MS);
                            }
                        }
                        vTaskDelay(10 / portTICK_PERIOD_MS);
                    }
                } else if (!contacts.empty() && select_index < base_count) {
                    box.running_flag = true;
                    // Set chat title as username for UI, pass identifier for
                    // API
                    MessageBox::chat_title =
                        contacts[select_index].display_name;
                    box.start_box_task(contacts[select_index].identifier);
                    while (box.running_flag) {
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                    }

                    // 通知の取得
                    notif_res = http_client.get_notifications();
                    notif_res = http_client.get_notifications();
                }

                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
            }

            vTaskDelay(10);
        }

        running_flag = false;
        vTaskDelete(NULL);
    };
};
bool ContactBook::running_flag = false;

class WiFiSetting {
   public:
    static bool running_flag;

    void start_wifi_setting_task() {
        printf("Start WiFi Setting Task...");
        // xTaskCreate(&menu_task, "menu_task", 4096, NULL, 6, NULL, 1);
        xTaskCreatePinnedToCore(&wifi_setting_task, "wifi_setting_task", 8096,
                                NULL, 6, NULL, 1);
    }

    static std::string char_to_string_ssid(uint8_t *uint_ssid) {
        if (uint_ssid == nullptr) return std::string("");
        char char_ssid[33] = {0};
        // ensure null-terminated copy up to 32 bytes
        snprintf(char_ssid, sizeof(char_ssid), "%s", reinterpret_cast<const char *>(uint_ssid));
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
        int select_x_index = 0;
        int select_y_index = 0;

        // 文字列の配列を作成
        char char_set[7][35];

        sprintf(char_set[0], "0123456789");
        sprintf(char_set[1], "abcdefghijklmn");
        sprintf(char_set[2], "opqrstuvwxyz");
        sprintf(char_set[3], "ABCDEFGHIJKLMN");
        sprintf(char_set[4], "OPQRSTUVWXYZ");
        sprintf(char_set[5], "!\"#$%%&\\'()*+,");
        sprintf(char_set[6], "-./:;<=>?@[]^_`{|}~");

        // int font_ = 13; // unused
        // int margin = 3; // unused

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        while (1) {
            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);

            sprite.setCursor(0, 0);
            sprite.print(input_type.c_str());
            sprite.drawFastHLine(0, 14, 128, 0xFFFF);
            sprite.drawFastHLine(0, 45, 128, 0xFFFF);

            Joystick::joystick_state_t joystick_state =
                joystick.get_joystick_state();
            Button::button_state_t type_button_state =
                type_button.get_button_state();
            Button::button_state_t back_button_state =
                back_button.get_button_state();

            // 入力イベント
            if (back_button_state.pushed) {
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
                type_text =
                    type_text + char_set[select_y_index][select_x_index];
                type_button.clear_button_state();
                type_button.reset_timer();
            }

            // 文字種のスクロールの設定
            int char_set_length = sizeof(char_set) / sizeof(char_set[0]);
            if (select_y_index >= char_set_length) {
                select_y_index = 0;
            } else if (select_y_index < 0) {
                select_y_index = char_set_length - 1;
            }

            // 文字選択のスクロールの設定
            if (select_x_index < 0) {
                // 一番左へ行ったら右端へ戻る
                select_x_index = 0;
                for (int i = 0; char_set[select_y_index][i] != '\0'; i++) {
                    select_x_index += 1;
                }
                select_x_index -= 1;  // 最後の有効インデックス
            } else if (char_set[select_y_index][select_x_index] == '\0') {
                // 一番右へ行ったら左へ戻る
                select_x_index = 0;
            }

            int draw_x = 0;
            for (int i = 0; char_set[select_y_index][i] != '\0'; i++) {
                sprite.setCursor(draw_x, 46);
                if (select_x_index == i) {
                    sprite.setTextColor(0x000000u, 0xFFFFFFu);
                } else {
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                }
                sprite.print(char_set[select_y_index][i]);
                char c = char_set[select_y_index][i];
                const char *c_ptr = &c;
                draw_x += 8;
            }

            // 入力された文字の表示
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            sprite.setCursor(0, 15);
            sprite.print(type_text.c_str());

            sprite.pushSprite(&lcd, 0, 0);
            // Avoid starving the watchdog while waiting for input
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        return type_text;
    }

    static void set_wifi_info(uint8_t *ssid = 0) {
        WiFi wifi;

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        int select_index = 0;
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

            // 入力イベント
            if (joystick_state.left or back_button_state.pushed) {
                break;
            } else if (joystick_state.pushed_up_edge) {
                select_index -= 1;
            } else if (joystick_state.pushed_down_edge) {
                select_index += 1;
            }

            if (select_index > 2) {
                select_index = 2;
            } else if (select_index < 0) {
                select_index = 0;
            }

            sprite.setCursor(0, 0);
            if (select_index == 0) {
                sprite.fillRect(0, (font_height + margin) * select_index, 128,
                                font_height + 3, 0xFFFF);
                sprite.setTextColor(0x000000u, 0xFFFFFFu);
            } else {
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
            }
            std::string disp_ssid = "SSID: " + get_omitted_ssid(ssid);
            sprite.print(disp_ssid.c_str());

            sprite.setCursor(0, font_height + margin);
            if (select_index == 1) {
                sprite.fillRect(0, (font_height + margin) * select_index, 128,
                                font_height + 3, 0xFFFF);
                sprite.setTextColor(0x000000u, 0xFFFFFFu);
            } else {
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
            }
            sprite.print("PASSWORD: ****");

            sprite.setCursor(35, 40);
            if (select_index == 2) {
                sprite.setTextColor(0x000000u, 0xFFFFFFu);
            } else {
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
            }
            sprite.print("CONNECT");

            sprite.pushSprite(&lcd, 0, 0);

            // 個別の情報入力画面へ遷移
            if (type_button_state.pushed) {
                type_button.clear_button_state();
                type_button.reset_timer();
                back_button.clear_button_state();
                back_button.reset_timer();
                joystick.reset_timer();

                if (select_index == 0) {
                    input_ssid = input_info("SSID", char_to_string_ssid(ssid));
                } else if (select_index == 1) {
                    input_pass = input_info("PASSWORD", input_pass);
                } else if (select_index == 2) {
                    wifi.wifi_set_sta(input_ssid, input_pass);
                    // wifi.wifi_set_sta("elecom-3e6943_24","7ku65wjwx8fv");
                    sprite.fillRect(0, 0, 128, 64, 0);
                    sprite.setFont(&fonts::Font2);
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    sprite.drawCenterString("Connecting...", 64, 22);
                    sprite.pushSprite(&lcd, 0, 0);

                    EventBits_t bits = xEventGroupWaitBits(
                        s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                        pdMS_TO_TICKS(10000));

                    if (bits & WIFI_CONNECTED_BIT) {
                        // Save credentials to NVS (max 5)
                        save_wifi_credential(input_ssid, input_pass);
                        sprite.fillRect(0, 0, 128, 64, 0);
                        sprite.setFont(&fonts::Font2);
                        sprite.drawCenterString("Connected!", 64, 22);
                        sprite.pushSprite(&lcd, 0, 0);
                        vTaskDelay(2000 / portTICK_PERIOD_MS);
                        return;
                    } else {
                        sprite.fillRect(0, 0, 128, 64, 0);
                        sprite.setFont(&fonts::Font2);
                        sprite.drawCenterString("Connection Failed!", 64, 22);
                        sprite.pushSprite(&lcd, 0, 0);
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
        lcd.init();
        lcd.setRotation(2);

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font2);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height());

        sprite.fillRect(0, 0, 128, 64, 0);
        sprite.setCursor(30, 20);
        sprite.print("Scanning...");
        sprite.pushSprite(&lcd, 0, 0);

        WiFi wifi;

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        uint16_t ssid_n = DEFAULT_SCAN_LIST_SIZE;
        wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
        wifi.wifi_scan(&ssid_n, ap_info);

        int select_index = 0;
        int font_height = 13;
        int margin = 3;

        while (true) {
            sprite.fillRect(0, 0, 128, 64, 0);

            Joystick::joystick_state_t joystick_state =
                joystick.get_joystick_state();
            Button::button_state_t type_button_state =
                type_button.get_button_state();
            Button::button_state_t back_button_state =
                back_button.get_button_state();

            // 入力イベント
            if (joystick_state.left or back_button_state.pushed) {
                break;
            } else if (joystick_state.pushed_up_edge) {
                select_index -= 1;
            } else if (joystick_state.pushed_down_edge) {
                select_index += 1;
            }

            if (select_index < 0) {
                select_index = 0;
            } else if (select_index > ssid_n) {
                select_index = ssid_n;
            }

            if (type_button_state.pushed) {
                sprite.setColorDepth(8);
                sprite.setFont(&fonts::Font2);
                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
            }

        for (int i = 0; i <= ssid_n; i++) {
            sprite.setCursor(10, (font_height + margin) * i);

            if (i < ssid_n) {
                ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);
                ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);
                ESP_LOGI(TAG, "Channel \t\t%d", ap_info[i].primary);
            }

            if (i == select_index) {
                sprite.setTextColor(0x000000u, 0xFFFFFFu);
                sprite.fillRect(0, (font_height + margin) * select_index,
                                128, font_height + 3, 0xFFFF);
            } else {
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
            }

            if (ssid_n == i) {
                // 手動入力のためのOtherを表示
                std::string disp_ssid = "Other";
                sprite.print(disp_ssid.c_str());
            } else {
                // スキャンの結果取得できたSSIDを表示
                sprite.print(get_omitted_ssid(ap_info[i].ssid).c_str());
            }
        }

            sprite.pushSprite(&lcd, 0, 0);

            // 個別のWiFi設定画面へ遷移
            if (type_button_state.pushed) {
                if (ssid_n == select_index) {
                    set_wifi_info();
                } else {
                    set_wifi_info(ap_info[select_index].ssid);
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

        running_flag = false;
        vTaskDelete(NULL);
    };
};
bool WiFiSetting::running_flag = false;

// Define proxy after WiFiSetting is fully defined
inline std::string wifi_input_info_proxy(std::string input_type,
                                         std::string type_text) {
    return WiFiSetting::input_info(input_type, type_text);
}

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
        sprite.pushSprite(&lcd, 0, 0);

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

        ESP_ERROR_CHECK(esp_now_init());
        ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

        esp_now_peer_info_t peerInfo = {
            .channel = 0,
            .ifidx = WIFI_IF_STA,
            .encrypt = false,
        };

        memset(peerInfo.lmk, 0, ESP_NOW_KEY_LEN);  // ← これを追加
        memcpy(peerInfo.peer_addr, peer_mac, 6);
        ESP_ERROR_CHECK(esp_now_add_peer(&peerInfo));

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
        ESP_ERROR_CHECK(esp_now_init());
        ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    }

    void morse_p2p() {
        p2p_init();

        Max98357A buzzer;
        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

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
                buzzer.stop_tone();
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
                input_switch_pos = pos;
                back_button.pushed_same_time();
                type_button.clear_button_state();
            } else if (back_button_state.pushed and
                       !back_button_state.pushed_same_time and
                       !type_button_state.pushing) {
                return;
            } else if (joystick_state.left) {
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
                sprite.pushSprite(&lcd, 0, 0);
                vTaskDelay(300 / portTICK_PERIOD_MS);

                input_switch_pos = pos;
            } else if (back_button_state.pushed) {
                back_button.clear_button_state();
            }

            // Enter(送信)キーの判定ロジック
            if (enter_button_state.pushed and message_text != "") {
                message_text = "";
                pos = 0;
                input_switch_pos = 9;
                enter_button.clear_button_state();
            }

            std::string display_text =
                message_text + morse_text + alphabet_text;

            // カーソルの点滅制御用
            if (esp_timer_get_time() - t >= 500000) {
                display_text += "|";
                printf("Timder!\n");
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

                    if ((uint8_t)ch[0] == 0xE3 &&
                        ((uint8_t)ch[1] == 0x82 || (uint8_t)ch[1] == 0x83)) {
                        sprite.setFont(&fonts::lgfxJapanGothic_12);
                    } else {
                        sprite.setFont(&fonts::Font2);
                    }

                    sprite.print(ch.c_str());
                    pos += char_len;
                } else {
                    break;
                }
            }

            // 受信したメッセージを描画
            sprite.drawFastHLine(0, 32, 128, 0xFFFF);
            sprite.setCursor(0, 35);
            sprite.print(received_text.c_str());

            sprite.pushSprite(&lcd, 0, 0);

            message_text += alphabet_text;
            if (alphabet_text != "" && input_lang == 1) {
                std::string translate_targt =
                    message_text.substr(input_switch_pos);
                for (const auto &pair : TalkDisplay::romaji_kana) {
                    std::cout << "Key: " << pair.first << std::endl;
                    size_t pos = translate_targt.find(pair.first);
                    if (pos != std::string::npos) {
                        translate_targt.replace(pos, pair.first.length(),
                                                pair.second);
                    }
                }
                message_text =
                    message_text.substr(0, input_switch_pos) + translate_targt;
            }
            alphabet_text = "";

            // チャタリング防止用に100msのsleep

            espnow_send(display_text);
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

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
        chatapi::ChatApiClient api;
        std::string password = get_nvs((char *)"password");
        if (password == "") password = "password123";
        if (api.token().empty() && user_name != "") {
            api.login(user_name, password);
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
        sprite.pushSprite(&lcd, 0, 0);
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

class SettingMenu {
   public:
    static bool running_flag;

    void start_message_menue_task() {
        printf("Start MessageMenue Task...");
        xTaskCreatePinnedToCore(&message_menue_task, "message_menue_task",
                                16192, NULL, 6, NULL, 1);
    }

    static void message_menue_task(void *pvParameters) {
        lcd.init();

        WiFiSetting wifi_setting;

        Max98357A buzzer;
        Led led;

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        lcd.setRotation(2);
        // int MAX_SETTINGS = 20; // unused
        int ITEM_PER_PAGE = 4;

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font4);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height());

        typedef struct {
            std::string setting_name;
        } setting_t;

        // Add Bluetooth pairing item to settings
        setting_t settings[10] = {{"Profile"},        {"Wi-Fi"},
                                  {"Bluetooth"},      {"Sound"},
                                  {"Real Time Chat"}, {"Auto Update"},
                                  {"OTA Manifest"},   {"Update Now"},
                                  {"Develop"},        {"Factory Reset"}};

        int select_index = 0;
        int font_height = 13;
        int margin = 3;

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

            sprite.fillScreen(0);

            sprite.setFont(&fonts::Font2);

            int last_index = (int)(sizeof(settings) / sizeof(setting_t)) -
                             1;  // 0-based last index
            int total_items = last_index + 1;
            int page = select_index / ITEM_PER_PAGE;
            int start = page * ITEM_PER_PAGE;
            int end = start + ITEM_PER_PAGE - 1;
            if (end > last_index) end = last_index;

            for (int i = start; i <= end; i++) {
                int row = i - start;
                int y = (font_height + margin) * row;
                sprite.setCursor(10, y);

                if (i == select_index) {
                    sprite.setTextColor(0x000000u, 0xFFFFFFu);
                    sprite.fillRect(0, y, 128, font_height + 3, 0xFFFF);
                } else {
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                }
                // Render name (Develop/Auto Update show ON/OFF)
                if (settings[i].setting_name == "Develop") {
                    std::string dev = get_nvs((char *)"develop_mode");
                    bool on = (dev == "true");
                    std::string label =
                        settings[i].setting_name + (on ? " [ON]" : " [OFF]");
                    sprite.print(label.c_str());
                } else if (settings[i].setting_name == "Auto Update") {
                    std::string au = get_nvs((char *)"ota_auto");
                    bool on = (au == "true");
                    std::string label =
                        settings[i].setting_name + (on ? " [ON]" : " [OFF]");
                    sprite.print(label.c_str());
                } else if (settings[i].setting_name == "OTA Manifest") {
                    // show current (or default) manifest URL
                    std::string mf = get_nvs((char *)"ota_manifest");
                    if (mf.empty()) {
                        mf = "https://mimoc.jp/api/firmware/"
                             "latest?device=esp32s3&channel=stable";
                    }
                    std::string label = "OTA Manifest";
                    sprite.print(label.c_str());
                } else if (settings[i].setting_name == "Update Now") {
                    sprite.print("Update Now");
                } else {
                    sprite.print(settings[i].setting_name.c_str());
                }
            }

            if (joystick_state.pushed_up_edge) {
                select_index -= 1;
            } else if (joystick_state.pushed_down_edge) {
                select_index += 1;
            }

            if (select_index < 0) {
                select_index = 0;
            } else if (select_index > last_index) {
                select_index = last_index;
            }

            sprite.pushSprite(&lcd, 0, 0);

            // ジョイスティック左を押されたらメニューへ戻る
            // 戻るボタンを押されたらメニューへ戻る
            if (joystick_state.left || back_button_state.pushed) {
                break;
            }

            if (type_button_state.pushed &&
                settings[select_index].setting_name == "Wi-Fi") {
                wifi_setting.running_flag = true;
                wifi_setting.start_wifi_setting_task();
                while (wifi_setting.running_flag) {
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
            } else if (type_button_state.pushed &&
                       settings[select_index].setting_name == "Bluetooth") {
                // Simple Bluetooth pairing UI (UI only, no BLE stack)
                type_button.clear_button_state();
                joystick.reset_timer();

                // Local state backed by NVS
                auto nvs_str = [](const char *key) {
                    return get_nvs((char *)key);
                };
                auto nvs_put = [](const char *key, const std::string &v) {
                    save_nvs((char *)key, v);
                };

                // Helper to generate 6-digit code
                auto gen_code = []() -> std::string {
                    uint32_t r = (uint32_t)esp_timer_get_time();
                    // Very simple PRNG from timer; adequate for UI pairing hint
                    r = (1103515245u * r + 12345u);
                    uint32_t n = (r % 900000u) + 100000u;  // 100000-999999
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%06u", (unsigned)n);
                    return std::string(buf);
                };

                // Initialize state (read once; avoid frequent NVS I/O)
                bool pairing = (nvs_str("ble_pair") == std::string("true"));
                long long now_us = esp_timer_get_time();
                long long exp_us = 0;
                std::string code = "";
                {
                    std::string s = nvs_str("ble_exp_us");
                    if (!s.empty()) exp_us = std::atoll(s.c_str());
                    code = nvs_str("ble_code");
                }
                if (pairing && exp_us <= now_us) {
                    pairing = false;
                    nvs_put("ble_pair", "false");
                }
                if (pairing && code.empty()) {
                    code = gen_code();
                    nvs_put("ble_code", code);
                }
                if (pairing) {
                    // Ensure BLE is advertising if pairing already ON
                    ble_uart_enable();
                }

                int sel = 0;  // 0: Toggle, 1: Refresh Code
                while (1) {
                    Joystick::joystick_state_t jst =
                        joystick.get_joystick_state();
                    Button::button_state_t tbs = type_button.get_button_state();
                    Button::button_state_t bbs = back_button.get_button_state();
                    Button::button_state_t ebs =
                        enter_button.get_button_state();

                    // Refresh time only (avoid reading from NVS in loop)
                    now_us = esp_timer_get_time();
                    long long remain_s =
                        pairing ? (exp_us - now_us) / 1000000LL : 0;
                    if (pairing && remain_s <= 0) {
                        pairing = false;
                        nvs_put("ble_pair", "false");
                        ble_uart_disable();
                        mqtt_rt_resume();
                    }

                    // Draw screen
                    sprite.fillRect(0, 0, 128, 64, 0);
                    sprite.setFont(&fonts::Font2);
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    sprite.drawCenterString("Bluetooth Pairing", 64, 4);

                    // Status line
                    char status[32];
                    snprintf(status, sizeof(status), "Status: %s",
                             pairing ? "ON" : "OFF");
                    sprite.setCursor(6, 22);
                    sprite.print(status);

                    // Code + remain
                    if (pairing && !code.empty()) {
                        sprite.setCursor(6, 36);
                        sprite.print("Code: ");
                        sprite.print(code.c_str());
                        sprite.setCursor(6, 50);
                        char ttl[32];
                        snprintf(ttl, sizeof(ttl), "Expires: %llds", remain_s);
                        sprite.print(ttl);
                    } else {
                        sprite.setCursor(6, 36);
                        sprite.print("Press to start pairing");
                    }

                    // Hint about same account login
                    // (kept brief due to screen size)
                    // Example: "Login same account on phone"
                    sprite.pushSprite(&lcd, 0, 0);

                    // Input handling
                    if (bbs.pushed || jst.left) {
                        break;  // exit Bluetooth menu
                    }
                    if (jst.pushed_left_edge) sel = 0;
                    if (jst.pushed_right_edge) sel = 1;
                    if (tbs.pushed || ebs.pushed) {
                        if (!pairing) {
                            // Turn on pairing, generate code and 120s window
                            code = gen_code();
                            nvs_put("ble_code", code);
                            long long until =
                                esp_timer_get_time() + 120LL * 1000000LL;
                            exp_us = until;
                            nvs_put("ble_exp_us", std::to_string(until));
                            nvs_put("ble_pair", "true");
                            pairing = true;
                            // Show enabling message, then free sprite to save
                            // RAM
                            sprite.fillRect(0, 0, 128, 64, 0);
                            sprite.setFont(&fonts::Font2);
                            sprite.setTextColor(0xFFFFFFu, 0x000000u);
                            sprite.drawCenterString("Enabling BLE...", 64, 26);
                            sprite.pushSprite(&lcd, 0, 0);
                            // Pause MQTT to free memory before BLE init
                            mqtt_rt_pause();
                            // Free sprite buffer (~8KB) to increase largest
                            // block
                            sprite.deleteSprite();
                            ble_uart_enable();
                            if (ble_uart_last_err() != 0) {
                                // BLE init failed; roll back and show message
                                pairing = false;
                                nvs_put("ble_pair", "false");
                                // Recreate sprite to render error
                                sprite.createSprite(lcd.width(), lcd.height());
                                sprite.fillRect(0, 0, 128, 64, 0);
                                sprite.setFont(&fonts::Font2);
                                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                                sprite.drawCenterString("BLE Init Failed", 64,
                                                        22);
                                sprite.drawCenterString("Check memory/CFG", 64,
                                                        40);
                                sprite.pushSprite(&lcd, 0, 0);
                                vTaskDelay(1200 / portTICK_PERIOD_MS);
                                mqtt_rt_resume();
                                break;
                            }
                            // Success: recreate sprite for UI rendering
                            sprite.createSprite(lcd.width(), lcd.height());
                        } else {
                            // If already on, toggle off
                            nvs_put("ble_pair", "false");
                            pairing = false;
                            ble_uart_disable();
                            mqtt_rt_resume();
                        }
                        type_button.clear_button_state();
                        type_button.reset_timer();
                        enter_button.clear_button_state();
                        enter_button.reset_timer();
                        joystick.reset_timer();
                    }

                    vTaskDelay(50 / portTICK_PERIOD_MS);
                }
            } else if (type_button_state.pushed &&
                       settings[select_index].setting_name == "OTA Manifest") {
                // Show current manifest URL in a simple modal
                std::string mf = get_nvs((char *)"ota_manifest");
                if (mf.empty()) {
                    mf = "https://mimoc.jp/api/firmware/"
                         "latest?device=esp32s3&channel=stable";
                }
                // Simple wrap: break into chunks that fit the screen width
                const int max_chars = 21;
                std::vector<std::string> lines;
                for (size_t p = 0; p < mf.size(); p += max_chars) {
                    lines.emplace_back(mf.substr(p, max_chars));
                    if (lines.size() >= 3) break;  // fit on 64px height
                }
                // Clear the triggering button state to avoid instant exit
                type_button.clear_button_state();
                type_button.reset_timer();
                back_button.clear_button_state();
                back_button.reset_timer();
                while (1) {
                    Joystick::joystick_state_t js =
                        joystick.get_joystick_state();
                    Button::button_state_t tb = type_button.get_button_state();
                    Button::button_state_t bb = back_button.get_button_state();
                    sprite.fillRect(0, 0, 128, 64, 0);
                    sprite.setFont(&fonts::Font2);
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    sprite.drawCenterString("OTA Manifest", 64, 4);
                    int y = 22;
                    for (auto &l : lines) {
                        sprite.drawCenterString(l.c_str(), 64, y);
                        y += 14;
                    }
                    sprite.pushSprite(&lcd, 0, 0);
                    if (bb.pushed || js.left || tb.pushed) {
                        break;
                    }
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                }
                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
            } else if (type_button_state.pushed &&
                       settings[select_index].setting_name == "Update Now") {
                // Manual OTA check and update
                // Pause MQTT to free resources during TLS/OTA
                mqtt_rt_pause();
                sprite.fillRect(0, 0, 128, 64, 0);
                sprite.setFont(&fonts::Font2);
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                sprite.drawCenterString("Checking update...", 64, 26);
                sprite.pushSprite(&lcd, 0, 0);
                esp_err_t r = ota_client::check_and_update_once();
                // If update was available, device will reboot inside OTA
                sprite.fillRect(0, 0, 128, 64, 0);
                if (r == ESP_OK) {
                    sprite.drawCenterString("Up-to-date", 64, 26);
                } else {
                    sprite.drawCenterString("Update failed", 64, 26);
                }
                sprite.pushSprite(&lcd, 0, 0);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                mqtt_rt_resume();
                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
            } else if (type_button_state.pushed &&
                       settings[select_index].setting_name == "Auto Update") {
                // Toggle auto OTA ON/OFF
                std::string au = get_nvs((char *)"ota_auto");
                bool on = (au == "true");
                on = !on;
                save_nvs((char *)"ota_auto",
                         on ? std::string("true") : std::string("false"));
                // Feedback
                sprite.fillRect(0, 0, 128, 64, 0);
                sprite.setFont(&fonts::Font2);
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                sprite.drawCenterString(
                    on ? "Auto Update: ON" : "Auto Update: OFF", 64, 22);
                sprite.pushSprite(&lcd, 0, 0);
                vTaskDelay(800 / portTICK_PERIOD_MS);
                // Start/stop background task accordingly (best-effort)
                if (on) {
                    ota_client::start_background_task();
                }
                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
            } else if (type_button_state.pushed &&
                       settings[select_index].setting_name == "Develop") {
                // Toggle develop mode ON/OFF
                std::string dev = get_nvs((char *)"develop_mode");
                bool on = (dev == "true");
                on = !on;
                save_nvs((char *)"develop_mode",
                         on ? std::string("true") : std::string("false"));
                if (on) {
                    save_nvs((char *)"server_scheme", std::string("http"));
                    save_nvs((char *)"server_host",
                             std::string("192.168.2.184"));
                    save_nvs((char *)"server_port", std::string("8080"));
                    save_nvs((char *)"mqtt_host", std::string("192.168.2.184"));
                    save_nvs((char *)"mqtt_port", std::string("1883"));
                } else {
                    save_nvs((char *)"server_scheme", std::string("https"));
                    save_nvs((char *)"server_host", std::string("mimoc.jp"));
                    save_nvs((char *)"server_port", std::string("443"));
                    save_nvs((char *)"mqtt_host", std::string("mimoc.jp"));
                    save_nvs((char *)"mqtt_port", std::string("1883"));
                }
                // Feedback screen
                sprite.fillRect(0, 0, 128, 64, 0);
                sprite.setFont(&fonts::Font2);
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                sprite.drawCenterString(on ? "Develop: ON" : "Develop: OFF", 64,
                                        22);
                sprite.drawCenterString(
                    on ? "http://192.168.2.184" : "https://mimoc.jp", 64, 40);
                sprite.pushSprite(&lcd, 0, 0);
                vTaskDelay(1200 / portTICK_PERIOD_MS);
                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
            } else if (type_button_state.pushed &&
                       settings[select_index].setting_name == "Profile") {
                Profile();
                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();

            } else if (type_button_state.pushed &&
                       settings[select_index].setting_name ==
                           "Real Time Chat") {
                P2P_Display p2p;
                p2p.morse_p2p();
            } else if (type_button_state.pushed &&
                       settings[select_index].setting_name == "Factory Reset") {
                // Confirmation dialog
                type_button.clear_button_state();
                type_button.reset_timer();
                back_button.clear_button_state();
                back_button.reset_timer();
                joystick.reset_timer();
                int sel = 0;  // 0: No, 1: Yes
                while (1) {
                    // Read input
                    Joystick::joystick_state_t jst =
                        joystick.get_joystick_state();
                    Button::button_state_t tbs = type_button.get_button_state();
                    Button::button_state_t bbs = back_button.get_button_state();

                    if (jst.pushed_left_edge) sel = 0;
                    if (jst.pushed_right_edge) sel = 1;

                    sprite.fillRect(0, 0, 128, 64, 0);
                    sprite.setFont(&fonts::Font2);
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    sprite.drawCenterString("Factory Reset?", 64, 10);

                    // Draw buttons
                    // No button (left)
                    uint16_t noFg = (sel == 0) ? 0x0000 : 0xFFFF;
                    uint16_t noBg = (sel == 0) ? 0xFFFF : 0x0000;
                    sprite.fillRoundRect(12, 34, 40, 18, 3, noBg);
                    sprite.drawRoundRect(12, 34, 40, 18, 3, 0xFFFF);
                    sprite.setTextColor(noFg, noBg);
                    sprite.drawCenterString("No", 12 + 20, 36);

                    // Yes button (right)
                    uint16_t ysFg = (sel == 1) ? 0x0000 : 0xFFFF;
                    uint16_t ysBg = (sel == 1) ? 0xFFFF : 0x0000;
                    sprite.fillRoundRect(76, 34, 40, 18, 3, ysBg);
                    sprite.drawRoundRect(76, 34, 40, 18, 3, 0xFFFF);
                    sprite.setTextColor(ysFg, ysBg);
                    sprite.drawCenterString("Yes", 76 + 20, 36);

                    sprite.pushSprite(&lcd, 0, 0);

                    if (bbs.pushed) {
                        // Cancel and return to settings list
                        break;
                    }
                    if (tbs.pushed) {
                        if (sel == 1) {
                            // Proceed with reset
                            sprite.fillRect(0, 0, 128, 64, 0);
                            sprite.setTextColor(0xFFFFFFu, 0x000000u);
                            sprite.drawCenterString("Resetting...", 64, 22);
                            sprite.drawCenterString("Erasing NVS", 64, 40);
                            sprite.pushSprite(&lcd, 0, 0);

                            esp_err_t err = nvs_flash_erase();
                            if (err == ESP_OK) {
                                nvs_flash_init();
                                sprite.fillRect(0, 0, 128, 64, 0);
                                sprite.drawCenterString("Reset Done", 64, 22);
                                sprite.drawCenterString("Rebooting...", 64, 40);
                                sprite.pushSprite(&lcd, 0, 0);
                                vTaskDelay(1000 / portTICK_PERIOD_MS);
                                esp_restart();
                            } else {
                                sprite.fillRect(0, 0, 128, 64, 0);
                                sprite.drawCenterString("Reset Failed", 64, 22);
                                sprite.drawCenterString("Check logs", 64, 40);
                                sprite.pushSprite(&lcd, 0, 0);
                                vTaskDelay(2000 / portTICK_PERIOD_MS);
                            }
                        }
                        // If No selected or after failure, exit dialog
                        break;
                    }

                    // debounce
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }
            }

            vTaskDelay(1);
        }

        running_flag = false;
        vTaskDelete(NULL);
    };
};
bool SettingMenu::running_flag = false;

class Game {
   public:
    static bool running_flag;

    void start_game_task() {
        printf("Start Game Task...");
        // xTaskCreate(&menu_task, "menu_task", 4096, NULL, 6, NULL, 1);
        xTaskCreatePinnedToCore(&game_task, "game_task", 4096, NULL, 6, NULL,
                                1);
    }

    static std::map<std::string, std::string> morse_code;
    static std::map<std::string, std::string> morse_code_reverse;
    static void game_task(void *pvParameters) {
        lcd.init();
        lcd.setRotation(0);

        Max98357A buzzer;
        Led led;

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        lcd.setRotation(2);

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font4);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height());

        char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

        srand(esp_timer_get_time());
        char random_char = letters[rand() % 26];

        while (1) {
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
            while (c < n) {
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
                    buzzer.stop_tone();
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
                    led.led_on();
                    neopixel.set_color(10, 0, 0);
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                    led.led_off();
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

                sprite.pushSprite(&lcd, 0, 0);

                message_text += alphabet_text;
                alphabet_text = "";

                // チャタリング防止用に100msのsleep
                vTaskDelay(10 / portTICK_PERIOD_MS);

                printf("message_text:%s\n", message_text.c_str());
            }

            // break_flagが立ってたら終了
            if (break_flag) {
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
            sprite.pushSprite(&lcd, 0, 0);

            while (1) {
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
            }

            if (break_flag) {
                break;
            }
        }

        running_flag = false;
        vTaskDelete(NULL);
    };
};
bool Game::running_flag = false;

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

class MenuDisplay {
#define NAME_LENGTH_MAX 8
   public:
    void start_menu_task() {
        printf("Start Menu Task...");
        // xTaskCreate(&menu_task, "menu_task", 4096, NULL, 6, NULL, 1);
        xTaskCreatePinnedToCore(&menu_task, "menu_task", 8096, NULL, 6, NULL,
                                0);
    }

    // static HttpClient http;

    static void menu_task(void *pvParameters) {
        struct menu_t {
            char menu_name[NAME_LENGTH_MAX];
            int display_position_x;
            int display_position_y;
        };

        struct menu_t menu_list[3] = {
            {"Talk", 9, 22}, {"Box", 51, 22}, {"Game", 93, 22}};

        int cursor_index = 0;

        HttpClient http_client;

        Joystick joystick;

        PowerMonitor power;

        // メニューから遷移する機能のインスタンス
        MessageBox box;
        Game game;
        ContactBook contactBook;
        SettingMenu settingMenu;

        Button type_button(GPIO_NUM_46);
        Button enter_button(GPIO_NUM_5);

        // TODO: Buttonクラスではなく別で実装する
        Button charge_stat(GPIO_NUM_8);

        lcd.setRotation(2);

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font4);
        sprite.setTextWrap(false);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height());

        // 開始時間を取得 st=start_time
        long long int st = esp_timer_get_time();
        // 電波強度の初期値
        float radioLevel = 0;

        // 通知の取得
        http_client.start_notifications();
        JsonDocument notif_res = http_client.get_notifications();

        // バッテリー電圧の取得
        PowerMonitor::power_state_t power_state = power.get_power_state();
        printf("Power Voltage:%d\n", power_state.power_voltage);

        if (power_state.power_voltage > 140) {
            power_state.power_voltage = 140;
        }
        float power_per = power_state.power_voltage / 1.4;
        int power_per_pix = (int)(0.12 * power_per);

        while (1) {
            // 画面上部のステータス表示
            sprite.drawFastHLine(0, 12, 128, 0xFFFF);

            // 電波状況表示
            int rx = 4;
            int ry = 6;
            int rh = 4;
            for (int r = radioLevel; 0 < r; r--) {
                sprite.fillRect(rx, ry, 2, rh, 0xFFFF);
                rx += 3;
                ry -= 2;
                rh += 2;
            }

            // 経過時間を取得
            int p_time = (esp_timer_get_time() - st) / 1000000;
            if (p_time > 3) {
                // 電波強度を更新
                wifi_ap_record_t ap;
                esp_wifi_sta_get_ap_info(&ap);
                printf("%d\n", ap.rssi);

                radioLevel = 4 - (ap.rssi / -20);
                if (radioLevel < 1) {
                    radioLevel = 1;
                }

                // バッテリー電圧を更新
                power_state = power.get_power_state();
                printf("Power Voltage:%d\n", power_state.power_voltage);

                if (power_state.power_voltage > 140) {
                    power_state.power_voltage = 140;
                }
                power_per = power_state.power_voltage / 1.4;
                power_per_pix = (int)(0.12 * power_per);

                // 通知情報を更新
                notif_res = http_client.get_notifications();
                st = esp_timer_get_time();
            }

            // メッセージ受信通知の表示
            // if (http.notif_flag) {
            //   sprite.drawRoundRect(50, 0, 20, 8, 2, 0xFFFF);
            // }

            // printf("Power Per:%d\n", power_per_pix);

            // 電池残量表示
            sprite.drawRoundRect(110, 0, 14, 8, 2, 0xFFFF);
            sprite.fillRect(111, 0, power_per_pix, 8, 0xFFFF);

            Button::button_state_t type_charge_stat =
                charge_stat.get_button_state();

            if (type_charge_stat.pushing) {
                sprite.fillRect(105, 2, 2, 2, 0xFFFF);
            }
            charge_stat.clear_button_state();
            charge_stat.reset_timer();

            sprite.fillRect(124, 2, 1, 4, 0xFFFF);

            // Menu選択の表示
            sprite.fillRoundRect(menu_list[cursor_index].display_position_x - 2,
                                 menu_list[cursor_index].display_position_y - 2,
                                 34, 34, 5, 0xFFFF);

            // Menu項目を表示させる
            int menu_lists_n = sizeof(menu_list) / sizeof(menu_t);
            for (int i = 0; i < menu_lists_n; i++) {
                const unsigned char *icon_image = mail_icon;

                if (i == 1) {
                    icon_image = setting_icon;
                } else if (i == 2) {
                    icon_image = game_icon;
                }

                if (cursor_index == i) {
                    sprite.drawBitmap(menu_list[i].display_position_x,
                                      menu_list[i].display_position_y,
                                      icon_image, 30, 30, TFT_WHITE, TFT_BLACK);
                } else {
                    sprite.drawBitmap(menu_list[i].display_position_x,
                                      menu_list[i].display_position_y,
                                      icon_image, 30, 30, TFT_BLACK, TFT_WHITE);
                }
            }

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

            if (type_button_state.pushed) {
                printf("Button pushed!\n");
                printf("Pushing time:%lld\n", type_button_state.pushing_sec);
                printf("Push type:%c\n", type_button_state.push_type);

                if (cursor_index == 0) {
                    contactBook.running_flag = true;
                    contactBook.start_message_menue_task();
                    while (contactBook.running_flag) {
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                    }
                    sprite.setFont(&fonts::Font4);
                } else if (cursor_index == 1) {
                    settingMenu.running_flag = true;
                    settingMenu.start_message_menue_task();
                    while (settingMenu.running_flag) {
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                    }
                    sprite.setFont(&fonts::Font4);

                    // box.running_flag = true;
                    // box.start_box_task();
                    // // talkタスクの実行フラグがfalseになるまで待機
                    // while(box.running_flag){
                    // 	vTaskDelay(100 / portTICK_PERIOD_MS);
                    // }
                    // sprite.setFont(&fonts::Font4);
                } else if (cursor_index == 2) {
                    game.running_flag = true;
                    game.start_game_task();
                    while (game.running_flag) {
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                    }
                    sprite.setFont(&fonts::Font4);
                }

                // 通知情報を更新
                notif_res = http_client.get_notifications();

                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
            }

            if (joystick_state.pushed_left_edge) {
                cursor_index -= 1;
                if (cursor_index < 0) {
                    cursor_index = menu_lists_n - 1;
                }
            } else if (joystick_state.pushed_right_edge) {
                cursor_index += 1;
                if (cursor_index >= menu_lists_n) {
                    cursor_index = 0;
                }
            }

            // 通知の表示
            for (int i = 0; i < notif_res["notifications"].size(); i++) {
                std::string notification_flag(
                    notif_res["notifications"][i]["notification_flag"]);
                if (notification_flag == "true") {
                    if (cursor_index == 0) {
                        sprite.fillCircle(37, 25, 4, 0);
                    } else {
                        sprite.fillCircle(37, 25, 4, 0xFFFF);
                    }
                    break;
                }
            }

            sprite.pushSprite(&lcd, 0, 0);
            sprite.fillRect(0, 0, 128, 64, 0);

            // esp_task_wdt_reset();

            // 30秒操作がなければsleep
            int button_free_time = type_button_state.release_sec / 1000000;
            int joystick_free_time = joystick_state.release_sec / 1000000;

            if (button_free_time >= 30 and joystick_free_time >= 30) {
                printf("button_free_time:%d\n", button_free_time);
                printf("joystick_free_time:%d\n", joystick_free_time);
                sprite.fillRect(0, 0, 128, 64, 0);
                sprite.pushSprite(&lcd, 0, 0);
                esp_deep_sleep_start();
            } else if (enter_button_state.pushed) {
                type_button.clear_button_state();
                type_button.reset_timer();
                esp_deep_sleep_start();
            }

            vTaskDelay(50 / portTICK_PERIOD_MS);
        }

        vTaskDelete(NULL);
    };
};

class ProfileSetting {
   public:
    static std::string input_info(std::string type_text = "") {
        int select_x_index = 0;
        int select_y_index = 0;

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

        while (1) {
            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);

            sprite.drawCenterString("HI, DE Mimoc.", 64, 0);
            sprite.drawCenterString("YOU ARE?", 64, 15);

            Joystick::joystick_state_t joystick_state =
                joystick.get_joystick_state();
            Button::button_state_t type_button_state =
                type_button.get_button_state();
            Button::button_state_t back_button_state =
                back_button.get_button_state();

            // 入力イベント
            if (back_button_state.pushed) {
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
                type_text =
                    type_text + char_set[select_y_index][select_x_index];
                type_button.clear_button_state();
                type_button.reset_timer();
            }

            // 文字種のスクロールの設定
            int char_set_length = sizeof(char_set) / sizeof(char_set[0]);
            if (select_y_index >= char_set_length) {
                select_y_index = 0;
            } else if (select_y_index < 0) {
                select_y_index = char_set_length - 1;
            }

            // 文字選択のスクロールの設定
            if (char_set[select_y_index][select_x_index] == '\0') {
                // 一番右へ行ったら左へ戻る
                select_x_index = 0;
            } else if (select_y_index < 0) {
                // 一番左へ行ったら右へ戻る
                select_x_index = 0;
                for (int i = 0; char_set[select_y_index][i] != '\0'; i++) {
                    select_x_index += 1;
                }
            }

            int draw_x = 0;
            for (int i = 0; char_set[select_y_index][i] != '\0'; i++) {
                sprite.setCursor(draw_x, 46);
                if (select_x_index == i) {
                    sprite.setTextColor(0x000000u, 0xFFFFFFu);
                } else {
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                }
                sprite.print(char_set[select_y_index][i]);
                char c = char_set[select_y_index][i];
                const char *c_ptr = &c;
                draw_x += sprite.textWidth(c_ptr);
            }

            // 入力された文字の表示
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            sprite.setCursor(0, 30);
            sprite.print(("Name: " + type_text).c_str());
            sprite.drawFastHLine(0, 45, 128, 0xFFFF);

            sprite.pushSprite(&lcd, 0, 0);
        }

        return type_text;
    }

    static std::string set_profile_info(uint8_t *ssid = 0) {
        std::string user_name = "";

        user_name = input_info();
        return user_name;
    }

    static void morse_greeting(const char* greet_txt, const char* last_greet_txt = "",
                               int cx = 64, int cy = 32) {
        Max98357A buzzer;

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font2);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height());

        sprite.fillRect(0, 0, 128, 64, 0);
        sprite.setCursor(30, 20);

        std::string greet_str(greet_txt);
        std::string morse_str;
        std::string show_greet_str;

        int greet_len = strlen(greet_txt);
        for (int i = 0; i < greet_len; i++) {
            char upper = std::toupper(static_cast<unsigned char>(greet_txt[i]));
            std::string key(1, upper);
            std::string morse_txt = Game::morse_code_reverse.at(key);

            printf("Morse %s\n", morse_txt.c_str());

            for (int j = 0; j < morse_txt.length(); j++) {
                sprite.fillRect(0, 0, 128, 64, 0);
                sprite.drawCenterString(last_greet_txt, cx, 15);
                char m = morse_txt[j];

                if (m == '.') {
                    buzzer.start_tone(2300.0f, 0.6f);
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                    buzzer.stop_tone();

                } else if (m == '_') {
                    buzzer.start_tone(2300.0f, 0.6f);
                    vTaskDelay(150 / portTICK_PERIOD_MS);
                    buzzer.stop_tone();
                }
                morse_str += m;
                sprite.drawCenterString((show_greet_str + morse_str).c_str(),
                                        cx, cy);
                sprite.pushSprite(&lcd, 0, 0);
            }
            morse_str = "";

            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.drawCenterString(last_greet_txt, cx, 15);
            show_greet_str = greet_str.substr(0, i + 1).c_str();
            sprite.drawCenterString(show_greet_str.c_str(), cx, cy);
            sprite.pushSprite(&lcd, 0, 0);

            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }

    static void profile_setting_task() {
        // Ensure Wi-Fi is connected before proceeding to initial setup
        while (1) {
            // If event group exists and connected bit is set, continue
            if (s_wifi_event_group) {
                EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
                if (bits & WIFI_CONNECTED_BIT) {
                    break;
                }
            }

            // Launch Wi-Fi setting UI; returns to this loop when user exits
            WiFiSetting wifi_setting;
            wifi_setting.running_flag = true;
            wifi_setting.start_wifi_setting_task();
            while (wifi_setting.running_flag) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }

            // Loop again to re-check connectivity; block until online
        }

        // Register user to WebServer after getting profile name
        // Uses default password if none is set in NVS
        int cx = 64;
        int cy = 15;
        morse_greeting("HI, DE Mimoc.", "", cx, cy);
        vTaskDelay(400 / portTICK_PERIOD_MS);
        morse_greeting("YOU ARE?", "HI, DE Mimoc.");

        while (cy > 0) {
            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.drawCenterString("HI, DE Mimoc.", cx, cy);
            sprite.drawCenterString("YOU ARE?", cx, cy + 17);
            sprite.pushSprite(&lcd, 0, 0);
            cy--;
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }

        std::string user_name = set_profile_info();
        save_nvs("user_name", user_name);

        // Try user registration via chat backend
        // If password not set, use default for development
        std::string password = get_nvs("password");
        if (password == "") {
            password = "password123";
            save_nvs((char *)"password", password);
        }

        // Call register API. If it fails (e.g., network), try login as
        // fallback. Use defaults/NVS overrides for server endpoint.
        {
            chatapi::ChatApiClient api;
            esp_err_t err = api.register_user(user_name, password);
            if (err != ESP_OK) {
                // Fallback: login if user already exists or registration failed
                api.login(user_name, password);
            }
        }
    };
};

class Oled {
   public:
    void BootDisplay() {
        printf("Booting!!!\n");

        lcd.init();
        lcd.clearDisplay();
        lcd.setRotation(2);
        lcd.fillScreen(0x000000u);

        sprite.createSprite(lcd.width(), lcd.height());

        sprite.drawBitmap(32, 0, mimocLogo, 64, 64, TFT_WHITE, TFT_BLACK);
        sprite.pushSprite(&lcd, 0, 0);

        // mopping_main();
    }

    void WatchDisplay() {
        // タイムゾーン設定（例：日本時間 JST-9）
        setenv("TZ", "JST-9", 1);
        tzset();

        lcd.init();
        lcd.clearDisplay();
        lcd.setRotation(2);
        lcd.fillScreen(0x000000u);

        sprite.createSprite(lcd.width(), lcd.height());

        Button type_button(GPIO_NUM_46);
        Button enter_button(GPIO_NUM_5);

        // 開始時間を取得 st=start_time
        long long int st = esp_timer_get_time();

        while (1) {
            Button::button_state_t type_button_state =
                type_button.get_button_state();
            Button::button_state_t enter_button_state =
                enter_button.get_button_state();

            // 入力イベント
            if (enter_button_state.pushed) {
                esp_deep_sleep_start();
            } else if (type_button_state.pushed) {
                break;
            }

            // 経過時間を取得
            int p_time = (esp_timer_get_time() - st) / 1000000;
            if (p_time > 5) {
                esp_deep_sleep_start();
            }

            // 現在時刻の取得と表示
            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);

            ESP_LOGI(TAG, "Current time: %04d/%02d/%02d %02d:%02d:%02d",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                     timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
                     timeinfo.tm_sec);

            sprite.fillScreen(0x000000u);
            char char_time[50];
            sprintf(char_time, "%04d/%02d/%02d %02d:%02d:%02d",
                    timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                    timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
                    timeinfo.tm_sec);
            sprite.drawCenterString(char_time, 64, 25);

            sprite.pushSprite(&lcd, 0, 0);
        }
    }

    void RecvNotif() {
        printf("RecvNotif!!!\n");

        sprite.fillRect(0, 0, 128, 64, 0);
        static constexpr char notif_text[] = "Recv!!";

        for (int i = 50; i >= 20; i--) {
            sprite.setCursor(25, i);   // カーソル位置を更新
            sprite.print(notif_text);  // 1バイトずつ出力
            // sprite.scroll(0, 0);  //
            // キャンバスの内容を1ドット上にスクロール
            sprite.pushSprite(&lcd, 0, 0);
        }

        for (int i = 20; i >= -50; i--) {
            sprite.setCursor(25, i);   // カーソル位置を更新
            sprite.print(notif_text);  // 1バイトずつ出力
            // sprite.scroll(0, 0);  //
            // キャンバスの内容を1ドット上にスクロール
            sprite.pushSprite(&lcd, 0, 0);
        }
    }

    void ShowImage(const unsigned char img[]) {
        lcd.init();
        // lcd.clearDisplay();
        lcd.setRotation(2);
        // lcd.fillScreen(0x000000u);

        sprite.createSprite(lcd.width(), lcd.height());
        sprite.fillScreen(0x000000u);
        // sprite.drawPixel(64, 32);

        sprite.drawBitmap(55, 25, img, 16, 22, TFT_WHITE, TFT_BLACK);
        // sprite.drawBitmap(32, 0, mimocLogo, 64, 64, TFT_WHITE,
        // TFT_BLACK);

        sprite.pushSprite(&lcd, 0, 0);
    }
};
