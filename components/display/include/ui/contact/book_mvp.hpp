#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/core/input_adapter.hpp"
#include "ui/core/screen.hpp"

namespace ui::contactbook {

struct RowData {
    std::string label;
    bool has_unread = false;
    int unread_count = 0;
};

struct ViewState : public ui::ScreenStateBase {
    int select_index = 0;
    int contact_per_page = 4;
    int font_height = 13;
    int margin = 3;
    std::vector<RowData> rows;
};

class Presenter : public ui::IScreenPresenter {
   public:
    explicit Presenter(ViewState& state) : state_(state) {}

    void handle_input(const ui::InputSnapshot& input) {
        if (input.up_edge) {
            state_.select_index -= 1;
        } else if (input.down_edge) {
            state_.select_index += 1;
        }
        clamp();
    }

    void clamp() {
        const int last_index = state_.rows.empty()
                                   ? 0
                                   : static_cast<int>(state_.rows.size()) - 1;
        if (state_.select_index < 0) {
            state_.select_index = 0;
        } else if (state_.select_index > last_index) {
            state_.select_index = last_index;
        }
    }

   private:
    ViewState& state_;
};

struct RenderApi {
    std::function<void()> begin_frame;
    std::function<void(int y, const RowData&, bool selected)> draw_row;
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

        int page = state.select_index / state.contact_per_page;
        int start = page * state.contact_per_page;
        int end = start + state.contact_per_page - 1;
        const int last_index = static_cast<int>(state.rows.size()) - 1;
        if (end > last_index) end = last_index;

        for (int i = start; i <= end; i++) {
            const int row = i - start;
            const int y = (state.font_height + state.margin) * row;
            if (api.draw_row) api.draw_row(y, state.rows[i], i == state.select_index);
        }

        if (api.present) api.present();
    }
};

}  // namespace ui::contactbook
