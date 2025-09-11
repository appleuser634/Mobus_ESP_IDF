#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>

#include "esp_psram.h"  // v4.4+（古いIDFは esp_spiram.h）

#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "sdkconfig.h"
#include <ArduinoJson.h>
#include "esp_now.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

void check_heap() {
    ESP_LOGI("HEAP", "Largest Free Block: %d",
             heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
}

static const char *TAG = "Mobus v3.14";

#include <map>
#include <string.h>

#include <LovyanGFX.hpp>
#include <joystick.h>
#include <power_monitor.h>

#include <nvs_rw.hpp>
#include <wifi.hpp>
#include <http_client.hpp>
#include <neopixel.hpp>
Neopixel neopixel;
#include <oled.hpp>
#include <ntp.hpp>

#define uS_TO_S_FACTOR 1000000ULL  // 秒→マイクロ秒

// #include <notification.hpp>

// #include <provisioning.h>
// OTA client (auto update)
#include "ota_client.hpp"

extern "C" {
void app_main();
}

void check_notification() {
    Oled oled;
    Buzzer buzzer;
    Led led;

    printf("通知チェック中...");
    HttpClient http_client;
    // 通知の取得
    http_client.start_notifications();

    int timeout = 3;
    for (int i = 0; i < timeout; i++) {
        JsonDocument notif_res = http_client.get_notifications();

        for (int i = 0; i < notif_res["notifications"].size(); i++) {
            std::string notification_flag(
                notif_res["notifications"][i]["notification_flag"]);
            if (notification_flag == "true") {
                printf("got notification!");

                for (int n = 0; n < 2; n++) {
                    buzzer.buzzer_on(2600);
                    led.led_on();
                    neopixel.set_color(0, 10, 100);
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                    buzzer.buzzer_off();
                    led.led_off();
                    neopixel.set_color(0, 0, 0);
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                }
                neopixel.set_color(0, 10, 10);
                save_nvs("notif_flag", "true");
                return;
            } else {
                printf("notofication not found");
                save_nvs("notif_flag", "false");
            }
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void app_main(void) {
    printf("Hello world!!!!\n");

    Oled oled;
    MenuDisplay menu;
    ProfileSetting profile_setting;

    WiFi wifi;
    wifi.main();

    initialize_sntp();
    start_rtc_task();

    // Deep Sleep Config
    const gpio_num_t ext_wakeup_pin_0 = GPIO_NUM_3;

    printf("Enabling EXT0 wakeup on pin GPIO%d\n", ext_wakeup_pin_0);
    esp_sleep_enable_ext0_wakeup(ext_wakeup_pin_0, 1);

    rtc_gpio_pullup_dis(ext_wakeup_pin_0);
    rtc_gpio_pulldown_en(ext_wakeup_pin_0);

    // 次のDeep Sleepの設定
    const int sleep_time_sec = 30;
    esp_sleep_enable_timer_wakeup(sleep_time_sec * uS_TO_S_FACTOR);

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        //// キャラクターの挨拶を描画
        // for (int i = 0; i <= 2; i++) {
        //   oled.ShowImage(robo1);
        //   vTaskDelay(800 / portTICK_PERIOD_MS);

        //  oled.ShowImage(robo2);
        //  vTaskDelay(800 / portTICK_PERIOD_MS);
        //}

        // 起動音を鳴らす
        Buzzer buzzer;
        buzzer.boot_sound();
        // 起動時のロゴを表示
        oled.BootDisplay();
        // LEDを光らす
        for (int i = 0; i < 50; i++) {
            neopixel.set_color(i, i, i);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        for (int i = 50; i > 0; i--) {
            neopixel.set_color(i, i, i);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }

    } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        std::string notif_flag = get_nvs("notif_flag");
        if (notif_flag == "false") {
            printf("wake up from timer");
            EventBits_t bits =
                xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                    pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "Wi-Fi Connected");
                // check_notification();
            } else {
                ESP_LOGW(TAG, "Wi-Fi Connection Timeout");
            }
        }
        esp_deep_sleep_start();
    } else {
        // 起動音を鳴らす
        Buzzer buzzer;
        buzzer.boot_sound();
        // 起動時のロゴを表示
        oled.BootDisplay();
        // LEDを光らす
        for (int i = 0; i < 50; i++) {
            neopixel.set_color(i, i, i);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        for (int i = 50; i > 0; i--) {
            neopixel.set_color(i, i, i);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
    }

    // Provisioning provisioning;
    // provisioning.main();

    std::string user_name = get_nvs("user_name");
    if (user_name == "") {
        profile_setting.profile_setting_task();
    }

    // Start OTA auto-update background if enabled
    {
        std::string auto_flag = get_nvs((char *)"ota_auto");
        if (auto_flag == "true") {
            ota_client::start_background_task();
        }
    }
    // TODO:menuから各機能の画面に遷移するように実装する
    save_nvs("notif_flag", "false");
    menu.start_menu_task();

    // HttpClient http_client;
    // std::string chat_to = "hazuki";
    // http_client.get_message(chat_to);
    // http_client.post_message();

    // Start ota task.
    // Ota ota;
    // ota.main();

    // FIXME
    // for (int i = 0; i <= 10; i++) {
    //   WiFi::wifi_state_t wifi_state = wifi.get_wifi_state();
    //   printf("Wifi state:%c\n", wifi_state.state);
    //   vTaskDelay(1000 / portTICK_PERIOD_MS);
    // }

    // HttpClient http;
    // http.start_receiving_wait();

    // Notification notif;
    // notif.check_notification();

    // app_mainが終了しないように無限ループ
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    };

    for (int i = 10; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}
