#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// Weak definition; can be overridden by a strong definition (e.g. in main)
EventGroupHandle_t s_wifi_event_group __attribute__((weak)) = nullptr;
