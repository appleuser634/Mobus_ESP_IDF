// MQTT runtime singleton to allow pause/resume around BLE to save memory

#include <string>
#include <queue>
#include <mutex>

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
    esp_mqtt_client_handle_t client = nullptr;
    bool connected = false;
    std::queue<std::string> queue;
} S;

static void on_event(void* handler_args, esp_event_base_t, int32_t event_id, void* event_data)
{
    auto* ev = static_cast<esp_mqtt_event_handle_t>(event_data);
    if (!ev) return;
    if (event_id == MQTT_EVENT_CONNECTED) {
        S.connected = true;
        if (!S.topic.empty()) {
            esp_mqtt_client_subscribe(S.client, S.topic.c_str(), 1);
        }
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        S.connected = false;
    } else if (event_id == MQTT_EVENT_DATA) {
        std::string payload(ev->data, ev->data_len);
        std::lock_guard<std::mutex> lk(S.m);
        S.queue.push(payload);
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
    std::string uri = std::string("mqtt://") + S.host + ":" + std::to_string(S.port);
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = uri.c_str();
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

bool mqtt_rt_is_running(void)
{
    return S.client != nullptr;
}

