#include <cstdio>
#include <iterator>
#include <string>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <button.h>
#include <buzzer.hpp>
#include <images.hpp>
#include <led.hpp>
#include <ctype.h>

#pragma once

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_SH110x _panel_instance;
    lgfx::Bus_I2C _bus_instance;

   public:
    LGFX(void) {
        {  // バス制御の設定を行います。
            auto cfg = _bus_instance.config();
            cfg.i2c_port = 0;         // 使用するI2Cポートを選択 (0 or 1)
            cfg.freq_write = 400000;  // 送信時のクロック
            cfg.freq_read = 400000;   // 受信時のクロック
            cfg.pin_sda = 1;          // SDAを接続しているピン番号
            cfg.pin_scl = 2;          // SCLを接続しているピン番号
            cfg.i2c_addr = 0x3C;      // I2Cデバイスのアドレス

            _bus_instance.config(cfg);  // 設定値をバスに反映します。
            _panel_instance.setBus(
                &_bus_instance);  // バスをパネルにセットします。
        }
        {  // 表示パネル制御の設定を行います。
            auto cfg = _panel_instance
                           .config();  // 表示パネル設定用の構造体を取得します。

            cfg.pin_cs = -1;    // CSが接続されているピン番号   (-1 = disable)
            cfg.pin_rst = -1;   // RSTが接続されているピン番号  (-1 = disable)
            cfg.pin_busy = -1;  // BUSYが接続されているピン番号 (-1 = disable)

            // ※
            // 以下の設定値はパネル毎に一般的な初期値が設定されていますので、不明な項目はコメントアウトして試してみてください。

            cfg.memory_width = 128;  // ドライバICがサポートしている最大の幅
            cfg.memory_height = 64;  // ドライバICがサポートしている最大の高さ
            cfg.panel_width = 128;   // 実際に表示可能な幅
            cfg.panel_height = 64;   // 実際に表示可能な高さ
            cfg.offset_x = 2;        // パネルのX方向オフセット量
            cfg.offset_y = 0;        // パネルのY方向オフセット量
            cfg.offset_rotation =
                0;  // 回転方向の値のオフセット 0~7 (4~7は上下反転)
            cfg.dummy_read_pixel =
                8;  // ピクセル読出し前のダミーリードのビット数
            cfg.dummy_read_bits =
                1;  // ピクセル以外のデータ読出し前のダミーリードのビット数
            cfg.readable = true;  // データ読出しが可能な場合 trueに設定
            cfg.invert = false;   // パネルの明暗が反転してしまう場合 trueに設定
            cfg.rgb_order =
                false;  // パネルの赤と青が入れ替わってしまう場合 trueに設定
            cfg.dlen_16bit =
                false;  // データ長を16bit単位で送信するパネルの場合 trueに設定
            cfg.bus_shared =
                true;  // SDカードとバスを共有している場合
                       // trueに設定(drawJpgFile等でバス制御を行います)

            _panel_instance.config(cfg);
        }

        setPanel(&_panel_instance);  // 使用するパネルをセットします。
    }
};

