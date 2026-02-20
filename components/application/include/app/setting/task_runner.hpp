#pragma once

#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace app::settingtask {

template <typename FeedFn>
inline void wait_while_running(bool& running_flag, FeedFn feed_wdt) {
    while (running_flag) {
        feed_wdt();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

}  // namespace app::settingtask
