#pragma once

namespace app {

class IConnectivityService {
   public:
    virtual ~IConnectivityService() = default;
    virtual bool is_wifi_connected() const = 0;
    virtual bool is_ble_ready() const = 0;
};

}  // namespace app
