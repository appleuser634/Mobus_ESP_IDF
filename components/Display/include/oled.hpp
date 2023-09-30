#include <iterator>
#include <string>

#define LGFX_USE_V1
#include <button.h>

#include <LovyanGFX.hpp>
#include <buzzer.hpp>
#include <images.hpp>
#include <led.hpp>

#pragma once

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_SH110x _panel_instance;
  lgfx::Bus_I2C _bus_instance;

public:
  LGFX(void) {
    { // バス制御の設定を行います。
      auto cfg = _bus_instance.config();
      cfg.i2c_port = 0;        // 使用するI2Cポートを選択 (0 or 1)
      cfg.freq_write = 400000; // 送信時のクロック
      cfg.freq_read = 400000;  // 受信時のクロック
      cfg.pin_sda = 21;        // SDAを接続しているピン番号
      cfg.pin_scl = 22;        // SCLを接続しているピン番号
      cfg.i2c_addr = 0x3C;     // I2Cデバイスのアドレス

      _bus_instance.config(cfg); // 設定値をバスに反映します。
      _panel_instance.setBus(&_bus_instance); // バスをパネルにセットします。
    }
    { // 表示パネル制御の設定を行います。
      auto cfg =
          _panel_instance.config(); // 表示パネル設定用の構造体を取得します。

      cfg.pin_cs = -1; // CSが接続されているピン番号   (-1 = disable)
      cfg.pin_rst = -1; // RSTが接続されているピン番号  (-1 = disable)
      cfg.pin_busy = -1; // BUSYが接続されているピン番号 (-1 = disable)

      // ※
      // 以下の設定値はパネル毎に一般的な初期値が設定されていますので、不明な項目はコメントアウトして試してみてください。

      cfg.memory_width = 128; // ドライバICがサポートしている最大の幅
      cfg.memory_height = 64; // ドライバICがサポートしている最大の高さ
      cfg.panel_width = 128; // 実際に表示可能な幅
      cfg.panel_height = 64; // 実際に表示可能な高さ
      cfg.offset_x = 2;      // パネルのX方向オフセット量
      cfg.offset_y = 0;      // パネルのY方向オフセット量
      cfg.offset_rotation = 0; // 回転方向の値のオフセット 0~7 (4~7は上下反転)
      cfg.dummy_read_pixel = 8; // ピクセル読出し前のダミーリードのビット数
      cfg.dummy_read_bits =
          1; // ピクセル以外のデータ読出し前のダミーリードのビット数
      cfg.readable = true; // データ読出しが可能な場合 trueに設定
      cfg.invert = false; // パネルの明暗が反転してしまう場合 trueに設定
      cfg.rgb_order =
          false; // パネルの赤と青が入れ替わってしまう場合 trueに設定
      cfg.dlen_16bit =
          false; // データ長を16bit単位で送信するパネルの場合 trueに設定
      cfg.bus_shared = true; // SDカードとバスを共有している場合
                             // trueに設定(drawJpgFile等でバス制御を行います)

      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance); // 使用するパネルをセットします。
  }
};

