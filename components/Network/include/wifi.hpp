#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <nvs_rw.hpp>

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

class WiFi {
   public:
    typedef struct {
        bool connected;  // true or false
        char state;      // "w"aiting or "f"ail or "s"uccess
    } wifi_state_t;

    wifi_state_t wifi_state = {false, 'w'};

    static void event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
        ESP_LOGI(TAG, "===== START WIFI EVENT HADLER =====");
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_base == WIFI_EVENT &&
                   event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry to connect to the AP");
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            ESP_LOGI(TAG, "connect to the AP fail");
        } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_num = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }

    void wifi_set_sta(std::string WIFI_SSID = "", std::string WIFI_PASS = "") {
        s_wifi_event_group = xEventGroupCreate();
        s_retry_num = 0;

        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL,
            &instance_any_id));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL,
            &instance_got_ip));

        wifi_config_t wifi_config = {};

        // Avoid driver writing config to NVS (suspected crash path)
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

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

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "wifi_init_sta finished.");

        ESP_LOGI(TAG, "Connect to SSID:%s, password:%s", WIFI_SSID.c_str(),
                 WIFI_PASS.c_str());

        esp_wifi_connect();

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
            ESP_LOGW(TAG, "wifi_scan: forcing WIFI_MODE_STA for scanning (was %d)", mode);
            if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) {
                ESP_LOGE(TAG, "wifi_scan: esp_wifi_set_mode failed: %s", esp_err_to_name(err));
                return;
            }
        }

        // Starting twice safely returns INVALID_STATE; ignore for robustness
        (void)esp_wifi_start();

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
        // Try saved credentials first (if any); otherwise leave Wi‑Fi idle until user configures
        wifi_connect_saved_any();
    }
};