static LGFX lcd;
static LGFX_Sprite sprite(
    &lcd);  // スプライトを使う場合はLGFX_Spriteのインスタンスを作成。

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

    static int release_time;

    static void SendAnimation() {
        sprite.fillRect(0, 0, 128, 64, 0);

        sprite.setCursor(30, 20);
        sprite.setFont(&fonts::Font4);
        sprite.print("Send!");
        sprite.setFont(&fonts::Font2);
        sprite.pushSprite(&lcd, 0, 0);

        vTaskDelay(2000 / portTICK_PERIOD_MS);
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

        Buzzer buzzer;
        Led led;

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
                buzzer.buzzer_on();
                led.led_on();
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
                buzzer.buzzer_off();
                led.led_off();
            }

            // printf("Release time:%lld\n",button_state.release_sec);
            if (type_button_state.release_sec > 200000) {
                // printf("Release time:%lld\n",button_state.release_sec);

                if (morse_code.count(morse_text)) {
                    alphabet_text = morse_code.at(morse_text);
                }
                morse_text = "";
            }
            if (back_button_state.pushing and type_button_state.pushed) {
                if (message_text != "") {
                    message_text.pop_back();
                }
                back_button.pushed_same_time();
                type_button.clear_button_state();
            } else if (back_button_state.pushed and
                       !back_button_state.pushed_same_time and
                       !type_button_state.pushing) {
                break;
            } else if (joystick_state.left) {
                break;
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
                message_text = "";

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
            sprite.print(display_text.c_str());
            sprite.pushSprite(&lcd, 0, 0);

            message_text += alphabet_text;
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
// std::map<std::string, std::string> TalkDisplay::morse_code = morse_code;
int TalkDisplay::release_time = 0;
bool TalkDisplay::running_flag = false;

class MessageBox {
   public:
    static bool running_flag;

    void start_box_task(std::string chat_to) {
        printf("Start Box Task...");
        // xTaskCreate(&menu_task, "menu_task", 4096, NULL, 6, NULL, 1);
        xTaskCreatePinnedToCore(&box_task, "box_task", 8012, &chat_to, 6, NULL,
                                1);
    }

    static void box_task(void *pvParameters) {
        nvs_main();
        lcd.init();
        lcd.setRotation(0);

        TalkDisplay talk;
        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        lcd.setRotation(2);

        sprite.fillRect(0, 0, 128, 64, 0);

        sprite.setFont(&fonts::Font4);
        sprite.setCursor(10, 20);
        sprite.print("Loading...");
        sprite.pushSprite(&lcd, 0, 0);

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font2);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height() * 2.5);

        HttpClient http_client;

        // メッセージの取得
        std::string chat_to = *(std::string *)pvParameters;
        JsonDocument res = http_client.get_message(chat_to);

        // 通知を非表示
        // http.notif_flag = false;

        int font_height = 16;
        int max_offset_y = -3;
        int min_offset_y =
            res["messages"].size() * (-1 * font_height) + (font_height * 2);
        int offset_y = min_offset_y;

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
                res = http_client.get_message(chat_to);
            }

            if (offset_y > max_offset_y) {
                offset_y = max_offset_y;
            } else if (offset_y < min_offset_y) {
                offset_y = min_offset_y;
            }

            // 描画処理
            int cursor_y = 0;
            for (int i = 0; i < res["messages"].size(); i++) {
                std::string message(res["messages"][i]["message"]);
                std::string message_from(res["messages"][i]["from"]);

                // cursor_y = offset_y + sprite.getCursorY() + 20;
                cursor_y = offset_y + (font_height * (i + 1));
                int next_cursor_y = offset_y + (font_height * (i + 2));

                if (message_from == chat_to) {
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    sprite.drawBitmap(0, cursor_y + 2, recv_icon2, 13, 12,
                                      TFT_BLACK, TFT_WHITE);
                } else {
                    sprite.setTextColor(0x000000u, 0xFFFFFFu);
                    sprite.fillRect(0, cursor_y, 128, font_height, 0xFFFF);
                    sprite.drawBitmap(0, cursor_y + 2, send_icon2, 13, 12,
                                      TFT_WHITE, TFT_BLACK);
                }

                sprite.setCursor(14, cursor_y);
                sprite.print(message.c_str());
                // sprite.drawFastHLine( 0, cursor_y, 128, 0xFFFF);
            }

            sprite.fillRect(0, 0, 128, 14, 0);
            sprite.setCursor(0, 0);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            sprite.print(chat_to.c_str());
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

class ContactBook {
   public:
    static bool running_flag;

    void start_message_menue_task() {
        printf("Start ContactBook Task...");
        xTaskCreatePinnedToCore(&message_menue_task, "message_menue_task", 8096,
                                NULL, 6, NULL, 1);
    }

