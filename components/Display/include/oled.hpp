#include <cstdio>
#include <iterator>
#include <string>
#include <cstdlib>
#include <algorithm>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <button.h>
#include <max98357a.h>
#include <gb_synth.hpp>
#include <boot_sounds.hpp>
#include <sound_settings.hpp>
#include <images.hpp>
#include <haptic_motor.hpp>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
#include "esp_ota_ops.h"

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
        buzzer.init();

        Joystick joystick;

        HttpClient http_client;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);
        // Enter long-press threshold: only for entering Save/Load (do not
        // change global default)
        enter_button.long_push_thresh = 300000;  // ~300ms

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
        // Ensure I2S is released even if we delete the task (avoid missing C++
        // destructors) so that re-entering Game can re-initialize audio
        // properly.
        {
            Max98357A
                cleanup;  // dummy handle to satisfy scope; does not allocate
        }
        // Explicitly deinit the buzzer used above
        // (declare as static_cast to silence unused warning if optimized)
        (void)0;  // no-op
        // Note: buzzer is still in scope here; ensure it is deinitialized
        // in case we exited without hitting earlier deinit paths.
        // (double-deinit is safe in our helper)
        buzzer.deinit();
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
    static std::string active_short_id;
    static std::string active_friend_id;

    static void set_active_contact(const std::string &short_id,
                                   const std::string &friend_id) {
        active_short_id = short_id;
        active_friend_id = friend_id;
        ESP_LOGI(TAG,
                 "[BLE] Active contact identifiers set (short=%s friend_id=%s)",
                 active_short_id.c_str(), active_friend_id.c_str());
    }

    void start_box_task(std::string chat_to) {
        printf("Start Box Task...");
        // Pass heap-allocated copy to avoid dangling pointer
        auto *arg = new std::string(chat_to);
        xTaskCreatePinnedToCore(&box_task, "box_task", 8012, arg, 6, NULL, 1);
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
        // Take ownership of heap arg and free after copy
        std::string chat_to = *(std::string *)pvParameters;
        delete (std::string *)pvParameters;
        ESP_LOGI(
            TAG,
            "[BLE] Opening message box (identifier=%s short=%s friend_id=%s)",
            chat_to.c_str(), active_short_id.c_str(), active_friend_id.c_str());
        JsonDocument res;
        auto fetch_messages_via_ble = [&](const std::string &fid,
                                          int timeout_ms) -> bool {
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
                std::string js = get_nvs((char *)"ble_messages");
                if (!js.empty()) {
                    ESP_LOGI(TAG, "[BLE] Received cached response (%zu bytes)",
                             js.size());
                    StaticJsonDocument<8192> in;
                    DeserializationError err = deserializeJson(in, js);
                    if (err == DeserializationError::Ok) {
                        int count = 0;
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
                            std::string outBuf;
                            serializeJson(in, outBuf);
                            deserializeJson(res, outBuf);
                            count = res["messages"].is<JsonArray>()
                                        ? res["messages"].as<JsonArray>().size()
                                        : 0;
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
                            count = arr.size();
                            std::string outBuf;
                            serializeJson(out, outBuf);
                            deserializeJson(res, outBuf);
                        } else if (in["payload"]["messages"].is<JsonArray>()) {
                            // Handle { type:..., payload: { messages:[...] } }
                            StaticJsonDocument<8192> out;
                            auto arr = out.createNestedArray("messages");
                            std::string my_id = get_nvs((char *)"user_id");
                            std::string my_name = get_nvs((char *)"user_name");
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
                                if (sender && !my_id.empty() &&
                                    my_id == sender) {
                                    o["from"] = my_name.c_str();
                                } else {
                                    o["from"] = fid.c_str();
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
                if (!fetch_messages_via_ble(chat_to, 4000)) {
                    ESP_LOGW(
                        TAG,
                        "[BLE] Refresh via BLE failed; using HTTP fallback");
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
        active_short_id.clear();
        active_friend_id.clear();
        vTaskDelete(NULL);
    };
};
bool MessageBox::running_flag = false;
std::string MessageBox::chat_title = "";
std::string MessageBox::active_short_id = "";
std::string MessageBox::active_friend_id = "";

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

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        int MAX_CONTACTS = 20;
        int CONTACT_PER_PAGE = 4;

        typedef struct {
            std::string display_name;  // username
            std::string
                identifier;  // preferred identifier (short_id if available)
            std::string short_id;
            std::string friend_id;
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
            // Allow more time for phone app to prepare friends list
            const int timeout_ms = 6000;
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
                                c.short_id = (sid && strlen(sid) > 0)
                                                 ? sid
                                                 : std::string("");
                                c.friend_id = fid ? fid : std::string("");
                                if (!c.short_id.empty())
                                    c.identifier = c.short_id;
                                else if (!c.friend_id.empty())
                                    c.identifier = c.friend_id;
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
            // Keep BLE connection alive and fall back to HTTP in parallel.
            // With NimBLE using PSRAM, Wi‑Fi coexists without disabling BLE.
            // HTTP fallback (Wi‑Fi). Ensure Wi‑Fi is connected.
            while (true) {
                bool connected = false;
                if (s_wifi_event_group) {
                    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
                    connected = bits & WIFI_CONNECTED_BIT;
                }
                // Actively check driver state as event bits may be stale
                wifi_ap_record_t ap = {};
                if (!connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                    connected = true;
                }
                // Try to resume Wi‑Fi driver if stopped
                if (!connected) {
                    (void)esp_wifi_start();
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

            auto& api = chatapi::shared_client(true);  // uses NVS server_host/port
            api.set_scheme("https");
            const auto creds = chatapi::load_credentials_from_nvs();
            (void)chatapi::ensure_authenticated(api, creds, false);
            std::string username = creds.username;
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
                        c.short_id =
                            (sid && strlen(sid) > 0) ? sid : std::string("");
                        c.friend_id = fid ? fid : std::string("");
                        if (!c.short_id.empty())
                            c.identifier = c.short_id;
                        else if (!c.friend_id.empty())
                            c.identifier = c.friend_id;
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

        MessageBox box;
        (void)box;
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
                        sprite.fillRect(0, 0, 128, 64, 0);
                        sprite.setFont(&fonts::Font2);
                        sprite.setTextColor(0xFFFFFFu, 0x000000u);
                        sprite.drawCenterString("Sending request...", 64, 22);
                        sprite.pushSprite(&lcd, 0, 0);

                        bool ok = false;
                        const char *emsg = nullptr;
                        if (ble_uart_is_ready()) {
                            long long rid = esp_timer_get_time();
                            std::string req =
                                std::string("{ \"id\":\"") +
                                std::to_string(rid) +
                                "\", \"type\": \"send_friend_request\", "
                                "\"code\": \"" +
                                friend_code + "\" }\n";
                            ble_uart_send(
                                reinterpret_cast<const uint8_t *>(req.c_str()),
                                req.size());
                            // wait for result in NVS
                            const int timeout_ms = 3000;
                            int waited = 0;
                            while (waited < timeout_ms) {
                                std::string rid_s =
                                    get_nvs((char *)"ble_result_id");
                                if (rid_s == std::to_string(rid)) {
                                    std::string j =
                                        get_nvs((char *)"ble_last_result");
                                    StaticJsonDocument<512> doc;
                                    if (deserializeJson(doc, j) ==
                                        DeserializationError::Ok) {
                                        ok = doc["ok"].as<bool>();
                                        if (doc["error"])
                                            emsg =
                                                doc["error"].as<const char *>();
                                    }
                                    break;
                                }
                                vTaskDelay(50 / portTICK_PERIOD_MS);
                                waited += 50;
                            }
                        } else {
                            auto& api = chatapi::shared_client(true);
                            api.set_scheme("https");
                            const auto creds = chatapi::load_credentials_from_nvs();
                            (void)chatapi::ensure_authenticated(api, creds, false);
                            std::string resp;
                            int status = 0;
                            auto err = api.send_friend_request(friend_code,
                                                               &resp, &status);
                            if (err == ESP_OK && status >= 200 && status < 300)
                                ok = true;
                            else if (err == ESP_OK && status >= 400) {
                                StaticJsonDocument<256> edoc;
                                if (deserializeJson(edoc, resp) ==
                                        DeserializationError::Ok &&
                                    edoc["error"]) {
                                    emsg = edoc["error"].as<const char *>();
                                }
                            }
                        }
                        sprite.fillRect(0, 0, 128, 64, 0);
                        sprite.setFont(&fonts::Font2);
                        if (ok)
                            sprite.drawCenterString("Request sent!", 64, 22);
                        else {
                            sprite.drawCenterString("Error:", 64, 16);
                            sprite.drawCenterString(emsg ? emsg : "Failed", 64,
                                                    34);
                        }
                        sprite.pushSprite(&lcd, 0, 0);
                        vTaskDelay(1200 / portTICK_PERIOD_MS);
                    }
                } else if (select_index == base_count + 1) {
                    // Pending Requests UI
                    type_button.clear_button_state();
                    joystick.reset_timer();
                    auto& api = chatapi::shared_client(true);
                    api.set_scheme("https");
                    const auto creds = chatapi::load_credentials_from_nvs();
                    (void)chatapi::ensure_authenticated(api, creds, false);

                    // Fetch pending (BLE first)
                    std::vector<std::pair<std::string, std::string>>
                        pending;  // {request_id, username}
                    if (ble_uart_is_ready()) {
                        long long rid = esp_timer_get_time();
                        std::string req = std::string("{ \"id\":\"") +
                                          std::to_string(rid) +
                                          "\", \"type\": \"get_pending\" }\n";
                        ble_uart_send(
                            reinterpret_cast<const uint8_t *>(req.c_str()),
                            req.size());
                        const int timeout_ms = 2500;
                        int waited = 0;
                        while (waited < timeout_ms) {
                            std::string js = get_nvs((char *)"ble_pending");
                            if (!js.empty()) {
                                StaticJsonDocument<4096> pdoc;
                                if (deserializeJson(pdoc, js) ==
                                    DeserializationError::Ok) {
                                    for (JsonObject r :
                                         pdoc["requests"].as<JsonArray>()) {
                                        std::string rid =
                                            r["request_id"].as<const char *>()
                                                ? r["request_id"]
                                                      .as<const char *>()
                                                : "";
                                        std::string uname =
                                            r["username"].as<const char *>()
                                                ? r["username"]
                                                      .as<const char *>()
                                                : "";
                                        if (!rid.empty())
                                            pending.push_back({rid, uname});
                                    }
                                    break;
                                }
                            }
                            vTaskDelay(100 / portTICK_PERIOD_MS);
                            waited += 100;
                        }
                    }
                    if (pending.empty()) {
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
                                        bool ok = false;
                                        if (ble_uart_is_ready()) {
                                            long long crid =
                                                esp_timer_get_time();
                                            std::string req =
                                                std::string("{ \"id\":\"") +
                                                std::to_string(crid) +
                                                "\", \"type\": "
                                                "\"respond_friend_request\", "
                                                "\"request_id\": \"" +
                                                rid + "\", \"accept\": " +
                                                (accept ? "true" : "false") +
                                                " }\n";
                                            ble_uart_send(reinterpret_cast<
                                                              const uint8_t *>(
                                                              req.c_str()),
                                                          req.size());
                                            const int timeout_ms = 2000;
                                            int waited = 0;
                                            while (waited < timeout_ms) {
                                                std::string rid_s = get_nvs(
                                                    (char *)"ble_result_id");
                                                if (rid_s ==
                                                    std::to_string(crid)) {
                                                    std::string j = get_nvs((
                                                        char *)"ble_last_"
                                                               "result");
                                                    StaticJsonDocument<512> dd;
                                                    if (deserializeJson(dd,
                                                                        j) ==
                                                        DeserializationError::
                                                            Ok)
                                                        ok =
                                                            dd["ok"].as<bool>();
                                                    break;
                                                }
                                                vTaskDelay(50 /
                                                           portTICK_PERIOD_MS);
                                                waited += 50;
                                            }
                                        } else {
                                            std::string rresp;
                                            int rstatus = 0;
                                            api.respond_friend_request(
                                                rid, accept, &rresp, &rstatus);
                                            ok = (rstatus >= 200 &&
                                                  rstatus < 300);
                                        }
                                        // Show outcome
                                        sprite.fillRect(0, 0, 128, 64, 0);
                                        sprite.setTextColor(0xFFFF, 0x0000);
                                        if (ok)
                                            sprite.drawCenterString("Done", 64,
                                                                    22);
                                        else
                                            sprite.drawCenterString("Failed",
                                                                    64, 22);
                                        sprite.pushSprite(&lcd, 0, 0);
                                        vTaskDelay(800 / portTICK_PERIOD_MS);
                                        // Refresh pending list
                                        pending.clear();
                                        // Re-fetch pending (BLE first)
                                        if (ble_uart_is_ready()) {
                                            long long rid2 =
                                                esp_timer_get_time();
                                            std::string req2 =
                                                std::string("{ \"id\":\"") +
                                                std::to_string(rid2) +
                                                "\", \"type\": \"get_pending\" "
                                                "}\n";
                                            ble_uart_send(reinterpret_cast<
                                                              const uint8_t *>(
                                                              req2.c_str()),
                                                          req2.size());
                                            const int timeout_ms = 1500;
                                            int waited = 0;
                                            while (waited < timeout_ms) {
                                                std::string js = get_nvs(
                                                    (char *)"ble_pending");
                                                if (!js.empty()) {
                                                    StaticJsonDocument<2048>
                                                        pdoc2;
                                                    if (deserializeJson(pdoc2,
                                                                        js) ==
                                                        DeserializationError::
                                                            Ok) {
                                                        for (
                                                            JsonObject r :
                                                            pdoc2["requests"]
                                                                .as<JsonArray>()) {
                                                            std::string rid2s =
                                                                r["request_id"]
                                                                        .as<const char
                                                                                *>()
                                                                    ? r["reques"
                                                                        "t_id"]
                                                                          .as<const char
                                                                                  *>()
                                                                    : "";
                                                            std::string uname2 =
                                                                r["username"]
                                                                        .as<const char
                                                                                *>()
                                                                    ? r["userna"
                                                                        "me"]
                                                                          .as<const char
                                                                                  *>()
                                                                    : "";
                                                            if (!rid2s.empty())
                                                                pending.push_back(
                                                                    {rid2s,
                                                                     uname2});
                                                        }
                                                    }
                                                    break;
                                                }
                                                vTaskDelay(100 /
                                                           portTICK_PERIOD_MS);
                                                waited += 100;
                                            }
                                        }
                                        if (pending.empty()) {
                                            std::string presp2;
                                            if (api.get_pending_requests(
                                                    presp2) == ESP_OK) {
                                                StaticJsonDocument<2048> pdoc2;
                                                if (deserializeJson(pdoc2,
                                                                    presp2) ==
                                                    DeserializationError::Ok) {
                                                    for (JsonObject r :
                                                         pdoc2["requests"]
                                                             .as<JsonArray>()) {
                                                        std::string rid2s =
                                                            r["request_id"]
                                                                    .as<const char
                                                                            *>()
                                                                ? r["request_"
                                                                    "id"]
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
                                                        if (!rid2s.empty())
                                                            pending.push_back(
                                                                {rid2s,
                                                                 uname2});
                                                    }
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
                    MessageBox::set_active_contact(
                        contacts[select_index].short_id,
                        contacts[select_index].friend_id);
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
        snprintf(char_ssid, sizeof(char_ssid), "%s",
                 reinterpret_cast<const char *>(uint_ssid));
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
        auto& api = chatapi::shared_client(true);
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

// Simple Game Boy-like step composer UI (2x Pulse + Noise)
class Composer {
   public:
    static bool running_flag;
    static TaskHandle_t s_play_task;
    static volatile bool s_abort;
    static volatile int s_play_pos_step;
    static long long s_pitch_popup_until;
    static char s_pitch_popup_text[16];
    static volatile int s_popup_kind;  // 0: note, 1: noise
    static volatile int s_popup_val;   // midi or noise index

    void start_composer_task() {
        xTaskCreatePinnedToCore(&composer_task, "composer_task", 16384, NULL, 6,
                                NULL, 1);
    }

    static void composer_task(void *pv) {
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

        // Pattern data (16 steps)
        static constexpr int STEPS = 16;
        int p1[STEPS];
        int p2[STEPS];
        int nz[STEPS];  // -1 off, else 0..7
        for (int i = 0; i < STEPS; ++i) {
            p1[i] = -1;
            p2[i] = -1;
            nz[i] = -1;
        }
        // Small default groove
        p1[0] = 60;
        p1[4] = 64;
        p1[8] = 67;
        p1[12] = 72;
        nz[4] = 3;
        nz[12] = 3;

        int tempo = 120;
        int cur_chan = 0;  // 0:SIN 1:SQR 2:NOI
        int cur_step = 0;
        int cur_note_p1 = 60;      // C4
        int cur_note_p2 = 67;      // G4
        int cur_noise = 1;         // default drum: SN (0=HH,1=SN,2=BD)
        int duty_idx1 = 2;         // 50%
        int duty_idx2 = 2;         // 50%
        bool noise_short = false;  // 7-bit flavor
        const float duties[4] = {0.125f, 0.25f, 0.5f, 0.75f};

        // playback control
        s_play_task = nullptr;
        s_abort = false;

        auto draw = [&]() {
            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            sprite.setFont(&fonts::Font2);

            // Header: BPM + play state only
            char hdr[40];
            bool playing = (s_play_task != nullptr) || (s_play_pos_step >= 0);
            snprintf(hdr, sizeof(hdr), "BPM:%d %s", tempo,
                     playing ? "PLAY" : "STOP");
            sprite.setCursor(2, 0);
            sprite.print(hdr);

            // Pitch readout (note names for selected step per channel)
            auto note_name = [](int midi) -> std::string {
                if (midi < 0) return std::string("--");
                static const char *names[12] = {"C",  "C#", "D",  "D#",
                                                "E",  "F",  "F#", "G",
                                                "G#", "A",  "A#", "B"};
                int n = midi % 12;
                if (n < 0) n += 12;
                int oct = midi / 12 - 1;  // MIDI 60 -> C4
                char buf[8];
                snprintf(buf, sizeof(buf), "%s%d", names[n], oct);
                return std::string(buf);
            };
            // remove inline pitch readout; show via popup only

            // Grid geometry (fit 128x64 nicely)
            const int label_w = 12;             // space for icons
            const int x0 = label_w + 2;         // grid left
            const int step_w = 7;               // 16 * 7 = 112px
            const int grid_w = step_w * STEPS;  // 112
            const int row_h = 14;               // larger rows
            const int y0 = 18;             // add a bit of space after header
            const int box_w = step_w - 2;  // 5
            const int box_h = row_h - 3;   // 11

            // Row icons (8x8 area)
            auto draw_icon = [&](int row, int type) {
                int ix = 2;
                int iy = y0 + row * row_h + (row_h - 8) / 2;
                if (type == 0) {
                    // SIN: approximate sine curve with small polyline
                    int px = ix;
                    int py = iy + 4;
                    for (int k = 0; k < 7; ++k) {
                        int nx = ix + k + 1;
                        // simple quarter-wave shape in 8px
                        float t = (k + 1) / 7.0f;
                        int ny = iy + 4 - (int)(3.0f * sinf(t * 3.14159f));
                        sprite.drawLine(px, py, nx, ny, 0xFFFF);
                        px = nx;
                        py = ny;
                    }
                } else if (type == 1) {
                    // SQR: square step
                    sprite.drawFastVLine(ix + 2, iy + 1, 6, 0xFFFF);
                    sprite.drawFastHLine(ix + 2, iy + 1, 4, 0xFFFF);
                    sprite.drawFastVLine(ix + 6, iy + 1, 6, 0xFFFF);
                    sprite.drawFastHLine(ix + 2, iy + 7, 4, 0xFFFF);
                } else {
                    // NOI: random-like dots pattern
                    sprite.drawPixel(ix + 1, iy + 2, 0xFFFF);
                    sprite.drawPixel(ix + 3, iy + 1, 0xFFFF);
                    sprite.drawPixel(ix + 5, iy + 4, 0xFFFF);
                    sprite.drawPixel(ix + 2, iy + 6, 0xFFFF);
                    sprite.drawPixel(ix + 6, iy + 3, 0xFFFF);
                }
            };
            draw_icon(0, 0);  // SIN
            draw_icon(1, 1);  // SQR
            draw_icon(2, 2);  // NOI

            // Group separators every 4 steps for readability
            for (int g = 0; g <= STEPS; g += 4) {
                int gx = x0 + g * step_w;
                if (gx >= x0 && gx <= x0 + grid_w) {
                    sprite.drawFastVLine(gx, y0 - 1, row_h * 3 + 2,
                                         0x7BEF /*gray*/);
                }
            }

            auto draw_row = [&](int row, const int *arr, bool noise = false) {
                for (int s = 0; s < STEPS; ++s) {
                    int x = x0 + s * step_w;
                    int y = y0 + row * row_h;
                    bool on = arr[s] >= 0;
                    bool sel = (s == cur_step) &&
                               ((cur_chan == row) || (noise && cur_chan == 2));

                    // Base cell
                    if (on) {
                        // Strongly filled cell (clearly ON)
                        sprite.fillRect(x, y, box_w, box_h, 0xFFFF);
                        if (sel) {
                            // selection: invert small bottom mark
                            sprite.fillRect(x + 1, y + box_h - 3, box_w - 2, 2,
                                            0x0000);
                        }
                    } else {
                        // outline cell
                        sprite.drawRect(x, y, box_w, box_h, 0xFFFF);
                        if (sel) {
                            // selection highlight: inner bar
                            sprite.fillRect(x + 1, y + 1, box_w - 2, box_h - 2,
                                            0x7BEF);
                        }
                    }
                }
            };

            draw_row(0, p1, false);
            draw_row(1, p2, false);
            draw_row(2, nz, true);

            // Draw playhead line when playing
            if (s_play_pos_step >= 0 && s_play_pos_step < STEPS) {
                int px = x0 + ((int)s_play_pos_step) * step_w;
                sprite.drawFastVLine(px, y0 - 1, row_h * 3 + 2, 0xFFFF);
            }

            // No footer: maximize grid area

            // Pitch popup dialog (on recent pitch change)
            if (esp_timer_get_time() < s_pitch_popup_until) {
                int bw = 110, bh = 32;
                int bx = (128 - bw) / 2;
                int by = (64 - bh) / 2;
                sprite.fillRoundRect(bx, by, bw, bh, 4, 0x0000);
                sprite.drawRoundRect(bx, by, bw, bh, 4, 0xFFFF);
                // Small note text
                sprite.setFont(&fonts::Font2);
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                sprite.drawCenterString(s_pitch_popup_text, 64, by + 2);
                // Bar zone
                int barx = bx + 8, bary = by + 18, barw = bw - 16, barh = 8;
                sprite.drawRect(barx, bary, barw, barh, 0xFFFF);
                if (s_popup_kind == 0) {
                    // Single-octave piano keyboard (C..B)
                    // White keys: C D E F G A B (7 keys equally spaced)
                    int ww = barw / 7;
                    if (ww < 4) ww = 4;       // minimal width
                    int rem = barw - ww * 7;  // leftover pixels to distribute
                    int x = barx;
                    int white_x[7];
                    int white_w[7];
                    for (int i = 0; i < 7; i++) {
                        int w = ww + (i < rem ? 1 : 0);
                        white_x[i] = x;
                        white_w[i] = w;
                        // draw white key outline
                        sprite.drawRect(x, bary, w - 1, barh, 0xFFFF);
                        x += w;
                    }
                    // Black keys: C#,D#,F#,G#,A# over corresponding gaps
                    auto draw_black = [&](int left_white_idx) {
                        int lw = left_white_idx;
                        int rw = left_white_idx + 1;
                        if (lw < 0 || rw > 6) return;
                        int cx = (white_x[lw] + white_w[lw] / 2 + white_x[rw] +
                                  white_w[rw] / 2) /
                                 2;
                        int bw = std::min(white_w[lw], white_w[rw]) * 2 / 3;
                        if (bw < 2) bw = 2;
                        int bx2 = cx - bw / 2;
                        int bh = (barh * 3) / 5;
                        sprite.fillRect(bx2, bary, bw, bh, 0xFFFF);
                    };
                    // place black keys at gaps (C# between C-D -> 0, D# -> 1,
                    // skip E-F, F#->3, G#->4, A#->5)
                    draw_black(0);
                    draw_black(1); /*E-F gap none*/
                    draw_black(3);
                    draw_black(4);
                    draw_black(5);
                    // Current note caret (map to single octave by semitone
                    // index)
                    int t = s_popup_val % 12;
                    if (t < 0) t += 12;
                    // Determine caret center
                    int caret_x = barx;
                    auto white_center = [&](int wi) {
                        return white_x[wi] + white_w[wi] / 2;
                    };
                    switch (t) {
                        case 0:
                            caret_x = white_center(0);
                            break;  // C
                        case 2:
                            caret_x = white_center(1);
                            break;  // D
                        case 4:
                            caret_x = white_center(2);
                            break;  // E
                        case 5:
                            caret_x = white_center(3);
                            break;  // F
                        case 7:
                            caret_x = white_center(4);
                            break;  // G
                        case 9:
                            caret_x = white_center(5);
                            break;  // A
                        case 11:
                            caret_x = white_center(6);
                            break;  // B
                        case 1:
                            caret_x = (white_center(0) + white_center(1)) / 2;
                            break;  // C#
                        case 3:
                            caret_x = (white_center(1) + white_center(2)) / 2;
                            break;  // D#
                        case 6:
                            caret_x = (white_center(3) + white_center(4)) / 2;
                            break;  // F#
                        case 8:
                            caret_x = (white_center(4) + white_center(5)) / 2;
                            break;  // G#
                        case 10:
                            caret_x = (white_center(5) + white_center(6)) / 2;
                            break;  // A#
                    }
                    sprite.drawFastVLine(caret_x, bary - 3, 3, 0xFFFF);
                } else {
                    // drums: 3 segments (HH/SN/BD)
                    int segw = barw / 3;
                    for (int i = 0; i < 3; i++) {
                        int rx = barx + i * segw;
                        sprite.drawRect(rx, bary, segw - 1, barh, 0x7BEF);
                        if (i == s_popup_val)
                            sprite.fillRect(rx + 1, bary + 1, segw - 3,
                                            barh - 2, 0xFFFF);
                    }
                }
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
            }

            sprite.pushSprite(&lcd, 0, 0);
        };

        draw();

        auto clamp_midi = [](int n) { return std::max(36, std::min(84, n)); };

        while (1) {
            Joystick::joystick_state_t js = joystick.get_joystick_state();
            Button::button_state_t tb = type_button.get_button_state();
            Button::button_state_t bb = back_button.get_button_state();
            Button::button_state_t eb = enter_button.get_button_state();

            // Exit (Back button only)
            if (bb.pushed) {
                break;
            }

            // (Enter+Left/Right for tempo is disabled; use Type+Left/Right)

            // Move step
            if (js.pushed_left_edge && !eb.pushing) {
                cur_step = (cur_step + STEPS - 1) % STEPS;
                draw();
            } else if (js.pushed_right_edge && !tb.pushing && !eb.pushing) {
                cur_step = (cur_step + 1) % STEPS;
                draw();
            }

            // While holding Type: tempo change (Left/Right)
            if (tb.pushing && js.pushed_right_edge) {
                tempo = std::min(440, tempo + 5);
                draw();
            }
            if (tb.pushing && js.pushed_left_edge) {
                tempo = std::max(40, tempo - 5);
                draw();
            }

            // Pitch adjust on up/down (only when holding Type)
            if (tb.pushing && js.pushed_up_edge) {
                if (cur_chan == 0) {
                    if (p1[cur_step] >= 0)
                        p1[cur_step] = clamp_midi(p1[cur_step] + 1);
                    else
                        cur_note_p1 = clamp_midi(cur_note_p1 + 1);
                } else if (cur_chan == 1) {
                    if (p2[cur_step] >= 0)
                        p2[cur_step] = clamp_midi(p2[cur_step] + 1);
                    else
                        cur_note_p2 = clamp_midi(cur_note_p2 + 1);
                } else {
                    if (nz[cur_step] >= 0)
                        nz[cur_step] = std::min(2, nz[cur_step] + 1);
                    else
                        cur_noise = std::min(2, cur_noise + 1);
                }
                // pitch popup
                auto note_name = [](int midi) -> std::string {
                    if (midi < 0) return std::string("--");
                    static const char *names[12] = {"C",  "C#", "D",  "D#",
                                                    "E",  "F",  "F#", "G",
                                                    "G#", "A",  "A#", "B"};
                    int n = midi % 12;
                    if (n < 0) n += 12;
                    int oct = midi / 12 - 1;
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%s%d", names[n], oct);
                    return std::string(buf);
                };
                std::string label;
                if (cur_chan == 2) {
                    const char *nm =
                        (nz[cur_step] == 0 ? "HH"
                                           : (nz[cur_step] == 1 ? "SN" : "BD"));
                    char b[8];
                    snprintf(b, sizeof(b), "%s", nm);
                    label = b;
                    s_popup_kind = 1;
                    s_popup_val = nz[cur_step];
                } else {
                    label =
                        note_name(cur_chan == 0 ? p1[cur_step] : p2[cur_step]);
                    s_popup_kind = 0;
                    s_popup_val = (cur_chan == 0 ? p1[cur_step] : p2[cur_step]);
                }
                strncpy(s_pitch_popup_text, label.c_str(),
                        sizeof(s_pitch_popup_text) - 1);
                s_pitch_popup_text[sizeof(s_pitch_popup_text) - 1] = '\0';
                s_pitch_popup_until = esp_timer_get_time() + 900000;  // 900ms
                draw();
            } else if (tb.pushing && js.pushed_down_edge) {
                if (cur_chan == 0) {
                    if (p1[cur_step] >= 0)
                        p1[cur_step] = clamp_midi(p1[cur_step] - 1);
                    else
                        cur_note_p1 = clamp_midi(cur_note_p1 - 1);
                } else if (cur_chan == 1) {
                    if (p2[cur_step] >= 0)
                        p2[cur_step] = clamp_midi(p2[cur_step] - 1);
                    else
                        cur_note_p2 = clamp_midi(cur_note_p2 - 1);
                } else {
                    if (nz[cur_step] >= 0)
                        nz[cur_step] = std::max(0, nz[cur_step] - 1);
                    else
                        cur_noise = std::max(0, cur_noise - 1);
                }
                auto note_name = [](int midi) -> std::string {
                    if (midi < 0) return std::string("--");
                    static const char *names[12] = {"C",  "C#", "D",  "D#",
                                                    "E",  "F",  "F#", "G",
                                                    "G#", "A",  "A#", "B"};
                    int n = midi % 12;
                    if (n < 0) n += 12;
                    int oct = midi / 12 - 1;
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%s%d", names[n], oct);
                    return std::string(buf);
                };
                std::string label;
                if (cur_chan == 2) {
                    const char *nm =
                        (nz[cur_step] == 0 ? "HH"
                                           : (nz[cur_step] == 1 ? "SN" : "BD"));
                    char b[8];
                    snprintf(b, sizeof(b), "%s", nm);
                    label = b;
                    s_popup_kind = 1;
                    s_popup_val = nz[cur_step];
                } else {
                    label =
                        note_name(cur_chan == 0 ? p1[cur_step] : p2[cur_step]);
                    s_popup_kind = 0;
                    s_popup_val = (cur_chan == 0 ? p1[cur_step] : p2[cur_step]);
                }
                strncpy(s_pitch_popup_text, label.c_str(),
                        sizeof(s_pitch_popup_text) - 1);
                s_pitch_popup_text[sizeof(s_pitch_popup_text) - 1] = '\0';
                s_pitch_popup_until = esp_timer_get_time() + 900000;
                draw();
            }

            // Channel change on Up/Down (without Type)
            if (!tb.pushing && js.pushed_up_edge) {
                cur_chan = (cur_chan + 2) % 3;
                draw();
            }
            if (!tb.pushing && js.pushed_down_edge) {
                cur_chan = (cur_chan + 1) % 3;
                draw();
            }

            // Toggle note on/off
            if (tb.pushed && !tb.pushed_same_time) {
                if (tb.push_type == 'l') {
                    // Long: change channel
                    cur_chan = (cur_chan + 1) % 3;
                } else {
                    if (cur_chan == 0) {
                        if (p1[cur_step] >= 0)
                            p1[cur_step] = -1;
                        else
                            p1[cur_step] = cur_note_p1;
                    } else if (cur_chan == 1) {
                        if (p2[cur_step] >= 0)
                            p2[cur_step] = -1;
                        else
                            p2[cur_step] = cur_note_p2;
                    } else {
                        if (nz[cur_step] >= 0)
                            nz[cur_step] = -1;
                        else
                            nz[cur_step] = std::min(2, std::max(0, cur_noise));
                    }
                }
                draw();
                type_button.clear_button_state();
            }

            // Save/Load dialog (Enter long)
            if (eb.pushed && eb.push_type == 'l') {
                // Clear current press/release events to avoid immediate confirm
                enter_button.clear_button_state();
                type_button.clear_button_state();
                back_button.clear_button_state();
                joystick.reset_timer();
                int slot = 1;
                bool save_mode = true;  // true: Save, false: Load
                while (1) {
                    // draw dialog
                    sprite.fillRect(0, 0, 128, 64, 0);
                    sprite.setFont(&fonts::Font2);
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    sprite.drawCenterString(
                        save_mode ? "Save Song" : "Load Song", 64, 4);
                    // mode toggle hint
                    sprite.drawCenterString("Type:Toggle  Enter:Confirm", 64,
                                            18);
                    // slot buttons
                    for (int i = 1; i <= 3; i++) {
                        int x = 10 + (i - 1) * 38;
                        int y = 32;
                        int w = 34;
                        int h = 18;
                        bool sel = (slot == i);
                        sprite.fillRoundRect(x, y, w, h, 3,
                                             sel ? 0xFFFF : 0x0000);
                        sprite.drawRoundRect(x, y, w, h, 3, 0xFFFF);
                        sprite.setTextColor(sel ? 0x000000u : 0xFFFFFFu,
                                            sel ? 0xFFFFu : 0x0000u);
                        char lab[8];
                        snprintf(lab, sizeof(lab), "S%d", i);
                        sprite.drawCenterString(lab, x + w / 2, y + 2);
                    }
                    sprite.pushSprite(&lcd, 0, 0);

                    // input
                    auto js2 = joystick.get_joystick_state();
                    auto tb2 = type_button.get_button_state();
                    auto bb2 = back_button.get_button_state();
                    if (js2.pushed_left_edge) {
                        slot = (slot == 1) ? 3 : slot - 1;
                    }
                    if (js2.pushed_right_edge) {
                        slot = (slot == 3) ? 1 : slot + 1;
                    }
                    if (bb2.pushed) break;
                    if (tb2.pushed) {
                        save_mode = !save_mode;
                        type_button.clear_button_state();
                    }
                    if (enter_button.get_button_state().pushed) {
                        if (save_mode) {
                            // serialize pattern and save
                            std::string s;
                            s += "tempo=" + std::to_string(tempo) + ";";
                            s += "d2=" + std::to_string(duty_idx2) + ";";
                            s += std::string("ns=") +
                                 (noise_short ? "1" : "0") + ";";
                            auto arr = [&](const char *key, const int *a) {
                                s += key;
                                s += "=";
                                for (int i = 0; i < STEPS; i++) {
                                    s += std::to_string(a[i]);
                                    if (i != STEPS - 1) s += ",";
                                }
                                s += ";";
                            };
                            arr("p1", p1);
                            arr("p2", p2);
                            arr("nz", nz);
                            save_nvs(
                                (char *)(slot == 1
                                             ? "song1"
                                             : (slot == 2 ? "song2" : "song3")),
                                s);
                            sprite.fillRect(0, 0, 128, 64, 0);
                            sprite.setFont(&fonts::Font2);
                            sprite.setTextColor(0xFFFFFFu, 0x000000u);
                            char m[20];
                            snprintf(m, sizeof(m), "Saved S%d", slot);
                            sprite.drawCenterString(m, 64, 22);
                            sprite.pushSprite(&lcd, 0, 0);
                            vTaskDelay(600 / portTICK_PERIOD_MS);
                            break;
                        } else {
                            // load from NVS
                            chiptune::Pattern lpat;
                            int ltempo = tempo;
                            int ld2 = duty_idx2;
                            bool lns = noise_short;
                            if (boot_sounds::load_song_from_nvs(
                                    slot, lpat, ltempo, ld2, lns)) {
                                // apply
                                for (int i = 0; i < STEPS; i++) {
                                    p1[i] = (i < (int)lpat.pulse1.size()
                                                 ? lpat.pulse1[i]
                                                 : -1);
                                }
                                for (int i = 0; i < STEPS; i++) {
                                    p2[i] = (i < (int)lpat.pulse2.size()
                                                 ? lpat.pulse2[i]
                                                 : -1);
                                }
                                for (int i = 0; i < STEPS; i++) {
                                    nz[i] = (i < (int)lpat.noise.size()
                                                 ? lpat.noise[i]
                                                 : -1);
                                }
                                tempo = std::max(40, std::min(440, ltempo));
                                duty_idx2 = std::max(0, std::min(3, ld2));
                                noise_short = lns;
                                // confirmation
                                sprite.fillRect(0, 0, 128, 64, 0);
                                sprite.setFont(&fonts::Font2);
                                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                                char m[20];
                                snprintf(m, sizeof(m), "Loaded S%d", slot);
                                sprite.drawCenterString(m, 64, 22);
                                sprite.pushSprite(&lcd, 0, 0);
                                vTaskDelay(600 / portTICK_PERIOD_MS);
                                draw();
                                break;
                            } else {
                                // not found
                                sprite.fillRect(0, 0, 128, 64, 0);
                                sprite.setFont(&fonts::Font2);
                                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                                sprite.drawCenterString("No Data", 64, 22);
                                sprite.pushSprite(&lcd, 0, 0);
                                vTaskDelay(600 / portTICK_PERIOD_MS);
                            }
                        }
                    }
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }
                enter_button.clear_button_state();
                joystick.reset_timer();
                type_button.reset_timer();
            }

            // Play/Stop（Enter短押し）
            if (eb.pushed && eb.push_type == 's' && s_play_task == nullptr) {
                // Copy pattern to heap for playback task
                struct PlayArgs {
                    chiptune::Pattern pat;
                    int bpm;
                    float d1;
                    float d2;
                    bool nshort;
                    volatile bool *abortp;
                };
                PlayArgs *args = (PlayArgs *)heap_caps_malloc(
                    sizeof(PlayArgs), MALLOC_CAP_DEFAULT);
                new (args) PlayArgs();
                args->pat.steps = STEPS;
                args->pat.pulse1.assign(p1, p1 + STEPS);
                args->pat.pulse2.assign(p2, p2 + STEPS);
                args->pat.noise.assign(nz, nz + STEPS);
                args->bpm = tempo;
                args->d1 = duties[duty_idx1];
                args->d2 = duties[duty_idx2];
                args->nshort = noise_short;
                args->abortp = &s_abort;

                auto task = +[](void *pv) {
                    PlayArgs *a = (PlayArgs *)pv;
                    Max98357A spk(40, 39, 41, 44100);  // unify with system rate
                    chiptune::GBSynth synth(spk.sample_rate);
                    // Streaming render to DMA buffers (no large PSRAM
                    // allocations)
                    struct Ctx {
                        chiptune::GBSynth *synth;
                        const chiptune::Pattern *pat;
                        int bpm;
                        float d1;
                        float d2;
                        bool ns;
                        chiptune::GBSynth::StreamState st;
                    } ctx{&synth, &a->pat, a->bpm, a->d1, a->d2, a->nshort, {}};

                    auto fill =
                        +[](int16_t *dst, size_t max, void *u) -> size_t {
                        Ctx *c = (Ctx *)u;
                        // ch1 = Sine, ch2 = Square, noise as configured
                        size_t w = c->synth->render_block(
                            *c->pat, c->bpm, c->d1, c->d2, c->ns, c->st, dst,
                            max, /*ch1_sine=*/true);
                        int step = c->st.step;
                        int last = c->pat->steps - 1;
                        if (step > last) step = last;
                        Composer::s_play_pos_step = (w > 0) ? step : -1;
                        return w;
                    };
                    // Total samples to render
                    const float step_sec = 60.0f / (float)a->bpm / 4.0f;
                    const int step_samples = std::max(
                        1, (int)std::round(step_sec * spk.sample_rate));
                    const size_t total_samples =
                        (size_t)step_samples * (size_t)a->pat.steps;
                    spk.play_pcm_mono16_stream(total_samples, 1.0f, a->abortp,
                                               fill, &ctx);
                    a->~PlayArgs();
                    heap_caps_free(a);
                    Composer::s_play_task = nullptr;
                    // Explicitly release I2S channel before task exit so others
                    // can use audio
                    spk.deinit();
                    vTaskDelete(NULL);
                };
                xTaskCreatePinnedToCore(task, "compose_play", 4096, args, 5,
                                        &s_play_task, 1);
                // Do not block UI; task will self-delete. We can't easily reset
                // handle here, so we just ignore until user presses again after
                // a while. A tiny debounce
                vTaskDelay(30 / portTICK_PERIOD_MS);
            }

            // Continuous redraw to animate playhead / popup
            draw();
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        // Ensure playback stopped before exit
        s_abort = true;
        int wait_ms = 0;
        while (s_play_task != nullptr && wait_ms < 1500) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            wait_ms += 10;
        }
        s_play_pos_step = -1;
        running_flag = false;
        vTaskDelete(NULL);
    }
};
bool Composer::running_flag = false;
TaskHandle_t Composer::s_play_task = nullptr;
volatile bool Composer::s_abort = false;
volatile int Composer::s_play_pos_step = -1;
long long Composer::s_pitch_popup_until = 0;
char Composer::s_pitch_popup_text[16] = {0};
volatile int Composer::s_popup_kind = 0;
volatile int Composer::s_popup_val = 60;

class SettingMenu {
   public:
    static bool running_flag;
    static bool sound_dirty;

    void start_message_menue_task() {
        printf("Start MessageMenue Task...");
        BaseType_t ok = xTaskCreatePinnedToCore(
            &message_menue_task, "message_menue_task", 16192, NULL, 6, NULL,
            1);
        if (ok != pdPASS) {
            ESP_LOGE("SETTING_MENU", "Failed to start message menu task (err=%ld)",
                     static_cast<long>(ok));
            running_flag = false;
            return;
        }
        running_flag = true;
    }

    static void message_menue_task(void *pvParameters) {
        lcd.init();

        WiFiSetting wifi_setting;

        Max98357A buzzer;

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

        // Settings list
        setting_t settings[13] = {
            {"Profile"},      {"Wi-Fi"},         {"Bluetooth"},
            {"Sound"},        {"Boot Sound"},    {"Real Time Chat"},
            {"Composer"},     {"Auto Update"},   {"OTA Manifest"},
            {"Update Now"},   {"Firmware Info"}, {"Develop"},
            {"Factory Reset"}};

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
                } else if (settings[i].setting_name == "Sound") {
                    bool on = sound_settings::enabled();
                    int vol_pct = static_cast<int>(sound_settings::volume() * 100.0f + 0.5f);
                    char label[40];
                    std::snprintf(label, sizeof(label), "Sound [%s, %d%%]",
                                  on ? "ON" : "OFF", vol_pct);
                    sprite.print(label);
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
                } else if (settings[i].setting_name == "Bluetooth") {
                    std::string label = "Bluetooth";
                    bool connected = ble_uart_is_ready();
                    std::string pairing = get_nvs((char *)"ble_pair");
                    if (connected)
                        label += " [Connected]";
                    else if (pairing == "true")
                        label += " [PAIRING]";
                    sprite.print(label.c_str());
                } else if (settings[i].setting_name == "Boot Sound") {
                    std::string bs = get_nvs((char *)"boot_sound");
                    if (bs.empty()) bs = "cute";
                    std::string shown =
                        bs == std::string("majestic")
                            ? "Majestic"
                            : (bs == std::string("random") ? "Random" : "Cute");
                    std::string label =
                        std::string("Boot Sound [") + shown + "]";
                    sprite.print(label.c_str());
                } else if (settings[i].setting_name == "Firmware Info") {
                    sprite.print("Firmware Info");
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
                       settings[select_index].setting_name == "Sound") {
                type_button.clear_button_state();
                enter_button.clear_button_state();
                back_button.clear_button_state();
                joystick.reset_timer();

                bool enabled = sound_settings::enabled();
                float volume = sound_settings::volume();

                while (1) {
                    sprite.fillRect(0, 0, 128, 64, 0);
                    sprite.setFont(&fonts::Font2);
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    sprite.drawCenterString("Sound Settings", 64, 6);

                    char status[32];
                    std::snprintf(status, sizeof(status), "Status: %s",
                                  enabled ? "ON" : "OFF");
                    sprite.drawCenterString(status, 64, 22);

                    int vol_pct = static_cast<int>(volume * 100.0f + 0.5f);
                    char vol_text[32];
                    std::snprintf(vol_text, sizeof(vol_text), "Volume: %d%%",
                                  vol_pct);
                    sprite.drawCenterString(vol_text, 64, 36);

                    sprite.setFont(&fonts::Font2);
                    sprite.drawCenterString("Type:Toggle  Up/Down:Vol", 64, 50);
                    sprite.drawCenterString("Back/Enter:Exit", 64, 58);
                    sprite.pushSprite(&lcd, 0, 0);

                    auto tbs = type_button.get_button_state();
                    auto ebs = enter_button.get_button_state();
                    auto bbs = back_button.get_button_state();
                    auto js = joystick.get_joystick_state();

                    if (tbs.pushed) {
                        enabled = !enabled;
                        sound_settings::set_enabled(enabled, false);
                        SettingMenu::sound_dirty = true;
                        type_button.clear_button_state();
                        type_button.reset_timer();
                    }

                    const float step = 0.05f;
                    bool volume_changed = false;
                    if (js.pushed_up_edge || js.pushed_right_edge) {
                        volume = std::min(1.0f, volume + step);
                        volume_changed = true;
                    } else if (js.pushed_down_edge || js.pushed_left_edge) {
                        volume = std::max(0.0f, volume - step);
                        volume_changed = true;
                    }
                    if (volume_changed) {
                        sound_settings::set_volume(volume, false);
                        SettingMenu::sound_dirty = true;
                        volume = sound_settings::volume();
                    }

                    if (ebs.pushed || bbs.pushed) {
                        enter_button.clear_button_state();
                        back_button.clear_button_state();
                        break;
                    }

                    vTaskDelay(50 / portTICK_PERIOD_MS);
                }

                joystick.reset_timer();
                type_button.clear_button_state();
                type_button.reset_timer();
                if (SettingMenu::sound_dirty) {
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }
            } else if (type_button_state.pushed &&
                       settings[select_index].setting_name == "Boot Sound") {
                // Simple boot sound selector: Type=cycle, Enter=preview,
                // Back=save
                type_button.clear_button_state();
                enter_button.clear_button_state();
                back_button.clear_button_state();
                joystick.reset_timer();

                std::vector<std::string> opts = {"cute", "majestic", "gb",
                                                 "random"};
                if (!get_nvs((char *)"song1").empty()) opts.push_back("song1");
                if (!get_nvs((char *)"song2").empty()) opts.push_back("song2");
                if (!get_nvs((char *)"song3").empty()) opts.push_back("song3");
                auto idx_of = [&](const std::string &s) {
                    for (size_t i = 0; i < opts.size(); ++i)
                        if (opts[i] == s) return (int)i;
                    return 0;
                };
                std::string cur = get_nvs((char *)"boot_sound");
                if (cur.empty()) cur = "cute";
                int idx = idx_of(cur);
                while (1) {
                    sprite.fillRect(0, 0, 128, 64, 0);
                    sprite.setFont(&fonts::Font2);
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    sprite.drawCenterString("Boot Sound", 64, 6);
                    std::string name;
                    if (opts[idx] == "majestic")
                        name = "Majestic";
                    else if (opts[idx] == "gb")
                        name = "GB Synth";
                    else if (opts[idx] == "random")
                        name = "Random";
                    else if (opts[idx] == "song1")
                        name = "Song 1";
                    else if (opts[idx] == "song2")
                        name = "Song 2";
                    else if (opts[idx] == "song3")
                        name = "Song 3";
                    else
                        name = "Cute";
                    sprite.drawCenterString(name.c_str(), 64, 24);
                    sprite.drawCenterString("Type:Next  Enter:Preview", 64, 40);
                    sprite.drawCenterString("Back:Save", 64, 52);
                    sprite.pushSprite(&lcd, 0, 0);

                    auto tbs = type_button.get_button_state();
                    auto ebs = enter_button.get_button_state();
                    auto bbs = back_button.get_button_state();
                    if (bbs.pushed || joystick.get_joystick_state().left) {
                        save_nvs((char *)"boot_sound", opts[idx]);
                        type_button.clear_button_state();
                        enter_button.clear_button_state();
                        back_button.clear_button_state();
                        break;
                    }
                    if (tbs.pushed) {
                        idx = (idx + 1) % (int)opts.size();
                        type_button.clear_button_state();
                    }
                    if (ebs.pushed) {
                        Max98357A sp;
                        if (opts[idx] == "majestic")
                            boot_sounds::play_majestic(sp, 0.5f);
                        else if (opts[idx] == "gb")
                            boot_sounds::play_gb(sp, 0.9f);
                        else if (opts[idx] == "song1")
                            boot_sounds::play_song(sp, 1, 0.9f);
                        else if (opts[idx] == "song2")
                            boot_sounds::play_song(sp, 2, 0.9f);
                        else if (opts[idx] == "song3")
                            boot_sounds::play_song(sp, 3, 0.9f);
                        else if (opts[idx] == "random") {
                            uint32_t r = (uint32_t)(esp_timer_get_time() & 3);
                            if (r == 0)
                                boot_sounds::play_cute(sp, 0.5f);
                            else if (r == 1)
                                boot_sounds::play_majestic(sp, 0.5f);
                            else
                                boot_sounds::play_gb(sp, 0.9f);
                        } else
                            boot_sounds::play_cute(sp, 0.5f);
                        enter_button.clear_button_state();
                    }
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }
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
                       settings[select_index].setting_name == "Firmware Info") {
                // Consume the button press before entering the info loop to
                // avoid instant exit
                type_button.clear_button_state();
                type_button.reset_timer();
                back_button.clear_button_state();
                back_button.reset_timer();
                vTaskDelay(pdMS_TO_TICKS(120));
                // Show version and partitions until user exits
                while (1) {
                    // Fetch info
                    const esp_app_desc_t *app = esp_app_get_description();
                    const esp_partition_t *running =
                        esp_ota_get_running_partition();
                    const esp_partition_t *boot = esp_ota_get_boot_partition();
                    const char *ver = app ? app->version : "unknown";
                    const char *run_label = running ? running->label : "-";
                    const char *boot_label = boot ? boot->label : "-";

                    sprite.fillRect(0, 0, 128, 64, 0);
                    sprite.setFont(&fonts::Font2);
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    sprite.drawCenterString("Firmware Info", 64, 0);

                    char line[64];
                    snprintf(line, sizeof(line), "Ver: %s", ver);
                    sprite.drawString(line, 2, 16);
                    if (running) {
                        snprintf(line, sizeof(line), "Run: %s @%06lx",
                                 run_label, (unsigned long)running->address);
                    } else {
                        snprintf(line, sizeof(line), "Run: -");
                    }
                    sprite.drawString(line, 2, 28);
                    if (boot) {
                        snprintf(line, sizeof(line), "Boot:%s @%06lx",
                                 boot_label, (unsigned long)boot->address);
                    } else {
                        snprintf(line, sizeof(line), "Boot: -");
                    }
                    sprite.drawString(line, 2, 40);

                    // Show short hint
                    sprite.drawString("Back=Exit", 2, 54);
                    sprite.pushSprite(&lcd, 0, 0);

                    // Exit when back or type pressed
                    Button::button_state_t tbs = type_button.get_button_state();
                    Button::button_state_t bbs = back_button.get_button_state();
                    if (tbs.pushed || bbs.pushed) {
                        type_button.clear_button_state();
                        type_button.reset_timer();
                        back_button.clear_button_state();
                        back_button.reset_timer();
                        break;
                    }
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }
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
                       settings[select_index].setting_name == "Composer") {
                Composer comp;
                comp.running_flag = true;
                comp.start_composer_task();
                while (comp.running_flag) {
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                sprite.setFont(&fonts::Font2);
                type_button.clear_button_state();
                type_button.reset_timer();
                joystick.reset_timer();
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
bool SettingMenu::sound_dirty = false;

class Game {
   public:
    static constexpr uint32_t kTaskStackWords = 12288;  // 48 KB task stack
    static bool running_flag;

    void start_game_task() {
        printf("Start Game Task...");
        BaseType_t ok = xTaskCreatePinnedToCore(
            &game_task, "game_task", kTaskStackWords, NULL, 6, NULL, 1);
        if (ok != pdPASS) {
            ESP_LOGE("GAME", "Failed to start game task (err=%ld)",
                     static_cast<long>(ok));
            running_flag = false;
            return;
        }
        running_flag = true;
    }

    static std::map<std::string, std::string> morse_code;
    static std::map<std::string, std::string> morse_code_reverse;
    static void game_task(void *pvParameters) {
        (void)pvParameters;
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
            MenuChoice choice = show_game_menu(joystick, type_button,
                                               back_button, enter_button);
            bool exit_requested = false;

            switch (choice) {
                case MenuChoice::kExit:
                    exit_requested = true;
                    break;
                case MenuChoice::kMopping:
                    exit_requested = run_mopping_game();
                    break;
                case MenuChoice::kMorse:
                    exit_requested = run_morse_trainer(
                        joystick, type_button, back_button, enter_button);
                    break;
            }

            reset_inputs(joystick, type_button, back_button, enter_button);
            if (exit_requested) {
                exit_task = true;
            }
        }

        UBaseType_t watermark_words = uxTaskGetStackHighWaterMark(nullptr);
        ESP_LOGI(
            "WASM_GAME",
            "game_task stack high watermark: %u words (%u bytes)%s",
            static_cast<unsigned int>(watermark_words),
            static_cast<unsigned int>(watermark_words * sizeof(StackType_t)),
            watermark_words == 0 ? " [LOW]" : "");

        running_flag = false;
        vTaskDelete(NULL);
    };

   private:
    enum class MenuChoice { kMopping = 0, kMorse = 1, kExit = 2 };

    static MenuChoice show_game_menu(Joystick &joystick, Button &type_button,
                                     Button &back_button,
                                     Button &enter_button) {
        reset_inputs(joystick, type_button, back_button, enter_button);

        const char *items[] = {"Mopping (Wasm)", "Morse Trainer"};
        int index = 0;

        while (true) {
            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.setFont(&fonts::Font2);
            sprite.setTextColor(0xFFFF, 0x0000);
            sprite.drawCenterString("Choose Game", 64, 6);

            for (int i = 0; i < 2; ++i) {
                int y = 24 + i * 18;
                if (i == index) {
                    sprite.fillRoundRect(8, y - 4, 112, 18, 3, 0xFFFF);
                    sprite.setTextColor(0x0000, 0xFFFF);
                } else {
                    sprite.setTextColor(0xFFFF, 0x0000);
                }
                sprite.drawCenterString(items[i], 64, y);
            }

            sprite.setTextColor(0xFFFF, 0x0000);
            sprite.drawCenterString("Type=Select  Back=Exit", 64, 54);
            sprite.pushSprite(&lcd, 0, 0);

            Joystick::joystick_state_t joy = joystick.get_joystick_state();
            Button::button_state_t type_state = type_button.get_button_state();
            Button::button_state_t back_state = back_button.get_button_state();

            if (joy.pushed_down_edge) {
                index = (index + 1) % 2;
            } else if (joy.pushed_up_edge) {
                index = (index + 2 - 1) % 2;
            }

            if (type_state.pushed) {
                type_button.clear_button_state();
                return index == 0 ? MenuChoice::kMopping : MenuChoice::kMorse;
            }

            if (back_state.pushed || joy.left) {
                back_button.clear_button_state();
                return MenuChoice::kExit;
            }

            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    static bool run_mopping_game() {
        sprite.fillRect(0, 0, 128, 64, 0);
        sprite.setFont(&fonts::Font2);
        sprite.setTextColor(0xFFFF, 0x0000);
        sprite.drawCenterString("Launching...", 64, 28);
        sprite.pushSprite(&lcd, 0, 0);

        return wasm_runtime::run_game("/spiffs/games/mopping.wasm");
    }

    static bool run_morse_trainer(Joystick &joystick, Button &type_button,
                                  Button &back_button, Button &enter_button) {
        reset_inputs(joystick, type_button, back_button, enter_button);

        Max98357A buzzer;
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

                sprite.pushSprite(&lcd, 0, 0);

                message_text += alphabet_text;
                alphabet_text = "";

                // チャタリング防止用に100msのsleep
                vTaskDelay(10 / portTICK_PERIOD_MS);

                printf("message_text:%s\n", message_text.c_str());
            }

            // break_flagが立ってたら終了
            if (break_flag) {
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

        buzzer.stop_tone();
        buzzer.deinit();
        reset_inputs(joystick, type_button, back_button, enter_button);
        return false;
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
        // Increase stack to avoid rare overflows during heavy UI & networking
        xTaskCreatePinnedToCore(&menu_task, "menu_task", 12288, NULL, 6, NULL,
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
        // reduce console traffic to avoid stressing VFS/stdio during UI init
        // ESP_LOGD(TAG, "Power Voltage:%d", power_state.power_voltage);

        if (power_state.power_voltage > 140) {
            power_state.power_voltage = 140;
        }
        float power_per = power_state.power_voltage / 1.4;
        int power_per_pix = (int)(0.12 * power_per);

        auto enter_light_sleep = [&](const char *reason, bool force) -> bool {
            if (!force && ble_uart_is_ready()) {
                ESP_LOGI(TAG, "Skip light sleep (%s): BLE link active", reason);
                return false;
            }

            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.pushSprite(&lcd, 0, 0);

            const gpio_num_t wake_pins[] = {
                type_button.gpio_num,
                enter_button.gpio_num,
                GPIO_NUM_3,
            };

            for (gpio_num_t pin : wake_pins) {
                if (pin < 0) {
                    continue;
                }
                esp_err_t err = gpio_wakeup_enable(pin, GPIO_INTR_HIGH_LEVEL);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to enable wakeup on GPIO%d: %s", pin,
                             esp_err_to_name(err));
                }
            }

            esp_err_t gpio_wake_err = esp_sleep_enable_gpio_wakeup();
            if (gpio_wake_err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to enable GPIO wakeup: %s",
                         esp_err_to_name(gpio_wake_err));
            }

            esp_light_sleep_start();

            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
            for (gpio_num_t pin : wake_pins) {
                gpio_wakeup_disable(pin);
            }

            type_button.clear_button_state();
            type_button.reset_timer();
            enter_button.clear_button_state();
            enter_button.reset_timer();
            joystick.reset_timer();
            st = esp_timer_get_time();
            lcd.init();
            lcd.setRotation(2);
            sprite.pushSprite(&lcd, 0, 0);
            return true;
        };

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
                wifi_ap_record_t ap = {};
                if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                    int rssi = ap.rssi;
                    // ESP_LOGD(TAG, "RSSI:%d", rssi);
                    radioLevel = 4 - (rssi / -20);
                } else {
                    radioLevel = 0;
                }
                if (radioLevel < 1) {
                    radioLevel = 1;
                }

                // バッテリー電圧を更新
                power_state = power.get_power_state();
                // ESP_LOGD(TAG, "Power Voltage:%d", power_state.power_voltage);

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
                // ESP_LOGD(TAG, "Button pushed! time=%lld type=%c",
                // type_button_state.pushing_sec, type_button_state.push_type);

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
                if (enter_light_sleep("idle timeout", false)) {
                    continue;
                }
            } else if (enter_button_state.pushed) {
                if (enter_light_sleep("enter button", true)) {
                    continue;
                }
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
                // Safe append only when indices are valid
                int char_set_length = sizeof(char_set) / sizeof(char_set[0]);
                if (select_y_index < 0) select_y_index = 0;
                if (select_y_index >= char_set_length)
                    select_y_index = char_set_length - 1;
                int row_len = strlen(char_set[select_y_index]);
                if (row_len > 0) {
                    if (select_x_index < 0) select_x_index = 0;
                    if (select_x_index >= row_len) select_x_index = row_len - 1;
                    type_text.push_back(
                        char_set[select_y_index][select_x_index]);
                }
                type_button.clear_button_state();
                type_button.reset_timer();
            }

            // 文字種のスクロールの設定
            int char_set_length = sizeof(char_set) / sizeof(char_set[0]);
            if (select_y_index >= char_set_length) select_y_index = 0;
            if (select_y_index < 0) select_y_index = char_set_length - 1;

            // 文字選択のスクロールの設定（行末/行頭で循環）
            int row_len = strlen(char_set[select_y_index]);
            if (row_len <= 0) row_len = 1;
            if (select_x_index >= row_len) select_x_index = 0;
            if (select_x_index < 0) select_x_index = row_len - 1;

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
            // Feed watchdog / yield to scheduler
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        return type_text;
    }

    static std::string set_profile_info(uint8_t *ssid = 0) {
        std::string user_name = "";

        user_name = input_info();
        return user_name;
    }

    static void morse_greeting(const char *greet_txt,
                               const char *last_greet_txt = "", int cx = 64,
                               int cy = 32) {
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

        // Ask username, then attempt register/login; only persist on success
        while (true) {
            std::string user_name = set_profile_info();

            // If password not set, use default for development
            std::string password = get_nvs("password");
            if (password == "") {
                password = "password123";
                save_nvs((char *)"password", password);
            }

            auto& api = chatapi::shared_client(true);
            api.set_scheme("https");
            esp_err_t err = api.register_user(user_name, password);
            if (err != ESP_OK) {
                err = api.login(user_name, password);
            }
            if (err == ESP_OK) {
                // ChatApiClient already persisted user_name/jwt/user_id
                break;
            }

            // Failed: clear any stale user_name and prompt again
            save_nvs("user_name", "");
            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.setFont(&fonts::Font2);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            sprite.drawCenterString("Register/Login Failed", 64, 18);
            sprite.drawCenterString("Check creds & network", 64, 34);
            sprite.pushSprite(&lcd, 0, 0);
            vTaskDelay(1200 / portTICK_PERIOD_MS);
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
