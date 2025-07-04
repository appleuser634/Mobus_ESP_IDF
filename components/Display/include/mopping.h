#include <stdbool.h>
#include "oled.hpp"

typedef struct {
    int id;
    const unsigned char *img;
    const unsigned char *img_1;
    const unsigned char *img_2;
    bool show;
    int x;
    int y;
    int w;
    int h;
} character;

/*
 * random byte generator
 */
int get_random(int num) { return rand() % num; }

void string_concat(char *dest, const char *src) {
    // destの末尾を見つける
    while (*dest != '\0') {
        dest++;
    }

    // srcをdestにコピーする
    while (*src != '\0') {
        *dest = *src;
        dest++;
        src++;
    }

    // 最後にヌル文字を追加
    *dest = '\0';
}

void intToStr(int num, char *str) {
    int i = 0;
    int isNegative = 0;

    // 負の数の場合、符号を記録し、正の数に変換
    if (num < 0) {
        isNegative = 1;
        num = -num;
    }

    // 数字を1桁ずつ取り出して、文字に変換して保存
    do {
        str[i++] = (num % 10) + '0';  // 最後の桁を文字に変換して保存
        num /= 10;
    } while (num != 0);

    // 負の数の場合、符号を追加
    if (isNegative) {
        str[i++] = '-';
    }

    // 文字列の終端
    str[i] = '\0';

    // 桁が逆になっているので、逆順に並べ替える
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

void opening() {
    bool button_state = false;
    int c = 0;

    Joystick joystick;

    Button type_button(GPIO_NUM_46);
    Button back_button(GPIO_NUM_3);
    Button enter_button(GPIO_NUM_5);

    sprite.setFont(&fonts::Font2);

    for (int i = 0; i < 20; i += 1) {
        sprite.fillRect(0, 0, 128, 64, 0);
        sprite.setCursor(38, i);  // カーソル位置を更新
        sprite.print("MOPPING");  // 1バイトずつ出力
        sprite.pushSprite(&lcd, 0, 0);
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    while (1) {
        sprite.fillRect(0, 0, 128, 64, 0);

        sprite.setCursor(38, 20);  // カーソル位置を更新
        sprite.print("MOPPING");   // 1バイトずつ出力

        if (c > 50) {
            sprite.setCursor(45, 45);  // カーソル位置を更新
            sprite.setTextColor(0x000000u, 0xFFFFFFu);
            sprite.drawCenterString("start", 64, 45);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            // ssd1306_drawRect(50, 50, 40, 10, 0);
        } else {
            sprite.setCursor(45, 45);  // カーソル位置を更新
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            sprite.drawCenterString("start", 64, 45);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
        }

        sprite.pushSprite(&lcd, 0, 0);

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

        // ジョイスティック左を押されたらメニューへ戻る
        // 戻るボタンを押されたらメニューへ戻る
        if (joystick_state.left || back_button_state.pushed) {
            type_button.clear_button_state();
            type_button.reset_timer();
            joystick.reset_timer();
            vTaskDelay(1 / portTICK_PERIOD_MS);
            return;
        }

        // ボタンを押して離した後にゲームに移る
        if (type_button_state.pushed) {
            type_button.clear_button_state();
            type_button.reset_timer();
            joystick.reset_timer();
            vTaskDelay(1 / portTICK_PERIOD_MS);
            break;
        }

        c++;
        if (c > 100) {
            c = 0;
        }
    }

    int kuina_x = -130;
    int kuina_y = 0;
    int kuina_num = 18;
    character kuinas[17] = {
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x, kuina_y, 16, 16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 10, kuina_y + 50, 16, 16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 20, kuina_y + 10, 16, 16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 30, kuina_y + 30, 16, 16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 40, kuina_y + 0, 16, 16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 50, kuina_y + 50, 16, 16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 60, kuina_y + 20, 16, 16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 70, kuina_y + 40, 16, 16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 80, kuina_y + 10, 16, 16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 90, kuina_y + 50, 16, 16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 100, kuina_y, 16, 16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 110, kuina_y + 50, 16,
         16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 120, kuina_y + 10, 16,
         16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 130, kuina_y + 30, 16,
         16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 140, kuina_y + 0, 16, 16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 150, kuina_y + 50, 16,
         16},
        {100, kuina_1, kuina_1, kuina_2, 1, kuina_x + 160, kuina_y + 20, 16,
         16},
    };

    while (1) {
        sprite.fillRect(0, 0, 128, 64, 0);
        if (kuinas[16].x < 64) {
            sprite.setCursor(38, 20);  // カーソル位置を更新
            sprite.print("MOPPING");   // 1バイトずつ出力
            sprite.setCursor(38, 20);  // カーソル位置を更新
            sprite.print("start");     // 1バイトずつ出力
        }

        for (int i = 0; i < kuina_num; i++) {
            if (kuinas[i].x > 0) {
                if (c < 10) {
                    kuinas[i].img = kuinas[i].img_1;
                } else {
                    kuinas[i].img = kuinas[i].img_2;
                }
                sprite.drawBitmap(kuinas[i].x, kuinas[i].y, kuinas[i].img,
                                  kuinas[i].w, kuinas[i].h, TFT_WHITE,
                                  TFT_BLACK);
            }
            kuinas[i].x += 1;
        }

        sprite.pushSprite(&lcd, 0, 0);

        if (kuinas[0].x > 128) {
            break;
        }

        c++;
        if (c > 20) {
            c = 0;
        }
    }

    character mongoose = {2, mongoose_1, mongoose_1, mongoose_2, 1,
                          0, 30,         16,         8};

    while (1) {
        sprite.fillRect(0, 0, 128, 64, 0);

        if (c < 10) {
            mongoose.img = mongoose.img_1;
        } else {
            mongoose.img = mongoose.img_2;
        }

        sprite.drawBitmap(mongoose.x, mongoose.y, mongoose.img, mongoose.w,
                          mongoose.h, TFT_WHITE, TFT_BLACK);

        sprite.pushSprite(&lcd, 0, 0);

        mongoose.x += 1;
        if (mongoose.x > 128) {
            break;
        }

        c++;
        if (c > 20) {
            c = 0;
        }
    }
}

void gen_enemy(character *enemy) {
    int character_n = get_random(10);
    if (character_n <= 5) {
        enemy[2].id = 0;
        enemy[2].img = grass_1;
        enemy[2].img_1 = grass_1;
        enemy[2].img_2 = grass_1;
        enemy[2].show = true;
        enemy[2].y = 53;
        enemy[2].w = 8;
        enemy[2].h = 8;
    } else if (character_n <= 7) {
        enemy[2].id = 1;
        enemy[2].img = pineapple_1;
        enemy[2].img_1 = pineapple_1;
        enemy[2].img_2 = pineapple_1;
        enemy[2].show = true;
        enemy[2].y = 45;
        enemy[2].w = 16;
        enemy[2].h = 16;
    } else if (character_n <= 8) {
        enemy[2].id = 2;
        enemy[2].img = mongoose_left_1;
        enemy[2].img_1 = mongoose_left_1;
        enemy[2].img_2 = mongoose_left_2;
        enemy[2].show = true;
        enemy[2].y = 53;
        enemy[2].w = 16;
        enemy[2].h = 8;
    } else if (character_n <= 9) {
        enemy[2].id = 3;
        enemy[2].img = earthworm_1;
        enemy[2].img_1 = earthworm_1;
        enemy[2].img_2 = earthworm_2;
        enemy[2].show = true;
        enemy[2].y = 53;
        enemy[2].w = 8;
        enemy[2].h = 8;
    }

    int enemy_distance = get_random(30);
    int distance_buffer = 60;
    enemy[2].x = enemy[1].x + distance_buffer + enemy_distance;
}

bool game_loop() {
    int init_kuina_x = 15;
    int init_kuina_y = 45;
    character kuina = {100,          kuina_1,      kuina_1, kuina_2, 0,
                       init_kuina_x, init_kuina_y, 16,      16};

    // struct character enemy =
    // {pineapple_1,pineapple_1,pineapple_1,130,53,8,8};
    character enemy[3] = {
        {0, grass_1, grass_1, grass_1, 1, 130, 53, 8, 8},
        {0, grass_1, grass_1, grass_1, 1, 160, 53, 8, 8},
        {1, pineapple_1, pineapple_1, pineapple_1, 1, 210, 45, 16, 16}};

    int flip_c = 0;
    bool flip_flag = false;

    bool button_state = false;

    bool jump_flag = false;
    int max_jump = 30;
    int jump_progress = 0;

    bool danger_flag = false;

    // 0 is grass      50%
    // 1 is pineapple  30%
    // 2 is mongoose   10%
    // 3 is earthworm  10%
    int enemy_num = 0;

    int random_n = get_random(10);

    int count = 0;
    int score = 0;

    bool loop_flag = true;

    bool hart_flag = false;
    int hart_cnt = 0;

    Joystick joystick;

    Button type_button(GPIO_NUM_46);
    Button back_button(GPIO_NUM_3);
    Button enter_button(GPIO_NUM_5);

    Led led;
    Neopixel neopixel;

    while (loop_flag) {
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

        // jump trigger
        if (type_button_state.push_edge && !jump_flag) {
            jump_flag = true;
            type_button.clear_button_state();
            type_button.reset_timer();
            joystick.reset_timer();
        } else if (back_button_state.pushed || joystick_state.left) {
            type_button.clear_button_state();
            type_button.reset_timer();
            joystick.reset_timer();
            return false;
        }

        // jump animation
        if (jump_flag && (max_jump * 2) < jump_progress) {
            jump_progress = 0;
            jump_flag = false;
            kuina.y = init_kuina_y;
        } else if (jump_flag && max_jump >= jump_progress) {
            kuina.y -= 1;
            jump_progress += 1;
        } else if (jump_flag && max_jump < jump_progress) {
            kuina.y += 1;
            jump_progress += 1;
        }

        flip_c++;
        if (flip_c > 20) {
            flip_flag = !flip_flag;
            flip_c = 0;
        }

        if (enemy[0].x < 0) {
            enemy[0] = enemy[1];
            enemy[1] = enemy[2];
            gen_enemy(enemy);
        }

        // flip character
        if (flip_flag) {
            enemy[0].img = enemy[0].img_1;
            enemy[1].img = enemy[1].img_1;
            enemy[2].img = enemy[2].img_1;
            kuina.img = kuina.img_1;
        } else {
            enemy[0].img = enemy[0].img_2;
            enemy[1].img = enemy[1].img_2;
            enemy[2].img = enemy[2].img_2;
            kuina.img = kuina.img_2;
        }

        for (int i = 0; i < 3; i++) {
            // draw enemy
            if (enemy[i].show) {
                sprite.drawBitmap(enemy[i].x, enemy[i].y, enemy[i].img,
                                  enemy[i].w, enemy[i].h, TFT_WHITE, TFT_BLACK);
            }

            enemy[i].x -= 1;

            // hit judge
            if (kuina.x + 12 < enemy[i].x + 12 &&
                (kuina.x + 4 + kuina.w) > enemy[i].x + 12 &&
                kuina.y > (init_kuina_y - enemy[i].h)) {
                if (enemy[i].id == 1 || enemy[i].id == 2) {
                    sprite.setCursor(25, 20);
                    sprite.print("GAME OVER!!");
                    loop_flag = false;
                    break;
                } else if (enemy[i].id == 3 && enemy[i].show) {
                    // bonus point
                    score += 100;
                    enemy[i].show = false;
                    hart_flag = true;
                }
            }
        }

        if (hart_flag && hart_cnt == 0) {
            led.led_on();
            neopixel.set_color(0, 10, 0);
        } else if (hart_flag && hart_cnt == 10) {
            led.led_off();
            neopixel.set_color(0, 0, 0);
        }

        if (hart_flag && hart_cnt < 60) {
            sprite.drawBitmap(20, 30, hart, 8, 8, TFT_WHITE, TFT_BLACK);
            hart_cnt++;
        }

        else {
            hart_flag = false;
            hart_cnt = 0;
        }

        // draw kuina
        sprite.drawBitmap(kuina.x, kuina.y, kuina.img, kuina.w, kuina.h,
                          TFT_WHITE, TFT_BLACK);

        count += 1;
        if (count > 10) {
            score += 1;
            count = 0;
        }

        // draw jump score
        char score_txt[30] = "Score:";
        char str_score[30];
        intToStr(score, str_score);
        string_concat(score_txt, str_score);
        sprite.setCursor(0, 0);
        sprite.print(score_txt);

        // draw_road();
        sprite.pushSprite(&lcd, 0, 0);
        sprite.fillRect(0, 0, 128, 64, 0);
    }

    // Game Over Animation
    led.led_on();
    neopixel.set_color(10, 0, 0);
    vTaskDelay(300 / portTICK_PERIOD_MS);
    led.led_off();
    neopixel.set_color(0, 0, 0);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
    // wait push button...
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
        // jump trigger
        if (type_button_state.pushed) {
            type_button.clear_button_state();
            type_button.reset_timer();
            joystick.reset_timer();
            return true;
        } else if (back_button_state.pushed || joystick_state.left) {
            type_button.clear_button_state();
            type_button.reset_timer();
            joystick.reset_timer();
            return false;
        }
    }
}

void mopping_main() {
    opening();
    while (1) {
        bool cont = game_loop();
        if (!cont) {
            break;
        }
    }
}
