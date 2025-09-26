/* ESP HTTP Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
#include <string>
#include <utility>
#include <atomic>
#include "esp_attr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
// #include "protocol_examples_utils.h"
#include "esp_tls.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#include "esp_http_client.h"

#include "chat_api.hpp"
#include "mqtt_runtime.h"
#include <notification_effects.hpp>

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

inline chatapi::ChatApiClient &dev_chat_api() {
    auto &client = chatapi::shared_client(true);
    client.set_scheme("https");
    return client;
}

inline std::atomic<bool> &notifications_task_running_flag() {
    static std::atomic<bool> flag{false};
    return flag;
}

/* Root cert for howsmyssl.com, taken from howsmyssl_com_root_cert.pem

   The PEM file was extracted from the output of this command:
   openssl s_client -showcerts -connect www.howsmyssl.com:443 </dev/null

   The CA root cert is the last cert given in the chain of certs.

   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
extern const char howsmyssl_com_root_cert_pem_start[] asm(
    "_binary_howsmyssl_com_root_cert_pem_start");
extern const char howsmyssl_com_root_cert_pem_end[] asm(
    "_binary_howsmyssl_com_root_cert_pem_end");

esp_err_t _http_client_event_handler(esp_http_client_event_t *evt) {
    static char *output_buffer;  // Buffer to store response of http request
                                 // from event handler
    static int output_len;       // Stores number of bytes read
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s",
                     evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // Clean the buffer in case of a new request
            if (output_len == 0 && evt->user_data) {
                // we are just starting to copy the output data into the use
                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
            }
            /*
             *  Check for chunked encoding is added as the URL for chunked
             * encoding used in this example returns binary data. However, event
             * handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the
                // buffer
                int copy_len = 0;
                if (evt->user_data) {
                    // The last byte in evt->user_data is kept for the NULL
                    // character in case of out-of-bound access.
                    copy_len = MIN(evt->data_len,
                                   (MAX_HTTP_OUTPUT_BUFFER - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data,
                               copy_len);
                    }
                } else {
                    int content_len =
                        esp_http_client_get_content_length(evt->client);
                    if (output_buffer == NULL) {
                        // We initialize output_buffer with 0 because it is used
                        // by strlen() and similar functions therefore should be
                        // null terminated.
                        output_buffer =
                            (char *)calloc(content_len + 1, sizeof(char));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(
                                TAG,
                                "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    copy_len = MIN(evt->data_len, (content_len - output_len));
                    if (copy_len) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                    }
                }
                output_len += copy_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH: {
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below
                // line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        }
        case HTTP_EVENT_DISCONNECTED: {
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(
                (esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        }
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "From", "user@example.com");
            esp_http_client_set_header(evt->client, "Accept", "text/html");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
}

static constexpr uint32_t kHttpGetTaskStackWords = 6192;
static StackType_t *http_get_task_stack = nullptr;
static StaticTask_t http_get_task_buffer;
static TaskHandle_t http_get_task_handle = nullptr;

static constexpr uint32_t kHttpFriendsTaskStackWords = 6144;
static StackType_t *http_get_friends_task_stack = nullptr;
static StaticTask_t http_get_friends_task_buffer;
static TaskHandle_t http_get_friends_task_handle = nullptr;
static TaskHandle_t http_get_friends_waiter = nullptr;

struct HttpFriendsTaskResult {
    esp_err_t err = ESP_FAIL;
    int status = 0;
    std::string payload;
};

static HttpFriendsTaskResult *http_get_friends_result = nullptr;

static StackType_t *ensure_http_get_stack() {
    if (http_get_task_stack) return http_get_task_stack;
    size_t bytes = kHttpGetTaskStackWords * sizeof(StackType_t);
    http_get_task_stack = static_cast<StackType_t *>(heap_caps_malloc(
        bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!http_get_task_stack) {
        ESP_LOGE(TAG,
                 "Failed to alloc http_get stack (bytes=%u free=%u largest=%u)",
                 static_cast<unsigned>(bytes),
                 static_cast<unsigned>(heap_caps_get_free_size(
                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(
                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    }
    return http_get_task_stack;
}

static StackType_t *ensure_http_friends_stack() {
    if (http_get_friends_task_stack) return http_get_friends_task_stack;
    size_t bytes = kHttpFriendsTaskStackWords * sizeof(StackType_t);
    http_get_friends_task_stack = static_cast<StackType_t *>(heap_caps_malloc(
        bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!http_get_friends_task_stack) {
        ESP_LOGE(TAG,
                 "Failed to alloc http_get_friends stack (bytes=%u free=%u largest=%u)",
                 static_cast<unsigned>(bytes),
                 static_cast<unsigned>(heap_caps_get_free_size(
                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(
                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    }
    return http_get_friends_task_stack;
}

JsonDocument res;
volatile int res_flag = 0;
void http_get_message_task(void *pvParameters) {
    ESP_LOGW(TAG, "Start http_get_message_task");
    // Take ownership of heap arg and free after copy
    std::string chat_from = *(std::string *)pvParameters;  // friend identifier
    delete (std::string *)pvParameters;

    // Prepare API client and ensure logged in
    auto &api = dev_chat_api();
    api.set_scheme("https");
    const auto creds = chatapi::load_credentials_from_nvs();
    if (chatapi::ensure_authenticated(api, creds) != ESP_OK) {
        ESP_LOGW(TAG,
                 "Chat API auth failed; continuing with cached token state");
    }
    const std::string username = creds.username;

    std::string response;
    int status = 0;
    ESP_LOGI(TAG,
             "HTTP get_messages start (free=%u largest=%u stack_hwm=%u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                               MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    esp_err_t err = api.get_messages(chat_from, 20, response, &status);
    if (err == ESP_OK && status == 401) {
        ESP_LOGW(TAG, "get_messages unauthorized; refreshing token");
        if (chatapi::ensure_authenticated(api, creds, false,
                                          /*force_refresh=*/true) == ESP_OK) {
            response.clear();
            status = 0;
            err = api.get_messages(chat_from, 20, response, &status);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get_messages failed: %s (status=%d)",
                 esp_err_to_name(err), status);
        // Return empty messages to avoid indefinite Loading... UI
        StaticJsonDocument<128> emptyDoc;
        emptyDoc.createNestedArray("messages");
        std::string outBuf;
        serializeJson(emptyDoc, outBuf);
        deserializeJson(res, outBuf);
        res_flag = 1;
        http_get_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    // Transform server response into legacy shape used by Display
    // Server:
    // {"messages":[{"id","sender_id","receiver_id","content","is_read","created_at"},
    // ...]} Legacy expected: {"messages":[{"message":"...","from":"<friend or
    // me>"}, ...]}
    StaticJsonDocument<3072> in;
    if (deserializeJson(in, response) != DeserializationError::Ok) {
        ESP_LOGE(TAG, "JSON parse error");
        StaticJsonDocument<128> emptyDoc;
        emptyDoc.createNestedArray("messages");
        std::string outBuf;
        serializeJson(emptyDoc, outBuf);
        deserializeJson(res, outBuf);
        res_flag = 1;
        http_get_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "get_messages non-OK HTTP status=%d", status);
        StaticJsonDocument<128> emptyDoc;
        emptyDoc.createNestedArray("messages");
        std::string outBuf;
        serializeJson(emptyDoc, outBuf);
        deserializeJson(res, outBuf);
        res_flag = 1;
        http_get_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    StaticJsonDocument<3072> out;
    auto arr = out.createNestedArray("messages");
    std::string my_id = api.user_id();
    for (JsonObject m : in["messages"].as<JsonArray>()) {
        JsonObject o = arr.createNestedObject();
        o["message"] = m["content"].as<const char *>();
        const char *mid = m["id"].as<const char *>();
        if (!mid) mid = m["message_id"].as<const char *>();
        if (mid && mid[0] != '\0') o["id"] = mid;
        const char *created = m["created_at"].as<const char *>();
        if (created && created[0] != '\0') o["created_at"] = created;
        // Mark origin by comparing sender_id to self
        const char *sender = m["sender_id"].as<const char *>();
        if (sender && my_id.size() && my_id == sender) {
            o["from"] = username.c_str();
        } else {
            o["from"] = chat_from.c_str();
        }
        if (m.containsKey("is_read")) {
            o["is_read"] = m["is_read"].as<bool>();
        }
    }
    // Serialize to buffer then parse into global res to keep type compatibility
    std::string outBuf;
    serializeJson(out, outBuf);
    deserializeJson(res, outBuf);
    res_flag = 1;
    http_get_task_handle = nullptr;
    vTaskDelete(NULL);
}

