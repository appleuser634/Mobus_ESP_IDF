// Minimal MQTT client for ESP-IDF to receive chat notifications
// Subscribes to topic: chat/messages/<user_id>

#pragma once

#include <string>
#include <queue>
#include <mutex>

#include "esp_log.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace chatmqtt {

static const char* TAG_MQTT = "ChatMQTT";

class MQTTClient {
public:
    MQTTClient() = default;
    ~MQTTClient() { stop(); }

    // host: e.g. "localhost" or "mimoc.jp"
    // port: typically 1883
    esp_err_t start(const std::string& host, int port = 1883) {
        if (client_) stop();

        // Build URI like mqtt://host:port
        uri_ = "mqtt://" + host + ":" + std::to_string(port);

        esp_mqtt_client_config_t cfg = {};
        // ESP-IDF v5+ style nested config
        cfg.broker.address.uri = uri_.c_str();
        cfg.network.disable_auto_reconnect = false;
        cfg.session.keepalive = 30;
        cfg.task.stack_size = 4096;

        client_ = esp_mqtt_client_init(&cfg);
        if (!client_) return ESP_FAIL;
        esp_mqtt_client_register_event(client_, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, &MQTTClient::event_handler_static, this);
        auto err = esp_mqtt_client_start(client_);
        if (err == ESP_OK) connected_ = true; else connected_ = false;
        return err;
    }

    void stop() {
        if (client_) {
            esp_mqtt_client_stop(client_);
            esp_mqtt_client_destroy(client_);
            client_ = nullptr;
        }
        connected_ = false;
    }

    bool is_connected() const { return connected_; }

    // topic: chat/messages/<user_id>
    esp_err_t subscribe_user(const std::string& user_id) {
        if (!client_) return ESP_ERR_INVALID_STATE;
        topic_ = "chat/messages/" + user_id;
        int mid = esp_mqtt_client_subscribe(client_, topic_.c_str(), 1);
        return (mid >= 0) ? ESP_OK : ESP_FAIL;
    }

    esp_err_t unsubscribe_user() {
        if (!client_ || topic_.empty()) return ESP_ERR_INVALID_STATE;
        int mid = esp_mqtt_client_unsubscribe(client_, topic_.c_str());
        topic_.clear();
        return (mid >= 0) ? ESP_OK : ESP_FAIL;
    }

    // Returns true if a message was available and filled into out_json
    bool pop_message(std::string& out_json) {
        std::lock_guard<std::mutex> lock(m_);
        if (queue_.empty()) return false;
        out_json = std::move(queue_.front());
        queue_.pop();
        return true;
    }

private:
    static void event_handler_static(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
        MQTTClient* self = static_cast<MQTTClient*>(handler_args);
        self->event_handler(base, event_id, event_data);
    }

    void event_handler(esp_event_base_t base, int32_t event_id, void* event_data) {
        auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
        switch (event->event_id) {
            case MQTT_EVENT_CONNECTED:
                ESP_LOGI(TAG_MQTT, "Connected to broker");
                connected_ = true;
                // Re-subscribe if topic already set
                if (!topic_.empty()) esp_mqtt_client_subscribe(client_, topic_.c_str(), 1);
                break;
            case MQTT_EVENT_DISCONNECTED:
                ESP_LOGW(TAG_MQTT, "Disconnected from broker");
                connected_ = false;
                break;
            case MQTT_EVENT_DATA: {
                // Copy payload into queue as JSON string
                std::string payload(event->data, event->data_len);
                {
                    std::lock_guard<std::mutex> lock(m_);
                    queue_.push(payload);
                }
                ESP_LOGI(TAG_MQTT, "Message on %.*s: %.*s", event->topic_len, event->topic, event->data_len, event->data);
                break;
            }
            default:
                break;
        }
    }

    esp_mqtt_client_handle_t client_{nullptr};
    bool connected_{false};
    std::string uri_;
    std::string topic_;
    std::queue<std::string> queue_;
    std::mutex m_;
};

} // namespace chatmqtt
