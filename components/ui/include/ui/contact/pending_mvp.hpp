#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/core/input_adapter.hpp"
#include "ui/core/screen.hpp"

namespace ui::contactpending {

struct ViewState : public ui::ScreenStateBase {
    int select_index = 0;
    int page_size = 3;
    std::vector<std::string> labels;
};

class Presenter : public ui::IScreenPresenter {
   public:
    enum class Command {
        None,
        Exit,
        Select,
    };

    explicit Presenter(ViewState& state) : state_(state) {}

    Command handle_input(const ui::InputSnapshot& input) {
        if (input.left_edge || input.back_pressed) return Command::Exit;
        if (input.up_edge && state_.select_index > 0) {
            state_.select_index -= 1;
        }
        if (input.down_edge && state_.select_index + 1 < (int)state_.labels.size()) {
            state_.select_index += 1;
        }
        if (input.type_pressed || input.enter_pressed) return Command::Select;
        return Command::None;
    }

   private:
    ViewState& state_;
};

struct RenderApi {
    std::function<void()> begin_frame;
    std::function<void(const std::string&)> draw_empty;
    std::function<void(int y, const std::string& label, bool selected)> draw_row;
    std::function<void()> present;
};

class Renderer : public ui::IScreenRenderer {
   public:
    void render(const ViewState& state, const RenderApi& api) {
        if (api.begin_frame) api.begin_frame();
        if (state.labels.empty()) {
            if (api.draw_empty) api.draw_empty("No pending requests");
            if (api.present) api.present();
            return;
        }

        const int start = (state.select_index / state.page_size) * state.page_size;
        int show = (int)state.labels.size() - start;
        if (show > state.page_size) show = state.page_size;
        const int row_h = 20;

        for (int i = 0; i < show; i++) {
            const int idx = start + i;
            const int y = i * row_h;
            if (idx < 0 || idx >= (int)state.labels.size()) continue;
            if (api.draw_row) {
                api.draw_row(y, state.labels[idx], idx == state.select_index);
            }
        }

        if (api.present) api.present();
    }
};

}  // namespace ui::contactpending