static void http_get_friends_task(void *pvParameters) {
    (void)pvParameters;
    HttpFriendsTaskResult res;

    auto &api = dev_chat_api();
    api.set_scheme("https");
    const auto creds = chatapi::load_credentials_from_nvs();
    if (chatapi::ensure_authenticated(api, creds) != ESP_OK) {
        ESP_LOGW(TAG, "Friends fetch auth failed; attempting cached token");
    }

    res.err = api.get_friends(res.payload, &res.status);
    if (res.err == ESP_OK && res.status == 401) {
        if (chatapi::ensure_authenticated(api, creds, false,
                                          /*force_refresh=*/true) == ESP_OK) {
            res.payload.clear();
            res.status = 0;
            res.err = api.get_friends(res.payload, &res.status);
        }
    }

    http_get_friends_result = new HttpFriendsTaskResult(std::move(res));
    if (http_get_friends_waiter) {
        xTaskNotifyGive(http_get_friends_waiter);
    }
    http_get_friends_task_handle = nullptr;
    vTaskDelete(NULL);
}

static JsonDocument notif_res;
int notif_res_flag = 0;

void http_get_notifications_task(void *pvParameters) {
    // Ensure Wi‑Fi is connected before starting MQTT
    {
        wifi_ap_record_t ap;
        const int max_wait_ms = 20000;
        int waited = 0;
        while (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            waited += 100;
            if (waited >= max_wait_ms) break;
        }
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi not connected; postponing MQTT start");
            // keep waiting a bit more to avoid tight loop
            while (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
                vTaskDelay(500 / portTICK_PERIOD_MS);
            }
        }
    }
    // Initialize MQTT and subscribe to user topic (via runtime)
    auto &api = chatapi::shared_client(true);  // use NVS-configured endpoint
    api.set_scheme("https");
    const auto creds = chatapi::load_credentials_from_nvs();
    if (chatapi::ensure_authenticated(api, creds) != ESP_OK) {
        ESP_LOGW(TAG,
                 "Chat API auth failed; continuing with cached token state");
    }

    auto api_user_id = api.user_id();
    auto api_host = api.host();

    // Choose MQTT host/port
    std::string mqtt_host = get_nvs("mqtt_host");
    if (mqtt_host.empty()) mqtt_host = api_host;
    std::string mqtt_port_str = get_nvs((char *)"mqtt_port");
    int mqtt_port = 1883;
    if (!mqtt_port_str.empty()) {
        int p = atoi(mqtt_port_str.c_str());
        if (p > 0) mqtt_port = p;
    }
    mqtt_rt_configure(mqtt_host.c_str(), mqtt_port, api_user_id.c_str());
    if (mqtt_rt_start() != 0) {
        ESP_LOGE(TAG, "MQTT connect failed");
        notifications_task_running_flag().store(false);
        vTaskDelete(NULL);
        return;
    }
    if (api_user_id.empty()) {
        ESP_LOGE(TAG, "No user_id for MQTT subscribe");
        notifications_task_running_flag().store(false);
        vTaskDelete(NULL);
        return;
    }
    mqtt_rt_update_user(api_user_id.c_str());

    // Pump incoming messages into legacy notif_res format
    while (1) {
        char buf[1024];
        if (mqtt_rt_pop_message(buf, sizeof(buf))) {
            StaticJsonDocument<1024> out;
            auto arr = out.createNestedArray("notifications");
            JsonObject o = arr.createNestedObject();
            o["notification_flag"] = "true";
            o["raw"] = buf;

            std::string outBuf;
            serializeJson(out, outBuf);
            deserializeJson(notif_res, outBuf);
            notif_res_flag = 1;
            notification_effects::signal_new_message();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    notifications_task_running_flag().store(false);
    vTaskDelete(NULL);
}

struct HttpPostMessageArgs {
    std::string chat_to;
    std::string message;
};

static TaskHandle_t http_post_task_handle = nullptr;
static constexpr uint32_t kHttpPostTaskStackWords = 8192;

static void send_message_via_api(const std::string &chat_to,
                                 const std::string &message) {
    auto &api = chatapi::shared_client(true);
    api.set_scheme("https");
    const auto creds = chatapi::load_credentials_from_nvs();
    if (chatapi::ensure_authenticated(api, creds) != ESP_OK) {
        ESP_LOGW(TAG,
                 "Chat API auth failed; continuing with cached token state");
    }

    std::string dummy;
    esp_err_t err = api.send_message(chat_to, message, &dummy);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "send_message failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Message sent to %s", chat_to.c_str());
    }
}

