
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
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "http_client.hpp"
#include "notification_bridge.hpp"

#pragma once

inline HttpClient &http_client = HttpClient::shared();

namespace {

constexpr const char *kTagNotification = "Notification";
constexpr uint32_t kCheckTaskStackWords = 4096;
constexpr uint32_t kNotifyTaskStackWords = 4096;

StackType_t *check_task_stack = nullptr;
StaticTask_t check_task_buffer;
TaskHandle_t check_task_handle = nullptr;

StackType_t *notify_task_stack = nullptr;
StaticTask_t notify_task_buffer;
TaskHandle_t notify_task_handle = nullptr;

StackType_t *alloc_notif_stack(StackType_t *&slot, uint32_t words,
                               const char *label) {
  if (slot) return slot;
  size_t bytes = words * sizeof(StackType_t);
  slot = static_cast<StackType_t *>(heap_caps_malloc(
      bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!slot) {
    ESP_LOGE(kTagNotification,
             "Failed to allocate %s stack (bytes=%u free=%u largest=%u)",
             label, static_cast<unsigned>(bytes),
             static_cast<unsigned>(heap_caps_get_free_size(
                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(
                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  }
  return slot;
}

}  // namespace

class Notification {
	
	public:		
		void check_notification();
		void recv_notification();
};

void check_notification_task(void *pvParameters){  
  Notification notif;

  while (1) {
    if (notification_bridge::consume_external_hint()) {
      if (http_client.refresh_unread_count() != ESP_OK) {
        http_client.force_unread_hint();
      }
    }

    if (http_client.consume_unread_hint()) {
      notif.recv_notification();
      continue;
    }

    if (http_client.refresh_unread_count() == ESP_OK &&
        http_client.consume_unread_hint()) {
      notif.recv_notification();
      continue;
    }

    vTaskDelay(3000 / portTICK_PERIOD_MS);
  }
}

void notification_task(void *pvParameters){
  // 通知音を鳴らす
  Buzzer buzzer;
  buzzer.recv_sound();
  // 通知画面を出す
  Oled oled;
  oled.RecvNotif();
  notify_task_handle = nullptr;
  vTaskDelete(NULL);
}

void Notification::check_notification(){
  if (check_task_handle) {
    ESP_LOGW(kTagNotification, "check_notification_task already running");
    return;
  }
  if (!alloc_notif_stack(check_task_stack, kCheckTaskStackWords,
                         "check_notification")) {
    return;
  }
  check_task_handle = xTaskCreateStaticPinnedToCore(
      &check_notification_task, "check_notification_task", kCheckTaskStackWords,
      nullptr, 5, check_task_stack, &check_task_buffer, 0);
  if (!check_task_handle) {
    ESP_LOGE(kTagNotification, "Failed to create check_notification_task (%u free)",
             static_cast<unsigned>(heap_caps_get_free_size(
                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  }
}

void Notification::recv_notification(){
  // 通知のため全タスクを一時停止
  vTaskSuspendAll();
  if (notify_task_handle) {
    ESP_LOGW(kTagNotification, "notification_task already running");
    xTaskResumeAll();
    return;
  }
  if (!alloc_notif_stack(notify_task_stack, kNotifyTaskStackWords,
                         "notification")) {
    xTaskResumeAll();
    return;
  }
  notify_task_handle = xTaskCreateStaticPinnedToCore(
      &notification_task, "notification_task", kNotifyTaskStackWords, nullptr,
      20, notify_task_stack, &notify_task_buffer, 0);
  if (!notify_task_handle) {
    ESP_LOGE(kTagNotification, "Failed to create notification_task (%u free)",
             static_cast<unsigned>(heap_caps_get_free_size(
                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  }
  // 停止したタスクを再開
  xTaskResumeAll();
}
