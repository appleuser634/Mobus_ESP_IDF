// MQTT runtime singleton to allow pause/resume around BLE to save memory

#include <string>
#include <queue>
#include <mutex>
#include <vector>
#include <deque>
#include <algorithm>
#include <cstring>

#include "esp_log.h"
#include "mqtt_client.h"

#include "mqtt_runtime.h"

static const char* TAG = "MQTT_RT";

namespace {
struct State {
    std::mutex m;
    std::string host;
    int port = 1883;
    std::string user_id;
    std::string topic; // chat/messages/<user_id>
    std::string uri;
    esp_mqtt_client_handle_t client = nullptr;
    bool connected = false;
    std::queue<std::string> queue;
    int next_listener_id = 1;
    struct Listener {
        int id = 0;
        std::string topic;
        std::deque<std::string> queue;
        bool active = true;
    };
    std::vector<Listener> listeners;
} S;

static void on_event(void* handler_args, esp_event_base_t, int32_t event_id, void* event_data)
{
    auto* ev = static_cast<esp_mqtt_event_handle_t>(event_data);
    if (!ev) return;
    if (event_id == MQTT_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "MQTT connected (uri=%s)", S.uri.c_str());
        S.connected = true;
        if (!S.topic.empty()) {
            esp_mqtt_client_subscribe(S.client, S.topic.c_str(), 1);
        }
        std::lock_guard<std::mutex> lk(S.m);
        for (auto &listener : S.listeners) {
            if (!listener.topic.empty()) {
                esp_mqtt_client_subscribe(S.client, listener.topic.c_str(), 1);
            }
        }
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "MQTT disconnected");
        S.connected = false;
    } else if (event_id == MQTT_EVENT_ERROR) {
        if (ev->error_handle) {
            ESP_LOGE(
                TAG,
                "MQTT error: type=%d tls_esp=0x%x tls_stack=0x%x cert=0x%x "
                "conn_rc=%d sock_errno=%d",
                static_cast<int>(ev->error_handle->error_type),
                static_cast<unsigned>(ev->error_handle->esp_tls_last_esp_err),
                static_cast<unsigned>(ev->error_handle->esp_tls_stack_err),
                static_cast<unsigned>(ev->error_handle->esp_tls_cert_verify_flags),
                static_cast<int>(ev->error_handle->connect_return_code),
                static_cast<int>(ev->error_handle->esp_transport_sock_errno));
        } else {
            ESP_LOGE(TAG, "MQTT error event without error_handle");
        }
    } else if (event_id == MQTT_EVENT_DATA) {
        std::string payload(ev->data, ev->data_len);
        std::string topic;
        if (ev->topic && ev->topic_len > 0) {
            topic.assign(ev->topic, ev->topic_len);
        }
        bool matched = false;
        {
            std::lock_guard<std::mutex> lk(S.m);
            if (!S.topic.empty() && topic == S.topic) {
                S.queue.push(payload);
                matched = true;
            }
            if (!matched) {
                for (auto &listener : S.listeners) {
                    if (listener.active && !listener.topic.empty() &&
                        listener.topic == topic) {
                        listener.queue.push_back(payload);
                        matched = true;
                        break;
                    }
                }
            }
        }
        (void)matched;
    }
}
} // namespace

int mqtt_rt_configure(const char* host, int port, const char* user_id)
{
    if (!host || !*host) return -1;
    S.host = host;
    if (port > 0) S.port = port; else S.port = 1883;
    if (user_id && *user_id) {
        S.user_id = user_id;
        S.topic = std::string("chat/messages/") + S.user_id;
    }
    return 0;
}