void http_post_message_task(void *pvParameters) {
    auto *args = static_cast<HttpPostMessageArgs *>(pvParameters);
    if (!args) {
        ESP_LOGE(TAG, "http_post_message_task received null args");
        http_post_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }
    HttpPostMessageArgs data = std::move(*args);
    delete args;
    send_message_via_api(data.chat_to, data.message);
    http_post_task_handle = nullptr;
    vTaskDelete(NULL);
}

class HttpClient {
   public:
    static HttpClient &shared() {
        static HttpClient instance;
        return instance;
    }

    struct FriendsResponse {
        esp_err_t err = ESP_FAIL;
        int status = 0;
        std::string payload;
    };

    bool notif_flag = false;

    HttpClient(void) {
        static bool s_nvs_ok = false;
        if (!s_nvs_ok) {
            esp_err_t ret = nvs_flash_init();
            if (ret == ESP_OK) {
                s_nvs_ok = true;
            } else {
                ESP_LOGW(TAG, "nvs_flash_init failed: %s",
                         esp_err_to_name(ret));
            }
        }
        static bool s_netif_ok = false;
        if (!s_netif_ok) {
            esp_err_t e = esp_netif_init();
            if (e == ESP_OK) {
                s_netif_ok = true;
            } else {
                ESP_LOGW(TAG, "esp_netif_init failed: %s", esp_err_to_name(e));
            }
        }
        // ESP_ERROR_CHECK(esp_event_loop_create_default());

        /* This helper function configures Wi-Fi or Ethernet, as selected in
         * menuconfig. Read "Establishing Wi-Fi or Ethernet Connection" section
         * in examples/protocols/README.md for more information about this
         * function.
         */
        // ESP_ERROR_CHECK(example_connect());
        ESP_LOGI(TAG, "Connected to AP, begin http example");
    }

