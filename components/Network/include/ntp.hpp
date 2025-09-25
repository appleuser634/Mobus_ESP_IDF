#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// SNTP初期化とNTPからの時刻同期
static void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp.nict.jp");  // 日本のNICTサーバー
    sntp_init();
}

// 時刻同期完了を待つ
static void wait_for_time_sync(void) {
    time_t now = 0;
    struct tm timeinfo = {0};

    int retry = 0;
    const int retry_count = 5;
    while (timeinfo.tm_year < (2016 - 1900) && ++retry <= retry_count) {
        // Use ROM-backed logging to avoid VFS/stdout interactions during early
        // network bring-up or unusual console states.
        ESP_DRAM_LOGI(TAG,
                      "Waiting for system time to be set... (%d/%d)", retry,
                      retry_count);
        vTaskDelay(pdMS_TO_TICKS(500));
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_DRAM_LOGW(TAG, "System time sync timed out");
    }
}

namespace {

constexpr uint32_t kSetRtcTaskStackWords = 4048;
StaticTask_t set_rtc_task_buffer;
TaskHandle_t set_rtc_task_handle = nullptr;
StackType_t *set_rtc_task_stack = nullptr;

StackType_t *ensure_set_rtc_stack() {
    if (set_rtc_task_stack) return set_rtc_task_stack;
    size_t bytes = kSetRtcTaskStackWords * sizeof(StackType_t);
    set_rtc_task_stack = static_cast<StackType_t *>(heap_caps_malloc(
        bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!set_rtc_task_stack) {
        ESP_LOGE(TAG, "set_rtc stack alloc failed (bytes=%u free=%u)",
                 static_cast<unsigned>(bytes),
                 static_cast<unsigned>(heap_caps_get_free_size(
                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    }
    return set_rtc_task_stack;
}

}  // namespace

void set_rtc(void *pvParameters) {
    // Set RTC
    while (1) {
        if (s_wifi_event_group == nullptr) {
            vTaskDelay(500 / portTICK_PERIOD_MS);
            continue;
        }
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT, pdFALSE,
                                               pdTRUE, pdMS_TO_TICKS(10000));
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Wi-Fi Connected");
            wait_for_time_sync();
        }

        vTaskDelay(pdMS_TO_TICKS(1 * 60 * 1000));
    }
}

void start_rtc_task() {
    printf("Start Rtc Task...");
    if (set_rtc_task_handle) {
        ESP_LOGW(TAG, "set_rtc task already running");
        return;
    }
    if (!ensure_set_rtc_stack()) {
        return;
    }
    set_rtc_task_handle = xTaskCreateStaticPinnedToCore(
        set_rtc, "set_rtc", kSetRtcTaskStackWords, nullptr, 6,
        set_rtc_task_stack, &set_rtc_task_buffer, 0);
    if (!set_rtc_task_handle) {
        ESP_LOGE(TAG, "Failed to start set_rtc task (free_heap=%u)",
                 static_cast<unsigned>(heap_caps_get_free_size(
                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    }
}
