
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


#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "sdkconfig.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_tls.h"

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "esp_http_client.h"

/* Constants that aren't configurable in menuconfig */
// #define WEB_SERVER "mimoc.tech"

#define USER_NAME "mu"
#define TO_USER_NAME "mimoc"
#define TOKEN "4321"

// #define WEB_SERVER "mimoc.tech"
#define WEB_SERVER "192.168.10.108"
// #define WEB_SERVER "raspberrypi.local"

#define WEB_PORT "3001"
#define WEB_PATH "/getUserName"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 1024

#pragma once

static const char *GET_REQUEST = "GET " WEB_PATH " HTTP/1.0\r\n"
    "Host: "WEB_SERVER":"WEB_PORT"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";

static const char *POST_REQUEST = "POST /sendMessage HTTP/1.0\r\n"
    "Host: "WEB_SERVER":"WEB_PORT"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
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
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
				free(output_buffer);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
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
    return ESP_OK;
}

static void http_get_task(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];

    while(1) {
        int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        /* Code to print the resolved IP.

           Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);

        if (write(s, GET_REQUEST, strlen(GET_REQUEST)) < 0) {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... socket send success");

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... set socket receiving timeout success");

        /* Read HTTP response */
        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            for(int i = 0; i < r; i++) {
                putchar(recv_buf[i]);
            }
			printf("\n");
        } while(r > 0);

        ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d.", r, errno);
        close(s);
        for(int countdown = 10; countdown >= 0; countdown--) {
            ESP_LOGI(TAG, "%d... ", countdown);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        ESP_LOGI(TAG, "Starting again!");
    }
}

static void http_post_task(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];

    while(1) {
        int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        /* Code to print the resolved IP.

           Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);

        if (write(s, POST_REQUEST, strlen(POST_REQUEST)) < 0) {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... socket send success");

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... set socket receiving timeout success");

        /* Read HTTP response */
        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            for(int i = 0; i < r; i++) {
                putchar(recv_buf[i]);
            }
			printf("\n");
        } while(r > 0);

        ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d.", r, errno);
        close(s);
        for(int countdown = 10; countdown >= 0; countdown--) {
            ESP_LOGI(TAG, "%d... ", countdown);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        ESP_LOGI(TAG, "Starting again!");
    }
}


class HttpClient {
	
	public:
		static int new_message_id;
		static bool notif_flag;
		static cJSON *messages;
	
		void post_message(std::string message = ""){
			xTaskCreate(&http_post_native_task, "http_post_native_task", 4096, &message, 5, NULL);
			// xTaskCreate(&http_post_task, "http_post_task", 4096, NULL, 5, NULL);
			// xTaskCreate(&http_get_task, "http_get_task", 4096, NULL, 5, NULL);
		}

		void start_receiving_wait(){
			printf("Start receiving wait...");
			xTaskCreate(&receiving_wait, "receiving_wait", 5124, NULL, 5, NULL);
		}
	
	private:
		
		static void http_post_native_task(void *pvParameters){

			char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};
			int content_length = 0;

			std::string message;
			message = *(std::string *)pvParameters;

			esp_http_client_config_t config = {
				.url = "http://" WEB_SERVER ":" WEB_PORT "/sendMessage",
				.event_handler = _http_event_handler,
				.user_data = output_buffer,
			};

			esp_http_client_handle_t client = esp_http_client_init(&config);

			// POST
			// const char *post_data = "{\"message\":\"Hello From Mobus!\"}";
			const std::string post_data_str = "{\"message\":\"" + message + "\",\"from\":\"" + USER_NAME + "\",\"to\":\"" + TO_USER_NAME +  "\",\"token\":\"" + TOKEN +  "\"}";
			const char *post_data = post_data_str.c_str();
			
			esp_http_client_set_method(client, HTTP_METHOD_POST);
			esp_http_client_set_header(client, "Content-Type", "application/json");
			esp_http_client_set_post_field(client, post_data, strlen(post_data));