    static void message_menue_task(void *pvParameters) {
        lcd.init();
        lcd.setRotation(0);

        Buzzer buzzer;
        Led led;

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        lcd.setRotation(2);

        int MAX_CONTACTS = 20;
        int CONTACT_PER_PAGE = 4;

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font4);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(),
                            lcd.height() * (MAX_CONTACTS / CONTACT_PER_PAGE));

        typedef struct {
            std::string name;
            int user_id;
        } contact_t;

        contact_t contacts[6] = {{"Kiki", 1},   {"Chibi", 2}, {"Musashi", 3},
                                 {"Shelly", 4}, {"Saku", 5},  {"Buncha", 6}};

        int select_index = 0;
        int font_height = 13;
        int margin = 3;
        int noti_circle_margin = 13;

        HttpClient http_client;
        // 通知の取得
        JsonDocument notif_res = http_client.get_notifications();

        MessageBox box;
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

            int length = sizeof(contacts) / sizeof(contact_t) - 1;
            for (int i = 0; i <= length; i++) {
                sprite.setCursor(10, (font_height + margin) * i);

                if (i == select_index) {
                    sprite.setTextColor(0x000000u, 0xFFFFFFu);
                    sprite.fillRect(0, (font_height + margin) * select_index,
                                    128, font_height + 3, 0xFFFF);
                } else {
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                }

                // 通知の表示
                for (int j = 0; j < notif_res["notifications"].size(); j++) {
                    std::string notification_flag(
                        notif_res["notifications"][j]["notification_flag"]);
                    std::string notification_from(
                        notif_res["notifications"][j]["from"]);
                    if (notification_flag == "true" &&
                        notification_from == contacts[i].name) {
                        if (i == select_index) {
                            sprite.fillCircle(
                                120, font_height * i + noti_circle_margin, 4,
                                0);
                        } else {
                            sprite.fillCircle(
                                120, font_height * i + noti_circle_margin, 4,
                                0xFFFF);
                        }
                    }
                }

                sprite.print(contacts[i].name.c_str());
            }

            if (joystick_state.pushed_up_edge) {
                select_index -= 1;
            } else if (joystick_state.pushed_down_edge) {
                select_index += 1;
            }

            if (select_index < 0) {
                select_index = 0;
            } else if (select_index >= length) {
                select_index = length;
            }

            sprite.pushSprite(&lcd, 0,
                              (int)(select_index / CONTACT_PER_PAGE) * -64);

            // ジョイスティック左を押されたらメニューへ戻る
            // 戻るボタンを押されたらメニューへ戻る
            if (joystick_state.left || back_button_state.pushed) {
                break;
            }

            if (type_button_state.pushed) {
                // talk.running_flag = true;
                // talk.start_talk_task(contacts[i].name);
                box.running_flag = true;
                box.start_box_task(contacts[select_index].name);
                while (box.running_flag) {
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }

                // 通知の取得
                notif_res = http_client.get_notifications();
                notif_res = http_client.get_notifications();

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
        char char_ssid[33];
        sprintf(char_ssid, "%s", uint_ssid);
        std::string ssid(char_ssid);

        return ssid;
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

        int font_ = 13;
        int margin = 3;

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
            sprite.setCursor(0, 15);
            sprite.print(type_text.c_str());

            sprite.pushSprite(&lcd, 0, 0);
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

                ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);
                ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);
                ESP_LOGI(TAG, "Channel \t\t%d", ap_info[i].primary);

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

class SettingMenu {
   public:
    static bool running_flag;

    void start_message_menue_task() {
        printf("Start MessageMenue Task...");
        xTaskCreatePinnedToCore(&message_menue_task, "message_menue_task", 4096,
                                NULL, 6, NULL, 1);
    }