static LGFX lcd;
static LGFX_Sprite
    sprite(&lcd); // スプライトを使う場合はLGFX_Spriteのインスタンスを作成。

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
    sprite.print("Send!");
    sprite.pushSprite(&lcd, 0, 0);

    vTaskDelay(2000 / portTICK_PERIOD_MS);
  };

  static bool running_flag;

  void start_talk_task() {
    printf("Start Talk Task...");
    // xTaskCreate(&menu_task, "menu_task", 4096, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(&talk_task, "talk_task", 4096, NULL, 6, NULL, 1);
  }

  static void talk_task(void *pvParameters) {
    lcd.init();
    lcd.setRotation(0);

    Buzzer buzzer;
    Led led;

    Joystick joystick;
    joystick.setup();

    Button type_button(GPIO_NUM_4);
    Button back_button(GPIO_NUM_25);
    Button enter_button(GPIO_NUM_26);

    HttpClient http;

    lcd.setRotation(2);

    sprite.setColorDepth(8);
    sprite.setFont(&fonts::Font4);
    // sprite.setFont(&fonts::Font2);
    // sprite.setFont(&fonts::FreeMono9pt7b);
    sprite.setTextWrap(true); // 右端到達時のカーソル折り返しを禁止
    sprite.createSprite(lcd.width(), lcd.height());

    // カーソル点滅制御用タイマー
    long long int t = esp_timer_get_time();

    while (true) {
      Joystick::joystick_state_t joystick_state = joystick.get_joystick_state();
      // printf("UP:%s\n", joystick_state.up ? "true" : "false");
      // printf("DOWN:%s\n", joystick_state.down ? "true" : "false");
      // printf("RIGHT:%s\n", joystick_state.right ? "true" : "false");
      // printf("LEFT:%s\n", joystick_state.left ? "true" : "false");

      // モールス信号打ち込みキーの判定ロジック
      Button::button_state_t type_button_state = type_button.get_button_state();

      Button::button_state_t back_button_state = back_button.get_button_state();
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
        // FIXME
        break;
      } else if (joystick_state.up and enter_button_state.pushed) {
        esp_restart();
      } else if (back_button_state.pushed) {
        back_button.clear_button_state();
      }

      // Enter(送信)キーの判定ロジック
      if (enter_button_state.pushed and message_text != "") {
        printf("Button pushed!\n");
        printf("Pushing time:%lld\n", enter_button_state.pushing_sec);
        printf("Push type:%c\n", enter_button_state.push_type);

        http.post_message(message_text);
        message_text = "";

        SendAnimation();

        enter_button.clear_button_state();
      }

      std::string display_text = message_text + morse_text + alphabet_text;

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

class BoxDisplay {
public:
  static bool running_flag;
  static HttpClient http;

  void start_box_task() {
    printf("Start Box Task...");
    // xTaskCreate(&menu_task, "menu_task", 4096, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(&box_task, "box_task", 4096, NULL, 6, NULL, 1);
  }

  static void box_task(void *pvParameters) {
    lcd.init();
    lcd.setRotation(0);

    Joystick joystick;
    joystick.setup();

    Button type_button(GPIO_NUM_4);
    Button back_button(GPIO_NUM_25);
    Button enter_button(GPIO_NUM_26);

    lcd.setRotation(2);

    sprite.setColorDepth(8);
    sprite.setFont(&fonts::Font2);
    sprite.setTextWrap(true); // 右端到達時のカーソル折り返しを禁止
    sprite.createSprite(lcd.width(), lcd.height() * 2.5);

    sprite.setCursor(0, 0);
    cJSON *message = NULL;
    printf("Messages Length:%d", cJSON_GetArraySize(HttpClient::messages));

    int messages_n = cJSON_GetArraySize(HttpClient::messages);
    if (messages_n == 0) {
      sprite.print("No Message...");
    } else {
      cJSON_ArrayForEach(message, HttpClient::messages) {
        int message_id = cJSON_GetObjectItem(message, "ID")->valuedouble;
        printf("MESSAGE_ID:%d\n", message_id);

        std::string message_from =
            cJSON_GetObjectItem(message, "MessageFrom")->valuestring;
        printf("MESSAGE_FROM:%s\n", message_from.c_str());

        std::string message_to =
            cJSON_GetObjectItem(message, "MessageTo")->valuestring;
        printf("MESSAGE_TO:%s\n", message_to.c_str());

        std::string message_s =
            cJSON_GetObjectItem(message, "Message")->valuestring;
        printf("MESSAGE:%s\n", message_s.c_str());

        sprite.print((message_s + "\n").c_str());
      }
    }

    sprite.pushSprite(&lcd, 0, 0);

    // 通知を非表示
    http.notif_flag = false;

    int show_y = 0;
    while (true) {
      Joystick::joystick_state_t joystick_state = joystick.get_joystick_state();
      Button::button_state_t back_button_state = back_button.get_button_state();

      if (joystick_state.left or back_button_state.pushed) {
        break;
      } else if (joystick_state.up and messages_n) {
        show_y += 8;
      } else if (joystick_state.down and messages_n) {
        show_y -= 8;
      }

      if (show_y < (lcd.height() * -1.5)) {
        show_y = (lcd.height() * -1.5);
      } else if (show_y > 0) {
        show_y = 0;
      }

      // チャタリング防止用に100msのsleep
      vTaskDelay(10 / portTICK_PERIOD_MS);
      sprite.pushSprite(&lcd, 0, show_y);
    }

    running_flag = false;
    vTaskDelete(NULL);
  };
};
bool BoxDisplay::running_flag = false;

class MessageMenue {
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

    Buzzer buzzer;
    Led led;

    Joystick joystick;
    joystick.setup();

    Button type_button(GPIO_NUM_4);
    Button back_button(GPIO_NUM_25);
    Button enter_button(GPIO_NUM_26);

    lcd.setRotation(2);

    sprite.setColorDepth(8);
    sprite.setFont(&fonts::Font4);
    sprite.setTextWrap(true); // 右端到達時のカーソル折り返しを禁止
    sprite.createSprite(lcd.width(), lcd.height());

    typedef struct {
      char name[255];
      int user_id;
    } contact_t;

    contact_t contacts[4] = {
        {"Musashi", 1}, {"Hazuki", 2}, {"Kiki", 3}, {"Chibi", 4}};

    int select_y = 0;
    int select_index = 0;

    while (1) {
      // Joystickの状態を取得
      Joystick::joystick_state_t joystick_state = joystick.get_joystick_state();

      // スイッチの状態を取得
      Button::button_state_t type_button_state = type_button.get_button_state();
      Button::button_state_t back_button_state = back_button.get_button_state();
      Button::button_state_t enter_button_state =
          enter_button.get_button_state();

      sprite.fillRect(0, 0, 128, 64, 0);

      sprite.setFont(&fonts::Font2);

      int contacts_length = sizeof(contacts) / sizeof(contact_t) - 1;
      int cur_y_offset = 13;

      // 選択している連絡先を矩形で囲む
      sprite.fillRect(0, select_y, 128, cur_y_offset, 0xFFFF);

      for (int i = contacts_length; i >= 0; i--) {
        sprite.setCursor(10, cur_y_offset * i);

        if (i == select_index) {
          sprite.setTextColor(0x000000u);
        } else {
          sprite.setTextColor(0xFFFFFFu);
        }
        sprite.print(contacts[i].name);
      }

      if (joystick_state.pushed_up_edge) {
        select_index -= 1;
      } else if (joystick_state.pushed_down_edge) {
        select_index += 1;
      }

      select_y = select_index * cur_y_offset;

      if (type_button_state.pushed) {
	// チャット画面
        while (1) {
          sprite.fillRect(0, 0, 128, 64, 0);

          sprite.setCursor(0, 0);
          sprite.setTextColor(0xFFFFFFu);

          // header
          sprite.print(contacts[select_index].name);
          sprite.drawFastHLine(0, 13, 128, 0xFFFF);

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

          if (back_button_state.pushed) {
            break;
          }

          sprite.pushSprite(&lcd, 0, 0);
          vTaskDelay(100 / portTICK_PERIOD_MS);
        }
      }

      // ジョイスティック左を押されたらメニューへ戻る
      // 戻るボタンを押されたらメニューへ戻る
      if (joystick_state.left || back_button_state.pushed) {
        break;
      }

      sprite.pushSprite(&lcd, 0, 0);
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    running_flag = false;
    vTaskDelete(NULL);
  };
};
bool MessageMenue::running_flag = false;

class Game {
public:
  static bool running_flag;

  void start_game_task() {
    printf("Start Game Task...");
    // xTaskCreate(&menu_task, "menu_task", 4096, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(&game_task, "game_task", 4096, NULL, 6, NULL, 1);
  }

  static std::map<std::string, std::string> morse_code;
  static void game_task(void *pvParameters) {
    lcd.init();
    lcd.setRotation(0);

    Buzzer buzzer;
    Led led;

    Joystick joystick;
    joystick.setup();

    Button type_button(GPIO_NUM_4);
    Button back_button(GPIO_NUM_25);
    Button enter_button(GPIO_NUM_26);

    lcd.setRotation(2);

    sprite.setColorDepth(8);
    sprite.setFont(&fonts::Font4);
    sprite.setTextWrap(true); // 右端到達時のカーソル折り返しを禁止
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

      // 問題を解き終わるまでループ
      while (c < n) {
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
          break_flag = true;
          break;
        } else if (joystick_state.left) {
          break_flag = true;
          break;
        } else if (joystick_state.up and enter_button_state.pushed) {
          esp_restart();
        } else if (back_button_state.pushed) {
          back_button.clear_button_state();
        }

        // Enter(送信)キーの判定ロジック
        if (enter_button_state.pushed and message_text != "") {
          printf("Button pushed!\n");
          printf("Pushing time:%lld\n", enter_button_state.pushing_sec);
          printf("Push type:%c\n", enter_button_state.push_type);

          message_text = "";

          enter_button.clear_button_state();
        }

        // 出題の文字と一緒であればcを++
        if (*message_text.c_str() == random_char) {
          c += 1;
          random_char = letters[rand() % 26];
        }

        message_text = "";

        std::string display_text = message_text + morse_text + alphabet_text;

        std::string strN = std::to_string(n);
        std::string strC = std::to_string(c);

        std::string nPerC = strC + "/" + strN;

        sprite.fillRect(0, 0, 128, 64, 0);

        sprite.setFont(&fonts::Font2);
        sprite.setCursor(88, 0);
        sprite.print(nPerC.c_str());

        // sprite.setFont(&fonts::Font4);
        sprite.setFont(&fonts::FreeMono12pt7b);
        sprite.setCursor(50, 36);
        sprite.print(display_text.c_str());

        sprite.setCursor(50, 5);
        sprite.print(random_char);

        sprite.drawFastHLine(0, 16, 43, 0xFFFF);
        sprite.drawFastHLine(75, 16, 128, 0xFFFF);
        sprite.drawRect(46, 2, 26, 26, 0xFFFF);

        sprite.pushSprite(&lcd, 0, 0);

        message_text += alphabet_text;
        alphabet_text = "";

        // チャタリング防止用に100msのsleep
        vTaskDelay(10 / portTICK_PERIOD_MS);

        printf("message_text:%s\n", message_text.c_str());
      }

      // Play時間を取得
      int p_time = (esp_timer_get_time() - st) / 1000000;

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

        std::string t_text = "Time: " + (std::to_string(p_time)) + "s";

        sprite.fillRect(0, 0, 128, 64, 0);

        sprite.setFont(&fonts::Font4);
        sprite.setCursor(32, 0);
        sprite.print("Clear!");

        sprite.setFont(&fonts::Font2);
        sprite.setCursor(37, 32);
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

class MenuDisplay {
#define NAME_LENGTH_MAX 8

public:
  void start_menu_task() {
    printf("Start Menu Task...");
    // xTaskCreate(&menu_task, "menu_task", 4096, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(&menu_task, "menu_task", 4096, NULL, 6, NULL, 1);
  }

  static HttpClient http;

  static void menu_task(void *pvParameters) {
    struct menu_t {
      char menu_name[NAME_LENGTH_MAX];
      int display_position_x;
      int display_position_y;
    };

    struct menu_t menu_list[3] = {
        {"Talk", 9, 22}, {"Box", 51, 22}, {"Game", 93, 22}};

    int cursor_index = 0;

    Joystick joystick;
    joystick.setup();

    PowerMonitor power;
    power.setup();

    // メニューから遷移する機能のインスタンス
    TalkDisplay talk;
    BoxDisplay box;
    Game game;
    MessageMenue messageMenue;

    Button type_button(GPIO_NUM_4);
    Button enter_button(GPIO_NUM_26);

    lcd.setRotation(2);

    sprite.setColorDepth(8);
    sprite.setFont(&fonts::Font4);
    sprite.setTextWrap(false); // 右端到達時のカーソル折り返しを禁止
    sprite.createSprite(lcd.width(), lcd.height());

    // 開始時間を取得 st=start_time
    long long int st = esp_timer_get_time();
    // 電波強度の初期値
    float radioLevel = 4;

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
      if (p_time > 10) {
        // 電波強度を更新
        wifi_ap_record_t ap;
        esp_wifi_sta_get_ap_info(&ap);
        printf("%d\n", ap.rssi);

        radioLevel = 4 - (ap.rssi / -20);
        if (radioLevel < 1) {
          radioLevel = 1;
        }
        st = esp_timer_get_time();
      }

      // メッセージ受信通知の表示
      if (http.notif_flag) {
        sprite.drawRoundRect(50, 0, 20, 8, 2, 0xFFFF);
      }

      PowerMonitor::power_state_t power_state = power.get_power_state();
      printf("Power Voltage:%d\n", power_state.power_voltage);

      float power_per = power_state.power_voltage / 25.5;
      int power_per_pix = (int)(0.14 * power_per);

      printf("Power Per:%d\n", power_per_pix);

      // 電池残量表示
      sprite.drawRoundRect(110, 0, 14, 8, 2, 0xFFFF);
      sprite.fillRect(111, 0, power_per_pix, 8, 0xFFFF);

      sprite.fillRect(124, 2, 1, 4, 0xFFFF);

      // Menu選択の表示
      sprite.fillRoundRect(menu_list[cursor_index].display_position_x - 2,
                           menu_list[cursor_index].display_position_y - 2, 36,
                           36, 5, 0xFFFF);

      // Menu項目を表示させる
      int menu_lists_n = sizeof(menu_list) / sizeof(menu_t);
      for (int i = 0; i <= menu_lists_n; i++) {
        const unsigned char *icon_image = mail_icon;

        if (i == 1) {
          icon_image = setting_icon;
        } else if (i == 2) {
          icon_image = game_icon;
        }
        sprite.drawBitmap(menu_list[i].display_position_x,
                          menu_list[i].display_position_y, icon_image, 32, 32,
                          TFT_WHITE, TFT_BLACK);
      }

      Joystick::joystick_state_t joystick_state = joystick.get_joystick_state();
      // printf("UP:%s\n", joystick_state.up ? "true" : "false");
      // printf("DOWN:%s\n", joystick_state.down ? "true" : "false");
      // printf("RIGHT:%s\n", joystick_state.right ? "true" : "false");
      // printf("LEFT:%s\n", joystick_state.left ? "true" : "false");
      vTaskDelay(50 / portTICK_PERIOD_MS);

      Button::button_state_t type_button_state = type_button.get_button_state();
      if (type_button_state.pushed) {
        printf("Button pushed!\n");
        printf("Pushing time:%lld\n", type_button_state.pushing_sec);
        printf("Push type:%c\n", type_button_state.push_type);

        if (cursor_index == 0) {
          talk.running_flag = true;
          talk.start_talk_task();
          // talkタスクの実行フラグがfalseになるまで待機
          while (talk.running_flag) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
          }
          sprite.setFont(&fonts::Font4);
        } else if (cursor_index == 1) {
          messageMenue.running_flag = true;
          messageMenue.start_message_menue_task();
          // talkタスクの実行フラグがfalseになるまで待機
          while (messageMenue.running_flag) {
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
          // talkタスクの実行フラグがfalseになるまで待機
          while (game.running_flag) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
          }
          sprite.setFont(&fonts::Font4);
        }

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

      sprite.pushSprite(&lcd, 0, 0);
      sprite.fillRect(0, 0, 128, 64, 0);

      esp_task_wdt_reset();

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
      sprite.setCursor(25, i);  // カーソル位置を更新
      sprite.print(notif_text); // 1バイトずつ出力
      // sprite.scroll(0, 0);  // キャンバスの内容を1ドット上にスクロール
      sprite.pushSprite(&lcd, 0, 0);
    }

    for (int i = 20; i >= -50; i--) {
      sprite.setCursor(25, i);  // カーソル位置を更新
      sprite.print(notif_text); // 1バイトずつ出力
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
