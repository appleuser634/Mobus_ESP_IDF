#include <iterator>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <button.h>
#include <buzzer.hpp>

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

class TalkDisplay {
    
    public:   
    
    int cursor_point = 2;

    std::string morse_text = "";
    std::string message_text = "";
    std::string alphabet_text = "";

    std::string long_push_text = "_";
    std::string short_push_text = ".";

    
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
        {"_._.","C"},
        {"_..","D"},
        {".","E"},
        {".._.","F"},
        {"__.","G"},
        {"....","H"},
        {"..","I"},
        {".___","J"},
        {"_._","K"},
        {"._..","L"},
        {"__","M"},
        {"_.","N"},
        {"___","O"},
        {".__.","P"},
        {"__._","Q"},
        {"._.","R"},
        {"...","S"},
        {"_","T"},
        {".._","U"},
        {"..._","V"},
        {".__","W"},
        {"_.._","X"},
        {"_.__","Y"},
        {"__..","Z"},
        {"....."," "},
    };


    int release_time = 0;

	void SendAnimation() {
		sprite.fillRect(0, 0, 128, 64, 0);
		
		sprite.setCursor(30,20);
		sprite.print("Send!");
		sprite.pushSprite(&lcd, 0, 0);
		
		vTaskDelay(2000 / portTICK_PERIOD_MS);
	};

    void Talk() { 
        lcd.init();
        lcd.setRotation(0);
        
		Buzzer buzzer;

        Joystick joystick;
        joystick.setup();

        Button type_button(GPIO_NUM_4);
        Button back_button(GPIO_NUM_25);
        Button enter_button(GPIO_NUM_26);
		
		HttpClient http;

        lcd.setRotation(2);

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font4);
        sprite.setTextWrap(true);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height()); 

        int cursor_position = 0; 

        while (true) {
        
            //for (int i = 0; i <= 2 ; i++) {
            //    cursor_position = i * 22;
            //    sprite.setCursor(26,cursor_position);  // カーソル位置を更新 
            //    sprite.print(menu_names[i]);  // 1バイトずつ出力
            //}
            //
            //sprite.setCursor(2, cursor_point);  // カーソル位置を更新 
            //sprite.print("->");  // 1バイトずつ出力 
            

            //Joystick::joystick_state_t joystick_state = joystick.get_joystick_state();
            //printf("C6_Voltage:%d\n",joystick_state.C6_voltage);
            //printf("C7_Voltage:%d\n",joystick_state.C7_voltage);
            //printf("UP:%s\n", joystick_state.up ? "true" : "false");
            //printf("DOWN:%s\n", joystick_state.down ? "true" : "false");
            //printf("RIGHT:%s\n", joystick_state.right ? "true" : "false");
            //printf("LEFT:%s\n", joystick_state.left ? "true" : "false");
			vTaskDelay(50 / portTICK_PERIOD_MS);

			// モールス信号打ち込みキーの判定ロジック
            Button::button_state_t type_button_state = type_button.get_button_state();
			Button::button_state_t back_button_state = back_button.get_button_state();

			if (type_button_state.push_edge and !back_button_state.pushing){
				buzzer.buzzer_on();
			}
            
			if (type_button_state.pushed and !back_button_state.pushing) {
                printf("Button pushed!\n");
                printf("Pushing time:%lld\n",type_button_state.pushing_sec);
                printf("Push type:%c\n",type_button_state.push_type);
                if (type_button_state.push_type == 's'){
                    morse_text += short_push_text;
                }
                else if (type_button_state.push_type == 'l'){
                    morse_text += long_push_text;
                }

                type_button.clear_button_state();
				buzzer.buzzer_off();
            }

            // printf("Release time:%lld\n",button_state.release_sec);
            if (type_button_state.release_sec > 8){
                // printf("Release time:%lld\n",button_state.release_sec);

                if (morse_code.count(morse_text)) {
                    alphabet_text = morse_code.at(morse_text);
                }
                morse_text = "";
            }
            
			if (back_button_state.pushing and type_button_state.pushed){
				if (message_text != ""){
					message_text.pop_back();
				}
				back_button.pushed_same_time();
                type_button.clear_button_state();
            } else if (back_button_state.pushed and !back_button_state.pushed_same_time and !type_button_state.pushing){
				break;
			} else if (back_button_state.pushed){
				back_button.clear_button_state();
			}
               
            std::string display_text = message_text + morse_text + alphabet_text;

            sprite.fillRect(0, 0, 128, 64, 0);
            
            sprite.setCursor(0,cursor_position);
            sprite.print(display_text.c_str());
            sprite.pushSprite(&lcd, 0, 0);

            message_text += alphabet_text;
            alphabet_text = "";

			// Enter(送信)キーの判定ロジック
			Button::button_state_t enter_button_state = enter_button.get_button_state();
            if (enter_button_state.pushed and message_text != "") {
                printf("Button pushed!\n");
                printf("Pushing time:%lld\n",enter_button_state.pushing_sec);
                printf("Push type:%c\n",enter_button_state.push_type);
				
				http.post_message(message_text);
				message_text = "";

				SendAnimation();

                enter_button.clear_button_state();
            }



            

            // チャタリング防止用に100msのsleep
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    };
};

class MenuDisplay {
    
    #define NAME_LENGTH_MAX 8

    public:   

	struct menu_t {
        char menu_name[NAME_LENGTH_MAX];
        int display_position_x;
        int display_position_y;
		// void (* func)();
    };

