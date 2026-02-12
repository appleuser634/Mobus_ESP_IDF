#pragma once

#include <string.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <nvs_rw.hpp>
#include <ble_uart.hpp>
#include <mqtt_runtime.h>

#include "lwip/err.h"
#include "lwip/sys.h"

#define EXAMPLE_ESP_WIFI_SSID "a"
#define EXAMPLE_ESP_WIFI_PASS "b"
#define DEFAULT_SCAN_LIST_SIZE 10

#define EXAMPLE_ESP_MAXIMUM_RETRY 5

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

/* FreeRTOS event group to signal when we are connected*/
extern EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about
 * two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;
static bool s_handlers_registered = false;
static esp_event_handler_instance_t s_instance_any_id = nullptr;
static esp_event_handler_instance_t s_instance_got_ip = nullptr;
static std::atomic<bool> s_wifi_reconfiguring{false};
static std::atomic<bool> s_post_connect_task_running{false};

class WiFi {
   public:
    typedef struct {
        bool connected;  // true or false
        char state;      // "w"aiting or "f"ail or "s"uccess
    } wifi_state_t;

    wifi_state_t wifi_state = {false, 'w'};

    static void post_connect_task(void *pvParameters) {
        (void)pvParameters;
        if (ble_uart_is_ready()) {
            ESP_LOGI(TAG, "Wi-Fi connected; disabling BLE bridge");
            ble_uart_disable();
            (void)mqtt_rt_resume();
            save_nvs((char *)"ble_pair", std::string("false"));
            save_nvs((char *)"ble_code", std::string(""));
            save_nvs((char *)"ble_exp_us", std::string("0"));
        }
        s_post_connect_task_running.store(false);
        vTaskDelete(nullptr);
    }

