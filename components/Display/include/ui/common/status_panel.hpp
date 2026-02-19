#pragma once

#include <functional>
#include <string>

namespace ui {

struct StatusPanelApi {
    std::function<void()> begin_frame;
    std::function<void(const std::string&, int)> draw_center_text;
    std::function<void()> present;
};

inline void render_status_panel(const StatusPanelApi& api,
                                const std::string& line1,
                                const std::string& line2,
                                int y1 = 22, int y2 = 40) {
    if (api.begin_frame) api.begin_frame();
    if (api.draw_center_text) {
        api.draw_center_text(line1, y1);
        if (!line2.empty()) api.draw_center_text(line2, y2);
    }
    if (api.present) api.present();
}

}  // namespace ui
