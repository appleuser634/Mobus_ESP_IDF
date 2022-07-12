#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <button.h>
#include <string.h>
#include <map>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_SSD1306 _panel_instance;
    lgfx::Bus_I2C _bus_instance;

   public:
    LGFX(void) {
        {  // バス制御の設定を行います。
            auto cfg = _bus_instance.config();
            cfg.i2c_port = 0;  // 使用するI2Cポートを選択 (0 or 1)
            cfg.freq_write = 400000;  // 送信時のクロック
            cfg.freq_read = 400000;   // 受信時のクロック
            cfg.pin_sda = 21;         // SDAを接続しているピン番号
            cfg.pin_scl = 22;         // SCLを接続しているピン番号
            cfg.i2c_addr = 0x3C;      // I2Cデバイスのアドレス

            _bus_instance.config(cfg);  // 設定値をバスに反映します。
            _panel_instance.setBus(
                &_bus_instance);  // バスをパネルにセットします。
        }
        {  // 表示パネル制御の設定を行います。
            auto cfg = _panel_instance
                           .config();  // 表示パネル設定用の構造体を取得します。

            cfg.pin_cs = -1;  // CSが接続されているピン番号   (-1 = disable)
            cfg.pin_rst = -1;  // RSTが接続されているピン番号  (-1 = disable)
            cfg.pin_busy = -1;  // BUSYが接続されているピン番号 (-1 = disable)

            // ※
            // 以下の設定値はパネル毎に一般的な初期値が設定されていますので、不明な項目はコメントアウトして試してみてください。

            cfg.memory_width = 128;  // ドライバICがサポートしている最大の幅
            cfg.memory_height = 64;  // ドライバICがサポートしている最大の高さ
            cfg.panel_width = 128;  // 実際に表示可能な幅
            cfg.panel_height = 64;  // 実際に表示可能な高さ
            cfg.offset_x = 0;       // パネルのX方向オフセット量
            cfg.offset_y = 0;       // パネルのY方向オフセット量
            cfg.offset_rotation =
                0;  // 回転方向の値のオフセット 0~7 (4~7は上下反転)
            cfg.dummy_read_pixel =
                8;  // ピクセル読出し前のダミーリードのビット数
            cfg.dummy_read_bits =
                1;  // ピクセル以外のデータ読出し前のダミーリードのビット数
            cfg.readable = true;  // データ読出しが可能な場合 trueに設定
            cfg.invert = false;  // パネルの明暗が反転してしまう場合 trueに設定
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
static LGFX_Sprite sprite(&lcd);  // スプライトを使う場合はLGFX_Spriteのインスタンスを作成。

static constexpr char text[] = "MoBus!!";
static constexpr size_t textlen = sizeof(text) / sizeof(text[0]);
size_t textpos = 0;

class MenuDisplay {
    
    #define NAME_LENGTH_MAX 8

    public:   
    typedef struct {
        char menu_name[NAME_LENGTH_MAX];
        int display_position;
    } menu_t;

    char menu_names[3][NAME_LENGTH_MAX] = {"Talk","Setting","Game"};
    menu_t menu_list[3];

    void set_menu_position() {
        int menu_names_length = sizeof(menu_names) / NAME_LENGTH_MAX;
        printf("menu_names_length:%d",menu_names_length);
    }

    int cursor_point = 2;
    
    void Menu() { 
        lcd.init();
        lcd.setRotation(0);
        
        Joystick joystick;
        joystick.setup();

        Button button;
        button.setup();

        //set_menu_position();

        // 画面が横長になるように回転
        // if (lcd.width() < lcd.height()) lcd.setRotation(lcd.getRotation() ^ 2);

        lcd.setRotation(2);

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font4);
        sprite.setTextWrap(false);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height());
       
        int cursor_position = 0; 
        
        for (int i = 0; i <= 1000; i++) {
        
            for (int i = 0; i <= 2 ; i++) {
                cursor_position = i * 22;
                sprite.setCursor(26,cursor_position);  // カーソル位置を更新 
                sprite.print(menu_names[i]);  // 1バイトずつ出力
            }
            
            sprite.setCursor(2, cursor_point);  // カーソル位置を更新 
            sprite.print("->");  // 1バイトずつ出力 

            Joystick::joystick_state_t joystick_state = joystick.get_joystick_state();
            //printf("C6_Voltage:%d\n",joystick_state.C6_voltage);
            //printf("C7_Voltage:%d\n",joystick_state.C7_voltage);
            //printf("UP:%s\n", joystick_state.up ? "true" : "false");
            //printf("DOWN:%s\n", joystick_state.down ? "true" : "false");
            //printf("RIGHT:%s\n", joystick_state.right ? "true" : "false");
            //printf("LEFT:%s\n", joystick_state.left ? "true" : "false");
            vTaskDelay(50 / portTICK_PERIOD_MS);

            Button::button_state_t button_state = button.get_button_state();
            if (button_state.pushed == true) {
                printf("Button pushed!\n");
                printf("Pushing time:%lld\n",button_state.pushing_sec);
                printf("Push type:%c\n",button_state.push_type);

                button.clear_button_state();
            }

            if (joystick_state.up == true){
                cursor_point -= 2;
            }
            else if (joystick_state.down == true){
                cursor_point += 2;
            }
            
            sprite.pushSprite(&lcd, 0, 0);
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    };
};

class TalkDisplay {
    
    public:   
    
    int cursor_point = 2;

    char message_text[256] = "";
    char long_push_text[2] = "_";
    char short_push_text[2] = ".";

    
    /*
    std::map<std::string, int> mp {
        {"apple",1},
        {"banana",2},
    };
    
    int apple = mp.at("apple");
 
    a = mp["apple"];
    
    mp["apple"] = int 6;
    mp["banana"] = int 4;

    */


    std::map<std::string, std::string> morse_code {
        {"._","A"},
        {"_...","B"},
    };
    
    // morse_code.insert( map<string, string>::value_type("._", "A");
    
    /*
   morse_code["._"] = "A";
   morse_code["_..."] = 1;
   morse_code["_._."] = 2;
   morse_code["_.."] = 3;
   morse_code["."] = 4;
   morse_code[".._."] = 5;
   morse_code["__."] = 6;
   morse_code["...."] = 7;
   morse_code[".."] = 8;
   morse_code[".___"] = 9;
   morse_code["_._"] = 10;
   morse_code["._.."] = 11;
   morse_code["__"] = 12;
   morse_code["_."] = 13;
   morse_code["___"] = 14;
   morse_code[".__."] = 15;
   morse_code["__._"] = 16;
   morse_code["._."] = 17;
   morse_code["..."] = 18;
   morse_code["_"] = 19;
   morse_code[".._"] = 20;
   morse_code["..._"] = 21;
   morse_code[".__"] = 22;
   morse_code["_.._"] = 23;
   morse_code["_.__"] = 24;
   morse_code["__.."] = 25;

   string alphabet[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
   */

    int release_time = 0;
    
    void Talk() { 
        lcd.init();
        lcd.setRotation(0);
        
        Joystick joystick;
        joystick.setup();

        Button button;
        button.setup();

        lcd.setRotation(2);

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font4);
        sprite.setTextWrap(false);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height()); 

        int cursor_position = 0; 

        std::string morse = morse_code.at("._");
        printf("morse ._ is %s\n",morse.c_str());
 
        for (int i = 0; i <= 1000; i++) {
        
            //for (int i = 0; i <= 2 ; i++) {
            //    cursor_position = i * 22;
            //    sprite.setCursor(26,cursor_position);  // カーソル位置を更新 
            //    sprite.print(menu_names[i]);  // 1バイトずつ出力
            //}
            //
            //sprite.setCursor(2, cursor_point);  // カーソル位置を更新 
            //sprite.print("->");  // 1バイトずつ出力 
            
            sprite.setCursor(0,cursor_position);  // カーソル位置を更新 
            sprite.print(message_text);  // 1バイトずつ出力

            //Joystick::joystick_state_t joystick_state = joystick.get_joystick_state();
            //printf("C6_Voltage:%d\n",joystick_state.C6_voltage);
            //printf("C7_Voltage:%d\n",joystick_state.C7_voltage);
            //printf("UP:%s\n", joystick_state.up ? "true" : "false");
            //printf("DOWN:%s\n", joystick_state.down ? "true" : "false");
            //printf("RIGHT:%s\n", joystick_state.right ? "true" : "false");
            //printf("LEFT:%s\n", joystick_state.left ? "true" : "false");
            vTaskDelay(50 / portTICK_PERIOD_MS);

            Button::button_state_t button_state = button.get_button_state();
            if (button_state.pushed == true) {
                printf("Button pushed!\n");
                printf("Pushing time:%lld\n",button_state.pushing_sec);
                printf("Push type:%c\n",button_state.push_type);
                if (button_state.push_type == 's'){
                    strcat(message_text, short_push_text);
                }
                else if (button_state.push_type == 'l'){
                    strcat(message_text, long_push_text);
                }

                button.clear_button_state();
            }

            printf("Release time:%lld\n",button_state.release_sec);
            if (button_state.release_sec > 8){
                printf("Release time:%lld\n",button_state.release_sec);
            }
            
            sprite.pushSprite(&lcd, 0, 0);
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    };
};

class Oled {
    
    public:

    void BootDisplay() {        
        printf("Booting!!!\n");
        
        lcd.init();
        lcd.setRotation(0);

        // 画面が横長になるように回転
        // if (lcd.width() < lcd.height()) lcd.setRotation(lcd.getRotation() ^ 2);

        lcd.setRotation(2);

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::FreeSansOblique12pt7b);
        sprite.setTextWrap(false);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height());  // 画面幅+１文字分の横幅を用意

        for (int i = 50; i >= 20; i--) {
            sprite.setCursor(25,i);  // カーソル位置を更新 
            sprite.print(text);  // 1バイトずつ出力
            // sprite.scroll(0, 0);  // キャンバスの内容を1ドット上にスクロール
            sprite.pushSprite(&lcd, 0, 0);
        }
        
        vTaskDelay(1500 / portTICK_PERIOD_MS);
        
        for (int i = 20; i >= -50; i--) {
            sprite.setCursor(25, i);  // カーソル位置を更新 
            sprite.print(text);  // 1バイトずつ出力
            // sprite.scroll(0, 0);  // キャンバスの内容を1ドット上にスクロール
            sprite.pushSprite(&lcd, 0, 0);
        }
    }
};

