#pragma once

#include <string>
#include "esp_err.h"

namespace ota_client {

// Configure manifest URL in NVS: key "ota_manifest"
// Auto update flag in NVS: key "ota_auto" ("true"/"false")

// Returns ESP_OK on success, performs OTA if newer is available.
esp_err_t check_and_update_once();

// Background task which runs periodically when auto update is ON.
void start_background_task();

} // namespace ota_client