			esp_err_t err = esp_http_client_perform(client);
			
			if (err == ESP_OK) {
				ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
						esp_http_client_get_status_code(client),
						esp_http_client_get_content_length(client));
			} else {
				ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
			}
			ESP_LOGI(TAG, "%s", output_buffer);

			cJSON *root = cJSON_Parse(output_buffer);
			
			std::string from = cJSON_GetObjectItem(root,"from")->valuestring;
			std::string send_message = cJSON_GetObjectItem(root,"message")->valuestring;
			
			printf("FROM:%s\n",from.c_str());
			printf("MESSAGE:%s\n",send_message.c_str());

			cJSON_Delete(root);
			esp_http_client_cleanup(client);
			vTaskDelete(NULL);
		}

		static void receiving_wait(void *pvParameters){
			
			char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};
			int content_length = 0;

			esp_http_client_config_t config = {
				.url = "http://" WEB_SERVER ":" WEB_PORT "/getMessage",
				.event_handler = _http_event_handler,
				.user_data = output_buffer,
			};

			esp_http_client_handle_t client = esp_http_client_init(&config);
			
			while (1) {
				
				// 5秒おきに受信を確認する
				vTaskDelay(5000 / portTICK_PERIOD_MS);

				printf("NEW_MESSAGE_ID:%d\n",new_message_id);
				
				std::string message_id = std::to_string(new_message_id);
				
				// POST
				std::string post_data_str = "{\"id\":\"" + message_id + "\",\"to\":\"" + USER_NAME + "\",\"token\":\"" + TOKEN + "\"}";
				const char *post_data = post_data_str.c_str();
				
				esp_http_client_set_method(client, HTTP_METHOD_POST);
				esp_http_client_set_header(client, "Content-Type", "application/json");
				esp_http_client_set_post_field(client, post_data, strlen(post_data));
	
				esp_err_t err = esp_http_client_perform(client);
				
				if (err == ESP_OK) {
					ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
							esp_http_client_get_status_code(client),
							esp_http_client_get_content_length(client));
				} else {
					ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
				}

				ESP_LOGI(TAG, "%s", output_buffer);

				std::string output_buffer_string(output_buffer, 1024);
				std::string null_string = "null";
				int null_found = output_buffer_string.find(null_string);

				if (null_found == 0){
					printf("No New Arrivals Message: response is null");
					continue;
				}
				
				cJSON *root = cJSON_Parse(output_buffer);
				cJSON *messages_tmp = NULL;
				cJSON *message = NULL;

				if (root == NULL){
					printf("No New Arrivals Message: Json is null");
					continue;
				}
				
				messages_tmp = cJSON_GetObjectItemCaseSensitive(root, "messages");
				
				if (cJSON_GetArraySize(messages_tmp) == 0){
					continue;
				}
				
				messages = messages_tmp;
				cJSON_ArrayForEach(message, messages)
				{		
					int message_id = cJSON_GetObjectItem(message,"ID")->valuedouble;
					// printf("MESSAGE_ID:%d\n",message_id);

					// std::string message_from = cJSON_GetObjectItem(message,"MessageFrom")->valuestring;
					// printf("MESSAGE_FROM:%s\n",message_from.c_str());

					// std::string message_to = cJSON_GetObjectItem(message,"MessageTo")->valuestring;
					// printf("MESSAGE_TO:%s\n",message_to.c_str());

					// std::string message_s = cJSON_GetObjectItem(message,"Message")->valuestring;
					// printf("MESSAGE:%s\n",message_s.c_str());

					if (message_id > new_message_id){
						new_message_id = message_id;
						notif_flag = true;
					}
				}
								
			}

			esp_http_client_cleanup(client);
			vTaskDelete(NULL);
		}
};

int HttpClient::new_message_id = 50;
cJSON* HttpClient::messages = NULL;
bool HttpClient::notif_flag = false;

