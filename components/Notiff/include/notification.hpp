
/* HTTP GET Example using plain POSIX sockets

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <cstring>
#include <stdlib.h>
#include <string.h>

#pragma once

class Notification {

public:
  static HttpClient http;

  void check_notification() {
    xTaskCreate(&check_notification_task, "check_notification_task", 4096, NULL,
                5, NULL);
  }

  static void recv_notification() {
    // 通知のため全タスクを一時停止
    vTaskSuspendAll();
    xTaskCreate(&notification_task, "notification_task", 4096, NULL, 20, NULL);
    // 停止したタスクを再開
    xTaskResumeAll();
  }

private:
  static void check_notification_task(void *pvParameters) {

    while (1) {
      if (http.notif_flag) {
        Notification::recv_notification();
        http.notif_flag = false;
      } else {
        vTaskDelay(3000 / portTICK_PERIOD_MS);
      }
    }
  }

  static void notification_task(void *pvParameters) {
    // 通知音を鳴らす
    Buzzer buzzer;
    buzzer.recv_sound();
    // 通知画面を出す
    Oled oled;
    oled.RecvNotif();
    vTaskDelete(NULL);
  }
};