    void post_message(const std::string &chat_to,
                      const std::string &message) {
        auto *args = new HttpPostMessageArgs{chat_to, message};
        if (http_post_task_handle) {
            ESP_LOGW(TAG, "http_post_message_task already running");
            delete args;
            return;
        }
        BaseType_t ok = xTaskCreatePinnedToCore(
            &http_post_message_task, "http_post_message_task",
            kHttpPostTaskStackWords, args, 5, &http_post_task_handle, 1);
        if (ok != pdPASS || http_post_task_handle == nullptr) {
            ESP_LOGE(TAG,
                     "Failed to create http_post_message_task (stack_words=%u ok=%ld)",
                     kHttpPostTaskStackWords, static_cast<long>(ok));
            delete args;
            http_post_task_handle = nullptr;
            send_message_via_api(chat_to, message);
        }
    }

    JsonDocument get_message(std::string chat_from) {
        // Launch task with heap-allocated argument to avoid dangling pointer
        auto *arg = new std::string(chat_from);

        ESP_LOGI(TAG, "Start get message!");
        res_flag = 0;
        if (http_get_task_handle != nullptr) {
            ESP_LOGW(TAG, "Previous http_get_message_task still running; "
                           "skipping new request");
            delete arg;
            StaticJsonDocument<128> emptyDoc;
            emptyDoc.createNestedArray("messages");
            std::string outBuf;
            serializeJson(emptyDoc, outBuf);
            deserializeJson(res, outBuf);
            return res;
        }
        res_flag = 0;
        if (!ensure_http_get_stack()) {
            ESP_LOGE(TAG, "http_get_message_task: stack alloc failed");
            delete arg;
            StaticJsonDocument<128> emptyDoc;
            emptyDoc.createNestedArray("messages");
            std::string outBuf;
            serializeJson(emptyDoc, outBuf);
            deserializeJson(res, outBuf);
            return res;
        }
        http_get_task_handle = xTaskCreateStaticPinnedToCore(
            &http_get_message_task, "http_get_message_task",
            kHttpGetTaskStackWords, arg, 5, http_get_task_stack,
            &http_get_task_buffer, tskNO_AFFINITY);
        if (!http_get_task_handle) {
            ESP_LOGE(TAG, "Task create failed! free_heap=%u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
            delete arg;
            StaticJsonDocument<128> emptyDoc;
            emptyDoc.createNestedArray("messages");
            std::string outBuf;
            serializeJson(emptyDoc, outBuf);
            deserializeJson(res, outBuf);
            return res;
        }
        // Wait with timeout to avoid indefinite freeze
        const int timeout_ms = 7000;
        int waited = 0;
        while (!res_flag && waited < timeout_ms) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            waited += 10;
        }
        if (!res_flag) {
            // Timeout: return empty messages to let UI proceed
            StaticJsonDocument<128> emptyDoc;
            emptyDoc.createNestedArray("messages");
            std::string outBuf;
            serializeJson(emptyDoc, outBuf);
            deserializeJson(res, outBuf);
        }
        res_flag = 0;
        return res;
    }

