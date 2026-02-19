#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/core/input_adapter.hpp"
#include "ui/core/screen.hpp"
#include "ui_strings.hpp"

namespace ui::settingmenu {

struct RowData {
    ui::Key key = ui::Key::SettingsProfile;
    std::string label;
};

struct ViewState : public ui::ScreenStateBase {
    int select_index = 0;
    int item_per_page = 4;
    int font_height = 13;
    int margin = 3;
    std::vector<RowData> rows;
};

class Presenter : public ui::IScreenPresenter {
   public:
    explicit Presenter(ViewState& state) : state_(state) {}

    bool handle_input(const ui::InputSnapshot& input) {
        const int prev = state_.select_index;
        if (input.up_edge) {
            state_.select_index -= 1;
        } else if (input.down_edge) {
            state_.select_index += 1;
        }
        clamp();
        return prev != state_.select_index;
    }

    void clamp() {
        const int last = state_.rows.empty()
                             ? 0
                             : static_cast<int>(state_.rows.size()) - 1;
        if (state_.select_index < 0) {
            state_.select_index = 0;
        } else if (state_.select_index > last) {
            state_.select_index = last;
        }
    }

   private:
    ViewState& state_;
};

struct RenderApi {
    std::function<void()> begin_frame;
    std::function<void(int, const RowData&, bool)> draw_row;
    std::function<void()> present;
};

class Renderer : public ui::IScreenRenderer {
   public:
    void render(const ViewState& state, const RenderApi& api) {
        if (api.begin_frame) api.begin_frame();
        if (state.rows.empty()) {
            if (api.present) api.present();
            return;
        }

        const int page = state.select_index / state.item_per_page;
        const int start = page * state.item_per_page;
        int end = start + state.item_per_page - 1;
        const int last = static_cast<int>(state.rows.size()) - 1;
        if (end > last) end = last;

        for (int i = start; i <= end; ++i) {
            const int row = i - start;
            const int y = (state.font_height + state.margin) * row;
            if (api.draw_row) api.draw_row(y, state.rows[i], i == state.select_index);
        }

        if (api.present) api.present();
    }
};

}  // namespace ui::settingmenu
