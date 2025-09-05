/* ESP HTTP Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
#include <string>
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

#include "esp_http_client.h"

#include "chat_api.hpp"
#include "mqtt_client.hpp"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048
#define HTTP_ENDPOINT "192.168.2.184"

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

JsonDocument res;
int res_flag = 0;
void http_get_message_task(void *pvParameters) {
    std::string chat_from =
        *(std::string *)pvParameters;  // friend identifier (uuid or short_id)

    // Prepare API client
    chatapi::ChatApiClient api(HTTP_ENDPOINT, 8080);

    // Ensure logged in
    std::string username = get_nvs("user_name");
    std::string password = get_nvs("password");
    if (password.empty()) password = "password123";  // default for dev env
    if (api.token().empty()) {
        api.login(username, password);
    }

    std::string response;
    esp_err_t err = api.get_messages(chat_from, 20, response);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get_messages failed: %s", esp_err_to_name(err));
        // Return empty messages to avoid indefinite Loading... UI
        StaticJsonDocument<128> emptyDoc;
        emptyDoc.createNestedArray("messages");
        std::string outBuf;
        serializeJson(emptyDoc, outBuf);
        deserializeJson(res, outBuf);
        res_flag = 1;
        vTaskDelete(NULL);
        return;
    }

    // Transform server response into legacy shape used by Display
    // Server:
    // {"messages":[{"id","sender_id","receiver_id","content","is_read","created_at"},
    // ...]} Legacy expected: {"messages":[{"message":"...","from":"<friend or
    // me>"}, ...]}
    StaticJsonDocument<4096> in;
    if (deserializeJson(in, response) != DeserializationError::Ok) {
        ESP_LOGE(TAG, "JSON parse error");
        vTaskDelete(NULL);
        return;
    }
    StaticJsonDocument<4096> out;
    auto arr = out.createNestedArray("messages");
    std::string my_id = api.user_id();
    for (JsonObject m : in["messages"].as<JsonArray>()) {
        JsonObject o = arr.createNestedObject();
        o["message"] = m["content"].as<const char *>();
        // Mark origin by comparing sender_id to self
        const char *sender = m["sender_id"].as<const char *>();
        if (sender && my_id.size() && my_id == sender) {
            o["from"] = username.c_str();
        } else {
            o["from"] = chat_from.c_str();
        }
    }
    // Serialize to buffer then parse into global res to keep type compatibility
    std::string outBuf;
    serializeJson(out, outBuf);
    deserializeJson(res, outBuf);
    res_flag = 1;
    vTaskDelete(NULL);
}

static JsonDocument notif_res;
int notif_res_flag = 0;
static chatmqtt::MQTTClient g_mqtt;  // single instance

void http_get_notifications_task(void *pvParameters) {
    // Initialize MQTT and subscribe to user topic
    chatapi::ChatApiClient api(HTTP_ENDPOINT, 8080);
    std::string username = get_nvs("user_name");
    std::string password = get_nvs("password");
    if (password.empty()) password = "password123";
    if (api.token().empty()) api.login(username, password);

    // Choose MQTT host/port
    std::string mqtt_host = get_nvs("mqtt_host");
    if (mqtt_host.empty()) mqtt_host = api.host();
    std::string mqtt_port_str = get_nvs((char*)"mqtt_port");
    int mqtt_port = 1883;
    if (!mqtt_port_str.empty()) { int p = atoi(mqtt_port_str.c_str()); if (p>0) mqtt_port = p; }
    if (g_mqtt.start(mqtt_host, mqtt_port) != ESP_OK) {
        ESP_LOGE(TAG, "MQTT connect failed");
        vTaskDelete(NULL);
        return;
    }
    if (api.user_id().empty()) {
        ESP_LOGE(TAG, "No user_id for MQTT subscribe");
        vTaskDelete(NULL);
        return;
    }
    g_mqtt.subscribe_user(api.user_id());

    // Pump incoming messages into legacy notif_res format
    while (1) {
        std::string msg;
        if (g_mqtt.pop_message(msg)) {
            StaticJsonDocument<1024> out;
            auto arr = out.createNestedArray("notifications");
            JsonObject o = arr.createNestedObject();
            o["notification_flag"] = "true";
            o["raw"] = msg.c_str();

            std::string outBuf;
            serializeJson(out, outBuf);
            deserializeJson(notif_res, outBuf);
            notif_res_flag = 1;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

std::string chat_to = "";
std::string message = "";
void http_post_message_task(void *pvParameters) {
    // Prepare API client and send message using new /api/messages/send
    chatapi::ChatApiClient api(HTTP_ENDPOINT, 8080);
    std::string username = get_nvs("user_name");
    std::string password = get_nvs("password");
    if (password.empty()) password = "password123";
    if (api.token().empty()) api.login(username, password);

    std::string dummy;
    esp_err_t err = api.send_message(chat_to, message, &dummy);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "send_message failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Message sent to %s", chat_to.c_str());
    }
    vTaskDelete(NULL);
}

class HttpClient {
   public:
    bool notif_flag = false;

    HttpClient(void) {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
            ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        ESP_ERROR_CHECK(esp_netif_init());
        // ESP_ERROR_CHECK(esp_event_loop_create_default());

        /* This helper function configures Wi-Fi or Ethernet, as selected in
         * menuconfig. Read "Establishing Wi-Fi or Ethernet Connection" section
         * in examples/protocols/README.md for more information about this
         * function.
         */
        // ESP_ERROR_CHECK(example_connect());
        ESP_LOGI(TAG, "Connected to AP, begin http example");
    }

    void post_message(const std::string *chat_to_data) {
        chat_to = chat_to_data[0];
        message = chat_to_data[1];
        xTaskCreatePinnedToCore(&http_post_message_task,
                                "http_post_message_task", 8192, &chat_to_data,
                                5, NULL, 0);
    }

    JsonDocument get_message(std::string chat_from) {
        xTaskCreatePinnedToCore(&http_get_message_task, "http_get_message_task",
                                8192, &chat_from, 5, NULL, 0);
        while (!res_flag) {
            vTaskDelay(1);
        }
        res_flag = 0;
        return res;
    }

    void start_notifications() {
        xTaskCreatePinnedToCore(&http_get_notifications_task,
                                "http_get_notifications_task", 6000, NULL, 5,
                                NULL, 0);
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
