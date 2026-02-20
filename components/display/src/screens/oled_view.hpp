#pragma once

#include "../runtime/prelude.hpp"

class Oled {
   public:
    void BootDisplay() {
        printf("Booting!!!\n");

        constexpr int kBootWidth = 128;
        constexpr int kBootHeight = 64;
        if (!ensure_sprite_surface(kBootWidth, kBootHeight, 8, "BootDisplay")) {
            return;
        }

        sprite.drawBitmap(32, 0, mimocLogo, 64, 64, TFT_WHITE, TFT_BLACK);
        push_sprite_safe(0, 0);

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

            push_sprite_safe(0, 0);
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
            push_sprite_safe(0, 0);
        }

        for (int i = 20; i >= -50; i--) {
            sprite.setCursor(25, i);   // カーソル位置を更新
            sprite.print(notif_text);  // 1バイトずつ出力
            // sprite.scroll(0, 0);  //
            // キャンバスの内容を1ドット上にスクロール
            push_sprite_safe(0, 0);
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

        push_sprite_safe(0, 0);
    }
};
