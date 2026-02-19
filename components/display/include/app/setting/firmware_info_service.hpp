#pragma once

#include <cstdio>
#include <string>

#include "esp_app_desc.h"
#include "esp_ota_ops.h"

namespace app::firmwareinfo {

struct FirmwareInfoLines {
    std::string line1;
    std::string line2;
    std::string line3;
};

inline FirmwareInfoLines collect() {
    FirmwareInfoLines out;

    const esp_app_desc_t* app = esp_app_get_description();
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot = esp_ota_get_boot_partition();

    const char* ver = app ? app->version : "unknown";
    const char* run_label = running ? running->label : "-";
    const char* boot_label = boot ? boot->label : "-";

    char line[64];
    std::snprintf(line, sizeof(line), "Ver: %s", ver);
    out.line1 = line;

    if (running) {
        std::snprintf(line, sizeof(line), "Run: %s @%06lx", run_label,
                      (unsigned long)running->address);
    } else {
        std::snprintf(line, sizeof(line), "Run: -");
    }
    out.line2 = line;

    if (boot) {
        std::snprintf(line, sizeof(line), "Boot:%s @%06lx", boot_label,
                      (unsigned long)boot->address);
    } else {
        std::snprintf(line, sizeof(line), "Boot: -");
    }
    out.line3 = line;

    return out;
}

}  // namespace app::firmwareinfo