    static void event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
        ESP_LOGI(TAG, "===== START WIFI EVENT HADLER =====");
        if (event_base == WIFI_EVENT &&
            event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_wifi_event_group) {
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            }
            std::string ble_pair = get_nvs((char *)"ble_pair");
            std::string ble_rst = get_nvs((char *)"ble_wifi_rst");
            std::string wifi_manual = get_nvs((char *)"wifi_manual_off");
            bool forced_off =
                (ble_pair == "true") || (ble_rst == "1") ||
                (wifi_manual == "1");
            if (s_wifi_reconfiguring.load()) {
                ESP_LOGI(TAG, "Wi-Fi reconfiguring; skip auto-reconnect");
                return;
            }
            if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
                if (forced_off) {
                    ESP_LOGI(TAG,
                             "Wi-Fi disconnect requested for BLE pairing; "
                             "skip auto-reconnect");
                } else {
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "retry to connect to the AP");
                }
            } else {
                if (s_wifi_event_group) {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                }
            }
            ESP_LOGI(TAG, "connect to the AP fail");
        } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_num = 0;
            if (s_wifi_event_group) {
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            }
            bool expected = false;
            if (s_post_connect_task_running.compare_exchange_strong(expected,
                                                                     true)) {
                BaseType_t ok = xTaskCreate(&post_connect_task,
                                            "wifi_post_connect", 4096, nullptr,
                                            5, nullptr);
                if (ok != pdPASS) {
                    s_post_connect_task_running.store(false);
                    ESP_LOGW(TAG, "Failed to start wifi_post_connect task");
                }
            }
        }
    }

    void wifi_set_sta(std::string WIFI_SSID = "", std::string WIFI_PASS = "") {
        s_wifi_reconfiguring.store(true);
        if (!s_wifi_event_group) {
            s_wifi_event_group = xEventGroupCreate();
        } else {
            xEventGroupClearBits(s_wifi_event_group,
                                 WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        }
        s_retry_num = 0;

        if (!s_handlers_registered) {
            ESP_ERROR_CHECK(esp_event_handler_instance_register(
                WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL,
                &s_instance_any_id));
            ESP_ERROR_CHECK(esp_event_handler_instance_register(
                IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL,
                &s_instance_got_ip));
            s_handlers_registered = true;
        }

        wifi_config_t wifi_config = {};

        // Avoid driver writing config to NVS (suspected crash path)
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

        bool wifi_was_started = false;
        wifi_mode_t current_mode = WIFI_MODE_NULL;
        if (esp_wifi_get_mode(&current_mode) == ESP_OK &&
            current_mode != WIFI_MODE_NULL) {
            wifi_was_started = true;
        }

        if (WIFI_SSID == "" && WIFI_PASS == "") {
            ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config));
        } else {
            // Safe copy with bounds to avoid overflow and keep NUL-termination
            size_t ssid_len = WIFI_SSID.size();
            if (ssid_len >= sizeof(wifi_config.sta.ssid)) {
                ssid_len = sizeof(wifi_config.sta.ssid) - 1;
            }
            memcpy(wifi_config.sta.ssid, WIFI_SSID.c_str(), ssid_len);
            wifi_config.sta.ssid[ssid_len] = '\0';

            size_t pass_len = WIFI_PASS.size();
            if (pass_len >= sizeof(wifi_config.sta.password)) {
                pass_len = sizeof(wifi_config.sta.password) - 1;
            }
            memcpy(wifi_config.sta.password, WIFI_PASS.c_str(), pass_len);
            wifi_config.sta.password[pass_len] = '\0';
        }

        // If we are already running, disconnect and stop before applying new config
        if (wifi_was_started) {
            esp_err_t err = esp_wifi_disconnect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED &&
                err != ESP_ERR_WIFI_CONN) {
                ESP_LOGW(TAG, "esp_wifi_disconnect returned %s",
                         esp_err_to_name(err));
            }
            err = esp_wifi_stop();
            if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
                ESP_LOGW(TAG, "esp_wifi_stop returned %s", esp_err_to_name(err));
            }
        }

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_err_t start_err = esp_wifi_start();
        if (start_err == ESP_ERR_WIFI_CONN ||
            start_err == ESP_ERR_WIFI_NOT_STOPPED) {
            start_err = ESP_OK;
        }
        ESP_ERROR_CHECK(start_err);

        ESP_LOGI(TAG, "wifi_init_sta finished.");

        ESP_LOGI(TAG, "Connect to SSID:%s, password:%s", WIFI_SSID.c_str(),
                 WIFI_PASS.c_str());

        esp_err_t conn_err = esp_wifi_connect();
        s_wifi_reconfiguring.store(false);
        ESP_ERROR_CHECK(conn_err);

        /* Waiting until either the connection is established
         * (WIFI_CONNECTED_BIT) or connection failed for the maximum number of
         * re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see
         * above) */
        // EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        //         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        //         pdFALSE,
        //         pdFALSE,
        //         portMAX_DELAY);
        //
        // ESP_LOGI(TAG, "xEventGroupWaitBits finished.");

        /* xEventGroupWaitBits() returns the bits before the call returned,
         * hence we can test which event actually happened. */
        // if (bits & WIFI_CONNECTED_BIT) {
        //     ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
        //              WIFI_SSID.c_str(), WIFI_PASS.c_str());
        //     wifi_state.connected = true;
        //     wifi_state.state = 's';
        // } else if (bits & WIFI_FAIL_BIT) {
        //     ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
        //              WIFI_SSID.c_str(), WIFI_PASS.c_str());
        //     wifi_state.connected = false;
        //     wifi_state.state = 'f';
        // } else {
        //     ESP_LOGE(TAG, "UNEXPECTED EVENT");
        // }
    }

    void wifi_init_sta() {
        ESP_ERROR_CHECK(esp_netif_init());

        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        // Keep Wi-Fi config in RAM to avoid NVS writes from driver
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        // Set STA mode once during init. Reconfiguring mode repeatedly during
        // scan flow can destabilize Wi-Fi heap internals on some paths.
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    }

    bool wifi_connect_saved_any(uint32_t timeout_ms_per = 12000) {
        auto creds = get_wifi_credentials();
        for (auto &p : creds) {
            ESP_LOGI(TAG, "Trying Wi-Fi SSID:%s", p.first.c_str());
            wifi_set_sta(p.first, p.second);
            // Wait until connected or fail bit
            EventBits_t bits = xEventGroupWaitBits(
                s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms_per));
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "Connected to SSID:%s", p.first.c_str());
                return true;
            }
            ESP_LOGW(TAG, "Failed to connect SSID:%s", p.first.c_str());
        }
        return false;
    }

    wifi_state_t get_wifi_state() { return wifi_state; }

    static void wifi_scan(uint16_t *number, wifi_ap_record_t *ap_info) {
        if (number == nullptr || ap_info == nullptr) {
            ESP_LOGE(TAG, "wifi_scan: invalid args (number/ap_info is null)");
            return;
        }

        // Clear caller-provided buffer up to its capacity
        memset(ap_info, 0, sizeof(wifi_ap_record_t) * (*number));

        // Ensure Wi‑Fi is started and in a mode that supports scanning
        wifi_mode_t mode = WIFI_MODE_NULL;
        esp_err_t err = esp_wifi_get_mode(&mode);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wifi_scan: esp_wifi_get_mode failed: %s", esp_err_to_name(err));
            return;
        }
        if (!(mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)) {
            ESP_LOGW(TAG,
                     "wifi_scan: unsupported wifi mode for scan (%d); skip",
                     mode);
            *number = 0;
            return;
        }

        wifi_scan_config_t scan_config = {};
        scan_config.ssid = nullptr;
        scan_config.bssid = nullptr;
        scan_config.channel = 0;        // all channels
        scan_config.show_hidden = false; // do not show hidden by default

        err = esp_wifi_scan_start(&scan_config, true /* block until done */);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wifi_scan: scan_start failed: %s", esp_err_to_name(err));
            *number = 0;
            return;
        }

        uint16_t ap_count = 0;
        err = esp_wifi_scan_get_ap_num(&ap_count);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wifi_scan: scan_get_ap_num failed: %s", esp_err_to_name(err));
            *number = 0;
            return;
        }

        uint16_t to_copy = (*number < ap_count) ? *number : ap_count;
        ESP_LOGI(TAG, "Max AP slots = %u, scanned APs = %u, copying = %u", *number, ap_count, to_copy);
        if (to_copy == 0) {
            *number = 0;
            return;
        }

        err = esp_wifi_scan_get_ap_records(&to_copy, ap_info);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wifi_scan: get_ap_records failed: %s", esp_err_to_name(err));
            *number = 0;
            return;
        }
        *number = to_copy;
    }

    void main(void) {
        // Initialize NVS
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
            ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
        wifi_init_sta();
        // Disable power save to avoid driver path timing issues during bring-up
        esp_wifi_set_ps(WIFI_PS_NONE);
        // Start Wi-Fi once during startup so scan path doesn't perform mode
        // transitions and driver bring-up repeatedly.
        {
            esp_err_t start_err = esp_wifi_start();
            if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_CONN &&
                start_err != ESP_ERR_WIFI_NOT_STOPPED) {
                ESP_ERROR_CHECK(start_err);
            }
        }
        // Try saved credentials first unless Wi‑Fi is manually disabled.
        if (get_nvs((char *)"wifi_manual_off") != "1") {
            wifi_connect_saved_any();
        } else {
            ESP_LOGI(TAG, "Wi-Fi disabled by user; skip auto-connect");
            (void)esp_wifi_stop();
        }
    }
};