	struct menu_t menu_list[3] = {
		{"Talk",26,0},
		{"Setting",26,22},
		{"Game",26,44}
	};

    int cursor_index = 0;
    
    void Menu() { 
        lcd.init();
        lcd.setRotation(0);
        
        Joystick joystick;
        joystick.setup();
		
		// メニューから遷移する機能のインスタンス
		TalkDisplay talk;

        Button type_button(GPIO_NUM_4);
        Button enter_button(GPIO_NUM_26);

        // 画面が横長になるように回転
        // if (lcd.width() < lcd.height()) lcd.setRotation(lcd.getRotation() ^ 2);

        lcd.setRotation(2);

        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font4);
        sprite.setTextWrap(false);  // 右端到達時のカーソル折り返しを禁止
        sprite.createSprite(lcd.width(), lcd.height());
        
        while (1) {
        	
			// Menu項目を表示させる
			int menu_lists_n = sizeof(menu_list) / sizeof(menu_t);
			for (int i = 0; i <= menu_lists_n; i++){ 
				sprite.setCursor(menu_list[i].display_position_x,menu_list[i].display_position_y);
                sprite.print(menu_list[i].menu_name);  // 1バイトずつ出力
			}
            

            Joystick::joystick_state_t joystick_state = joystick.get_joystick_state();
            printf("C6_Voltage:%d\n",joystick_state.C6_voltage);
            printf("C7_Voltage:%d\n",joystick_state.C7_voltage);
            printf("UP:%s\n", joystick_state.up ? "true" : "false");
            printf("DOWN:%s\n", joystick_state.down ? "true" : "false");
            printf("RIGHT:%s\n", joystick_state.right ? "true" : "false");
            printf("LEFT:%s\n", joystick_state.left ? "true" : "false");
            vTaskDelay(50 / portTICK_PERIOD_MS);

            Button::button_state_t type_button_state = type_button.get_button_state();
            if (type_button_state.pushed) {
                printf("Button pushed!\n");
                printf("Pushing time:%lld\n",type_button_state.pushing_sec);
                printf("Push type:%c\n",type_button_state.push_type);

				if (cursor_index == 0){
					talk.Talk();
				}

                type_button.clear_button_state();
            }

            if (joystick_state.pushed_up_edge){
                cursor_index -= 1;
				if (cursor_index < 0){
					cursor_index = menu_lists_n - 1;
				}
            }
            else if (joystick_state.pushed_down_edge){
                cursor_index += 1;
				if (cursor_index >= menu_lists_n){
					cursor_index = 0;
				}
            }
            
            // Menu選択"->"の表示
			sprite.setCursor(2, menu_list[cursor_index].display_position_y);  // カーソル位置を更新 
            sprite.print("->");  // 1バイトずつ出力 
            
			sprite.pushSprite(&lcd, 0, 0);
            sprite.fillRect(0, 0, 128, 64, 0);
        }
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

    // WIP
    void ShowImage() {         
        lcd.init();
		lcd.clearDisplay();
        lcd.setSwapBytes(false);
        // sprite.setColorDepth(1);
        // sprite.createSprite(lcd.width(), lcd.height());  // 画面幅+１文字分の横幅を用意
        // lcd.setRotation(2);

		const uint16_t imgWidth = 64;
		const uint16_t imgHeight = 64;

		const unsigned char img [] PROGMEM = {
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x80, 0xc0, 0x40, 0x60, 0x30, 0x30, 0x18, 0x18, 0x18, 0x08, 0x0c, 0x0c, 0x0c, 0x0c, 
			0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x08, 0x18, 0x18, 0x10, 0x30, 0x20, 0x60, 0xc0, 0xc0, 0x80, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xe0, 0x38, 0x1c, 
			0x06, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 
			0x0e, 0x1c, 0x70, 0xe0, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x3f, 0x03, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xe0, 0x70, 0x38, 0xfc, 0x7e, 0x3e, 
			0x0f, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x01, 0x07, 0xfe, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x80, 0xe0, 0x78, 0xfc, 0xfc, 0xf0, 0x3c, 0x0f, 0xc3, 0xf0, 0x3c, 0x02, 0x80, 0xe0, 0x78, 
			0xb0, 0xf0, 0x60, 0xf0, 0xf0, 0x00, 0xe0, 0x60, 0x30, 0xf0, 0xe0, 0xc0, 0xe0, 0x30, 0x30, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x1e, 0x78, 0xe0, 0x80, 
			0x00, 0x07, 0x03, 0x00, 0x07, 0x07, 0x01, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 
			0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x03, 0x03, 0x01, 0x00, 0x01, 0x03, 0x03, 0x03, 0x03, 
			0x00, 0x80, 0xe0, 0x38, 0x1f, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 
			0x07, 0x0e, 0x1c, 0x18, 0x30, 0x60, 0x60, 0xc0, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xc0, 0x40, 0x60, 0x30, 0x30, 0x18, 0x0c, 0x06, 
			0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xc0, 0x80, 0x80, 0x80, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0xf0, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x3f, 0x31, 0x2b, 0x2d, 0x2d, 0x6d, 
			0x6d, 0x2d, 0x2d, 0x2f, 0x31, 0x39, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		};

		lcd.startWrite();
		lcd.pushImage(0, 0, imgWidth, imgHeight, img);
		lcd.endWrite();

        // int x = 0;
        // int y = 0;
        // lcd.pushImage(x, y, 32, 13, img);
        // sprite.pushImage(x, y, imgWidth, imgHeight, img);
        //sprite.pushSprite(&lcd, 0, 0);
    }
};

