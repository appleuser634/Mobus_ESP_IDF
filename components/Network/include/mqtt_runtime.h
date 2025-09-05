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

// Pop next message if available; returns true when a message is written to out_json.
bool mqtt_rt_pop_message(char* out_json, size_t out_cap);

// Is client currently running/connected (best-effort)?
bool mqtt_rt_is_running(void);

#ifdef __cplusplus
}
#endif

