// Simple global MQTT runtime controls to save memory when BLE is enabled
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

// Configure runtime (host, port, user_id for topic). Safe to call multiple times.
int mqtt_rt_configure(const char* host, int port, const char* user_id);

// Start client if configured and not running; returns 0 on success.
int mqtt_rt_start(void);

// Stop client (disconnect + destroy). Keeps last config.
void mqtt_rt_stop(void);

// Convenience: pause (alias of stop) and resume.
static inline void mqtt_rt_pause(void) { mqtt_rt_stop(); }
int mqtt_rt_resume(void);

// Update user for topic subscription; applies immediately if connected.
int mqtt_rt_update_user(const char* user_id);

// Pop next personal message (chat/messages/<user_id>) if available.
bool mqtt_rt_pop_message(char* out_json, size_t out_cap);

// Lightweight helpers for additional MQTT topics (e.g., open chat rooms).
int mqtt_rt_add_listener(const char* topic);
void mqtt_rt_remove_listener(int listener_id);
bool mqtt_rt_listener_pop(int listener_id, char* out_json, size_t out_cap);

int mqtt_rt_publish(const char* topic, const char* payload, int qos, bool retain);

// Is client currently running/connected (best-effort)?
bool mqtt_rt_is_running(void);

#ifdef __cplusplus
}
#endif
