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
    const int retry_count = 10;
    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry,
                 retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

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
    xTaskCreatePinnedToCore(set_rtc, "set_rtc", 4048, NULL, 6, NULL, 0);
}
