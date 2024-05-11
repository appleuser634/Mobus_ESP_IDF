
/* HTTP GET Example using plain POSIX sockets

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <cstring>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#pragma once

HttpClient http_client;

class Notification {
	
	public:		
		void check_notification();
		void recv_notification();
};

void check_notification_task(void *pvParameters){  
  Notification notif;

  while (1) {
    if(http_client.notif_flag) {
      notif.recv_notification();
      http_client.notif_flag = false;
    } 
    else {
      vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
  }
}

void notification_task(void *pvParameters){
  // 通知音を鳴らす
  Buzzer buzzer;
  buzzer.recv_sound();
  // 通知画面を出す
  Oled oled;
  oled.RecvNotif();
  vTaskDelete(NULL);
}

void Notification::check_notification(){
  xTaskCreatePinnedToCore(&check_notification_task, "check_notification_task", 4096, NULL, 5, NULL, 0);
}

void Notification::recv_notification(){
  // 通知のため全タスクを一時停止
  vTaskSuspendAll();
  xTaskCreatePinnedToCore(&notification_task, "notification_task", 4096, NULL, 20, NULL, 0);
  // 停止したタスクを再開
  xTaskResumeAll();
}