    bool mark_message_read(const std::string &message_id) {
        (void)message_id;
        return false;
    }

    FriendsResponse fetch_friends_blocking(uint32_t timeout_ms = 12000) {
        FriendsResponse out;
        if (http_get_friends_task_handle) {
            out.err = ESP_ERR_INVALID_STATE;
            return out;
        }
        if (!ensure_http_friends_stack()) {
            out.err = ESP_ERR_NO_MEM;
            return out;
        }
        http_get_friends_result = nullptr;
        http_get_friends_waiter = xTaskGetCurrentTaskHandle();
        http_get_friends_task_handle = xTaskCreateStaticPinnedToCore(
            &http_get_friends_task, "http_get_friends_task",
            kHttpFriendsTaskStackWords, nullptr, 5, http_get_friends_task_stack,
            &http_get_friends_task_buffer, tskNO_AFFINITY);
        if (!http_get_friends_task_handle) {
            http_get_friends_waiter = nullptr;
            out.err = ESP_ERR_NO_MEM;
            return out;
        }

        uint32_t wait_ticks =
            timeout_ms ? pdMS_TO_TICKS(timeout_ms) : portMAX_DELAY;
        if (ulTaskNotifyTake(pdTRUE, wait_ticks) == 0) {
            http_get_friends_waiter = nullptr;
            if (http_get_friends_result) {
                out.err = http_get_friends_result->err;
                out.status = http_get_friends_result->status;
                out.payload = std::move(http_get_friends_result->payload);
                delete http_get_friends_result;
                http_get_friends_result = nullptr;
            } else {
                out.err = ESP_ERR_TIMEOUT;
            }
            return out;
        }
        http_get_friends_waiter = nullptr;
        if (http_get_friends_result) {
            out.err = http_get_friends_result->err;
            out.status = http_get_friends_result->status;
            out.payload = std::move(http_get_friends_result->payload);
            delete http_get_friends_result;
            http_get_friends_result = nullptr;
        } else {
            out.err = ESP_FAIL;
        }
        return out;
    }

    void start_notifications() {
        auto &running = notifications_task_running_flag();
        bool expected = false;
        if (!running.compare_exchange_strong(expected, true)) {
            return;
        }

        TaskHandle_t handle = nullptr;
        BaseType_t ok = xTaskCreatePinnedToCore(&http_get_notifications_task,
                                                "http_get_notifications_task",
                                                6000, NULL, 5, &handle, 0);
        if (ok != pdPASS || handle == nullptr) {
            ESP_LOGE(TAG, "Failed to start notification task (err=%ld)",
                     static_cast<long>(ok));
            running.store(false);
        }
    }

    static JsonDocument get_notifications() {
        if (!notif_res_flag) {
            JsonDocument doc;
            JsonArray data = doc.createNestedArray("notifications");
            return doc;
        }

        notif_res_flag = 0;
        return notif_res;
    }
};
