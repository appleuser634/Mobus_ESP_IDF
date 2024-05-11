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

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048
#define HTTP_ENDPOINT "192.168.2.100"

/* Root cert for howsmyssl.com, taken from howsmyssl_com_root_cert.pem

   The PEM file was extracted from the output of this command:
   openssl s_client -showcerts -connect www.howsmyssl.com:443 </dev/null

   The CA root cert is the last cert given in the chain of certs.

   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
extern const char howsmyssl_com_root_cert_pem_start[] asm("_binary_howsmyssl_com_root_cert_pem_start");
extern const char howsmyssl_com_root_cert_pem_end[]   asm("_binary_howsmyssl_com_root_cert_pem_end");

extern const char postman_root_cert_pem_start[] asm("_binary_postman_root_cert_pem_start");
extern const char postman_root_cert_pem_end[]   asm("_binary_postman_root_cert_pem_end");

esp_err_t _http_client_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
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
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // Clean the buffer in case of a new request
            if (output_len == 0 && evt->user_data) {
                // we are just starting to copy the output data into the use
                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
            }
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                int copy_len = 0;
                if (evt->user_data) {
                    // The last byte in evt->user_data is kept for the NULL character in case of out-of-bound access.
                    copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                } else {
                    int content_len = esp_http_client_get_content_length(evt->client);
                    if (output_buffer == NULL) {
                        // We initialize output_buffer with 0 because it is used by strlen() and similar functions therefore should be null terminated.
                        output_buffer = (char *) calloc(content_len + 1, sizeof(char));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
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
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
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
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
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
void http_get_message_task(void *pvParameters)
{
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};

    esp_http_client_config_t config = {
        .host = HTTP_ENDPOINT,
        .port = 3000,
        .path = "/messages",
        .event_handler = _http_client_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .user_data = local_response_buffer,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    std::string chat_from = *(std::string *)pvParameters;

    // GET 
    esp_http_client_set_header(client, "chat_from", chat_from.c_str());
    esp_http_client_set_header(client, "chat_to", "Hazuki");
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
      ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %"PRId64,
              esp_http_client_get_status_code(client),
              esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    ESP_LOG_BUFFER_HEX(TAG, local_response_buffer, strlen(local_response_buffer));
    printf("///////////// buffer ////////////");
    printf(local_response_buffer);
    std::string str_res(local_response_buffer);
    const char* json = str_res.c_str(); // const char* へのポインタを取得

    deserializeJson(res, json);
    printf("///////////// res size: %d ////////////",res["messages"].size());
    printf(local_response_buffer);
    res_flag = 1;

    vTaskDelete(NULL);
}

static JsonDocument notif_res;
int notif_res_flag = 0;
void http_get_notifications_task(void *pvParameters)
{
  while (1) {
    printf("START NOTIFICATIONS TASK...\n");
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};

    esp_http_client_config_t config = {
        .host = HTTP_ENDPOINT,
        .port = 3000,
        .path = "/notifications",
        .event_handler = _http_client_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .user_data = local_response_buffer,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // POST
    JsonDocument doc;
    doc["to"] = "Hazuki";

    char post_data[255];
    serializeJson(doc, post_data, sizeof(post_data));

    esp_http_client_set_url(client, "/notifications");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %"PRId64,
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    ESP_LOG_BUFFER_HEX(TAG, local_response_buffer, strlen(local_response_buffer));
    printf("///////////// buffer ////////////");
    printf(local_response_buffer);
    std::string str_res(local_response_buffer);
    const char* json = str_res.c_str(); // const char* へのポインタを取得

    deserializeJson(notif_res, json);
    printf("///////////// res size: %d ////////////",notif_res["notifications"].size());
    printf(local_response_buffer);
    notif_res_flag = 1;

    esp_http_client_cleanup(client); 

    while (notif_res_flag) {
      vTaskDelay(10);
    }
  }
}

std::string chat_to = "";
std::string message = "";
void http_post_message_task(void *pvParameters)
{
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};

    esp_http_client_config_t config = {
        .host = HTTP_ENDPOINT,
        .port = 3000,
        .path = "/messages",
        .event_handler = _http_client_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .user_data = local_response_buffer,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // std::string* chat_to_data = static_cast<std::string*>(pvParameters);;
    // std::string chat_to_data[] = (std::string *)pvParameters;
    // std::string chat_to = chat_to_data[0];
    // std::string message = chat_to_data[1];
    //std::string message = *(std::string *)pvParameters;

    // POST
    JsonDocument doc;
    doc["message"] = message; 
    doc["from"] = "Hazuki";
    doc["to"] = "Musashi";

    char post_data[255];
    serializeJson(doc, post_data, sizeof(post_data));

    esp_http_client_set_url(client, "/messages");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %"PRId64,
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client); 
    vTaskDelete(NULL);
}

class HttpClient {
  public:

  bool notif_flag = false;

  HttpClient(void) {
      esp_err_t ret = nvs_flash_init();
      if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
      }
      ESP_ERROR_CHECK(ret);

      ESP_ERROR_CHECK(esp_netif_init());
      // ESP_ERROR_CHECK(esp_event_loop_create_default());

      /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
      * Read "Establishing Wi-Fi or Ethernet Connection" section in
      * examples/protocols/README.md for more information about this function.
      */
      // ESP_ERROR_CHECK(example_connect());
      ESP_LOGI(TAG, "Connected to AP, begin http example");

  }

  void post_message(const std::string* chat_to_data)
  {
			chat_to = chat_to_data[0];
			message = chat_to_data[1];
      xTaskCreatePinnedToCore(&http_post_message_task, "http_post_message_task", 8192, &chat_to_data, 5, NULL, 0);
  }
  
  JsonDocument get_message(std::string chat_from)
  {
    xTaskCreatePinnedToCore(&http_get_message_task, "http_get_message_task", 8192, &chat_from, 5, NULL, 0);
    while (!res_flag) {
      vTaskDelay(1);
    }
    res_flag = 0;
    return res;
  }
  
  void start_notifications()
  {
    xTaskCreatePinnedToCore(&http_get_notifications_task, "http_get_notifications_task", 6000, NULL, 5, NULL, 0);
  }

  static JsonDocument get_notifications()
  {
    if (!notif_res_flag){
      JsonDocument doc;
      JsonArray data = doc.createNestedArray("notifications");
      return doc;
    }

    notif_res_flag = 0;
    return notif_res;
  }
};
