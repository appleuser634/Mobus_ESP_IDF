#pragma once

#include <string>

#include "esp_err.h"

namespace app {

struct MessageFetchResult {
    esp_err_t err = ESP_FAIL;
    bool from_ble = false;
};

class IMessageService {
   public:
    virtual ~IMessageService() = default;
    virtual MessageFetchResult fetch_history(const std::string& contact_id,
                                             int timeout_ms) = 0;
    virtual bool mark_all_read(const std::string& contact_id) = 0;
};

}  // namespace app