    static void message_menue_task(void *pvParameters) {
        lcd.init();
        lcd.setRotation(0);

        WiFiSetting wifi_setting;

        Buzzer buzzer;
        Led led;

        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        lcd.setRotation(2);

        int MAX_SETTINGS = 20;
        int ITEM_PER_PAGE = 4;

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font4);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(),
                            lcd.height() * (MAX_SETTINGS / ITEM_PER_PAGE));

        typedef struct {
            std::string setting_name;
        } setting_t;

        setting_t settings[4] = {{"Profile"}, {"Wi-Fi"}, {"Sound"}, {"Notif"}};

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

            int length = sizeof(settings) / sizeof(setting_t) - 1;
            for (int i = 0; i <= length; i++) {
                sprite.setCursor(10, (font_height + margin) * i);

                if (i == select_index) {
                    sprite.setTextColor(0x000000u, 0xFFFFFFu);
                    sprite.fillRect(0, (font_height + margin) * select_index,
                                    128, font_height + 3, 0xFFFF);
                } else {
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                }
                sprite.print(settings[i].setting_name.c_str());
            }

            if (joystick_state.pushed_up_edge) {
                select_index -= 1;
            } else if (joystick_state.pushed_down_edge) {
                select_index += 1;
            }

            if (select_index < 0) {
                select_index = 0;
            } else if (select_index >= length) {
                select_index = length;
            }

            sprite.pushSprite(&lcd, 0,
                              (int)(select_index / ITEM_PER_PAGE) * -64);

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

        Buzzer buzzer;
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
                    buzzer.buzzer_on();
                    led.led_on();
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
                    buzzer.buzzer_off();
                    led.led_off();
                }

                // printf("Release time:%lld\n",button_state.release_sec);
                if (type_button_state.release_sec > 200000) {
                    // printf("Release time:%lld\n",button_state.release_sec);

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
                sprite.setCursor(50, 36);
                sprite.print(display_text.c_str());

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

            // Play時間を取得
            p_time = round((esp_timer_get_time() - st) / 10000) / 100;
            char b_p_time[50];
            std::sprintf(b_p_time, "%.2f", p_time);
            std::string s_p_time(b_p_time);

            // Play時間を表示
            while (1) {
                // break_flagが立ってたら終了
                if (break_flag) {
                    break;
                }

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

                sprite.fillRect(0, 0, 128, 64, 0);

                sprite.setFont(&fonts::Font4);
                sprite.setCursor(32, 0);
                sprite.print("Clear!");

                sprite.setFont(&fonts::Font2);
                sprite.setCursor(28, 32);
                std::string t_text = "Time: " + s_p_time + "s";
                sprite.print(t_text.c_str());
                sprite.pushSprite(&lcd, 0, 0);
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

        lcd.setRotation(2);

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font4);
        sprite.setTextWrap(false);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height());

        // 開始時間を取得 st=start_time
        long long int st = esp_timer_get_time();
        // 電波強度の初期値
        float radioLevel = 4;

        // 通知の取得
        http_client.start_notifications();
        JsonDocument notif_res = http_client.get_notifications();

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

                // 通知情報を更新
                notif_res = http_client.get_notifications();
                st = esp_timer_get_time();
            }

            // メッセージ受信通知の表示
            // if (http.notif_flag) {
            //   sprite.drawRoundRect(50, 0, 20, 8, 2, 0xFFFF);
            // }

            PowerMonitor::power_state_t power_state = power.get_power_state();
            // printf("Power Voltage:%d\n", power_state.power_voltage);

            float power_per = power_state.power_voltage / 25.5;
            int power_per_pix = (int)(0.14 * power_per);

            // printf("Power Per:%d\n", power_per_pix);

            // 電池残量表示
            sprite.drawRoundRect(110, 0, 14, 8, 2, 0xFFFF);
            sprite.fillRect(111, 0, power_per_pix, 8, 0xFFFF);

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
            // printf("RIGHT:%s\n", joystick_state.right ? "true" : "false");
            // printf("LEFT:%s\n", joystick_state.left ? "true" : "false");

            Button::button_state_t type_button_state =
                type_button.get_button_state();
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

            sprite.setCursor(0, 0);
            sprite.print("Name:");
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
            sprite.setCursor(0, 15);
            sprite.print(type_text.c_str());

            sprite.pushSprite(&lcd, 0, 0);
        }

        return type_text;
    }

    static void set_profile_info(uint8_t *ssid = 0) {
        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        int select_index = 0;
        int font_height = 13;
        int margin = 3;

        std::string user_name = "";

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

            user_name = input_info();

            // チャタリング防止用に100msのsleep
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    static void opening() {
        Buzzer buzzer;

        lcd.init();
        lcd.setRotation(2);

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font2);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height());

        sprite.fillRect(0, 0, 128, 64, 0);
        sprite.setCursor(30, 20);

        char greet_txt[] = "Hi, DE Mimoc.";
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
                char m = morse_txt[j];

                if (m == '.') {
                    buzzer.buzzer_on();
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                    buzzer.buzzer_off();

                } else if (m == '_') {
                    buzzer.buzzer_on();
                    vTaskDelay(150 / portTICK_PERIOD_MS);
                    buzzer.buzzer_off();
                }
                morse_str += m;
                sprite.drawCenterString((show_greet_str + morse_str).c_str(),
                                        64, 32);
                sprite.pushSprite(&lcd, 0, 0);
            }
            morse_str = "";

            sprite.fillRect(0, 0, 128, 64, 0);
            show_greet_str = greet_str.substr(0, i + 1).c_str();
            sprite.drawCenterString(show_greet_str.c_str(), 64, 32);
            sprite.pushSprite(&lcd, 0, 0);

            vTaskDelay(50 / portTICK_PERIOD_MS);
        }

        sprite.pushSprite(&lcd, 0, 0);
    }

    static void profile_setting_task() {
        opening();
        Joystick joystick;

        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

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

            // // 入力イベント
            // if (joystick_state.left or back_button_state.pushed) {
            //     break;
            // } else if (joystick_state.pushed_up_edge) {
            //     select_index -= 1;
            // } else if (joystick_state.pushed_down_edge) {
            //     select_index += 1;
            // }

            // if (select_index < 0) {
            //     select_index = 0;
            // } else if (select_index > ssid_n) {
            //     select_index = ssid_n;
            // }

            // if (type_button_state.pushed) {
            //     sprite.setColorDepth(8);
            //     sprite.setFont(&fonts::Font2);
            //     type_button.clear_button_state();
            //     type_button.reset_timer();
            //     joystick.reset_timer();
            // }

            // for (int i = 0; i <= ssid_n; i++) {
            //     sprite.setCursor(10, (font_height + margin) * i);

            //     ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);
            //     ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);
            //     ESP_LOGI(TAG, "Channel \t\t%d", ap_info[i].primary);

            //     if (i == select_index) {
            //         sprite.setTextColor(0x000000u, 0xFFFFFFu);
            //         sprite.fillRect(0, (font_height + margin) * select_index,
            //                         128, font_height + 3, 0xFFFF);
            //     } else {
            //         sprite.setTextColor(0xFFFFFFu, 0x000000u);
            //     }

            //     if (ssid_n == i) {
            //         // 手動入力のためのOtherを表示
            //         std::string disp_ssid = "Other";
            //         sprite.print(disp_ssid.c_str());
            //     } else {
            //         // スキャンの結果取得できたSSIDを表示
            //         sprite.print(get_omitted_ssid(ap_info[i].ssid).c_str());
            //     }
            // }

            // sprite.pushSprite(&lcd, 0, 0);

            // 個別のWiFi設定画面へ遷移
            if (type_button_state.pushed) {
                // if (ssid_n == select_index) {
                //     set_wifi_info();
                // } else {
                //     set_wifi_info(ap_info[select_index].ssid);
                // }
                set_profile_info();
                type_button.clear_button_state();
                type_button.reset_timer();
                back_button.clear_button_state();
                back_button.reset_timer();
                joystick.reset_timer();
            }

            // チャタリング防止用に100msのsleep
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        vTaskDelete(NULL);
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
    }

    void RecvNotif() {
        printf("RecvNotif!!!\n");

        sprite.fillRect(0, 0, 128, 64, 0);
        static constexpr char notif_text[] = "Recv!!";

        for (int i = 50; i >= 20; i--) {
            sprite.setCursor(25, i);   // カーソル位置を更新
            sprite.print(notif_text);  // 1バイトずつ出力
            // sprite.scroll(0, 0);  // キャンバスの内容を1ドット上にスクロール
            sprite.pushSprite(&lcd, 0, 0);
        }

        for (int i = 20; i >= -50; i--) {
            sprite.setCursor(25, i);   // カーソル位置を更新
            sprite.print(notif_text);  // 1バイトずつ出力
            // sprite.scroll(0, 0);  // キャンバスの内容を1ドット上にスクロール
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
        // sprite.drawBitmap(32, 0, mimocLogo, 64, 64, TFT_WHITE, TFT_BLACK);

        sprite.pushSprite(&lcd, 0, 0);
    }
};
