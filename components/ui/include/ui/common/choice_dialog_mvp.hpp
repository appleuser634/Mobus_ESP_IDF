#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/core/input_adapter.hpp"
#include "ui/core/screen.hpp"

namespace ui::choice {

struct ViewState : public ui::ScreenStateBase {
    std::string title_top;
    std::string title_bottom;
    std::vector<std::string> options;
    int selected = 0;
};

class Presenter : public ui::IScreenPresenter {
   public:
    enum class Command {
        None,
        Confirm,
        Cancel,
    };

    explicit Presenter(ViewState& state) : state_(state) {}

    Command handle_input(const ui::InputSnapshot& input) {
        if (input.back_pressed || input.left_edge) {
            return Command::Cancel;
        }

        if (input.up_edge) {
            state_.selected -= 1;
            wrap_selected();
            return Command::None;
        }
        if (input.down_edge) {
            state_.selected += 1;
            wrap_selected();
            return Command::None;
        }
        if (input.type_pressed || input.enter_pressed) {
            return Command::Confirm;
        }
        return Command::None;
    }

   private:
    void wrap_selected() {
        if (state_.options.empty()) {
            state_.selected = 0;
            return;
        }
        const int count = static_cast<int>(state_.options.size());
        while (state_.selected < 0) state_.selected += count;
        while (state_.selected >= count) state_.selected -= count;
    }

    ViewState& state_;
};

struct RenderApi {
    std::function<void()> begin_frame;
    std::function<void(const std::string&, const std::string&)> draw_title;
    std::function<void(int, const std::string&, bool)> draw_option;
    std::function<void()> present;
};

class Renderer : public ui::IScreenRenderer {
   public:
    void render(const ViewState& state, const RenderApi& api) {
        if (api.begin_frame) api.begin_frame();
        if (api.draw_title) api.draw_title(state.title_top, state.title_bottom);
        if (api.draw_option) {
            for (size_t i = 0; i < state.options.size(); ++i) {
                api.draw_option(static_cast<int>(i), state.options[i],
                                static_cast<int>(i) == state.selected);
            }
        }
        if (api.present) api.present();
    }
};

}  // namespace ui::choice
