#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#define EXAMPLE_ESP_WIFI_SSID "a"
#define EXAMPLE_ESP_WIFI_PASS "b"
#define DEFAULT_SCAN_LIST_SIZE 10

#define EXAMPLE_ESP_MAXIMUM_RETRY 5

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

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

        if (WIFI_SSID == "" && WIFI_PASS == "") {
            ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config));
        } else {
            strcpy((char *)wifi_config.sta.ssid, WIFI_SSID.c_str());
            strcpy((char *)wifi_config.sta.password, WIFI_PASS.c_str());
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
        wifi_set_sta();
    }

    wifi_state_t get_wifi_state() { return wifi_state; }

    static void wifi_scan(uint16_t *number, wifi_ap_record_t *ap_info) {
        uint16_t ap_count = 0;
        memset(ap_info, 0, sizeof(*ap_info));

        esp_wifi_scan_start(NULL, true);

        ESP_LOGI(TAG, "Max AP number ap_info can hold = %u", *number);
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(number, ap_info));
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
        ESP_LOGI(TAG,
                 "Total APs scanned = %u, actual AP number ap_info holds = %u",
                 ap_count, *number);
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
        // esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    }
};
