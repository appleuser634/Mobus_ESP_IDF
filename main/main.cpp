#include <stdio.h>
#include <stdlib.h>

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

static const char *TAG = "Mobus v3.14";

#include <map>
#include <string.h>

#include <LovyanGFX.hpp>
#include <joystick.h>
#include <power_monitor.h>

#include <wifi.hpp>
#include <http_client.hpp>
#include <oled.hpp>

// #include <notification.hpp>

// #include <provisioning.h>
#include <ota.hpp>

extern "C" {
void app_main();
}

void app_main(void) {

  printf("Hello world!!!!\n");

  Oled oled;
  MenuDisplay menu;

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
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  } else {
    // 起動音を鳴らす
    Buzzer buzzer;
    buzzer.boot_sound();
    // 起動時のロゴを表示
    oled.BootDisplay();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }

  // Provisioning provisioning;
  // provisioning.main();

  const gpio_num_t ext_wakeup_pin_0 = GPIO_NUM_3;

  printf("Enabling EXT0 wakeup on pin GPIO%d\n", ext_wakeup_pin_0);
  esp_sleep_enable_ext0_wakeup(ext_wakeup_pin_0, 1);

  rtc_gpio_pullup_dis(ext_wakeup_pin_0);
  rtc_gpio_pulldown_en(ext_wakeup_pin_0);

  // TODO:menuから各機能の画面に遷移するように実装する
  menu.start_menu_task();

  WiFi wifi;
  wifi.main();

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

  //Notification notif;
  //notif.check_notification();

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