int mqtt_rt_start(void)
{
    if (S.client) return 0; // already started
    if (S.host.empty()) return -1;
    S.uri = std::string("mqtt://") + S.host + ":" + std::to_string(S.port);
    ESP_LOGI(TAG, "MQTT start: host=%s port=%d user_id=%s uri=%s",
             S.host.c_str(), S.port, S.user_id.c_str(), S.uri.c_str());
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = S.uri.c_str();
    cfg.network.disable_auto_reconnect = false;
    cfg.session.keepalive = 30;
    cfg.task.stack_size = 4096;
    S.client = esp_mqtt_client_init(&cfg);
    if (!S.client) return -2;
    esp_mqtt_client_register_event(S.client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, &on_event, nullptr);
    esp_err_t err = esp_mqtt_client_start(S.client);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(S.client);
        S.client = nullptr;
        return -3;
    }
    return 0;
}

void mqtt_rt_stop(void)
{
    if (S.client) {
        esp_mqtt_client_stop(S.client);
        esp_mqtt_client_destroy(S.client);
        S.client = nullptr;
    }
    S.connected = false;
}

int mqtt_rt_resume(void)
{
    return mqtt_rt_start();
}

int mqtt_rt_update_user(const char* user_id)
{
    if (!user_id || !*user_id) return -1;
    S.user_id = user_id;
    S.topic = std::string("chat/messages/") + S.user_id;
    if (S.connected && S.client) {
        esp_mqtt_client_subscribe(S.client, S.topic.c_str(), 1);
    }
    return 0;
}

bool mqtt_rt_pop_message(char* out_json, size_t out_cap)
{
    if (!out_json || out_cap == 0) return false;
    std::lock_guard<std::mutex> lk(S.m);
    if (S.queue.empty()) return false;
    std::string s = std::move(S.queue.front()); S.queue.pop();
    size_t n = s.size(); if (n >= out_cap) n = out_cap - 1;
    memcpy(out_json, s.data(), n);
    out_json[n] = 0;
    return true;
}

namespace {
State::Listener* find_listener(int id)
{
    for (auto &listener : S.listeners) {
        if (listener.id == id) return &listener;
    }
    return nullptr;
}
}

int mqtt_rt_add_listener(const char* topic)
{
    if (!topic || !*topic) return -1;
    std::string topic_str(topic);
    int id;
    std::string subscribe_topic;
    {
        std::lock_guard<std::mutex> lk(S.m);
        id = S.next_listener_id++;
        State::Listener listener;
        listener.id = id;
        listener.topic = std::move(topic_str);
        listener.active = true;
        subscribe_topic = listener.topic;
        S.listeners.push_back(std::move(listener));
    }
    if (!subscribe_topic.empty() && S.client && S.connected) {
        esp_mqtt_client_subscribe(S.client, subscribe_topic.c_str(), 1);
    }
    return id;
}

void mqtt_rt_remove_listener(int listener_id)
{
    if (listener_id <= 0) return;
    std::string topic;
    {
        std::lock_guard<std::mutex> lk(S.m);
        auto it = std::remove_if(S.listeners.begin(), S.listeners.end(),
                                 [&](const State::Listener& l) {
                                     if (l.id == listener_id) {
                                         topic = l.topic;
                                         return true;
                                     }
                                     return false;
                                 });
        if (it != S.listeners.end()) {
            S.listeners.erase(it, S.listeners.end());
        }
    }
    if (!topic.empty() && S.client && S.connected) {
        esp_mqtt_client_unsubscribe(S.client, topic.c_str());
    }
}

bool mqtt_rt_listener_pop(int listener_id, char* out_json, size_t out_cap)
{
    if (listener_id <= 0 || !out_json || out_cap == 0) return false;
    std::lock_guard<std::mutex> lk(S.m);
    auto* listener = find_listener(listener_id);
    if (!listener || listener->queue.empty()) return false;
    std::string payload = std::move(listener->queue.front());
    listener->queue.pop_front();
    size_t n = payload.size();
    if (n >= out_cap) n = out_cap - 1;
    memcpy(out_json, payload.data(), n);
    out_json[n] = 0;
    return true;
}

int mqtt_rt_publish(const char* topic, const char* payload, int qos, bool retain)
{
    if (!topic || !payload || !S.client) return -1;
    return esp_mqtt_client_publish(S.client, topic, payload, 0, qos, retain);
}

bool mqtt_rt_is_running(void)
{
    return S.client != nullptr;
}
