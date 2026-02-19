#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/core/input_adapter.hpp"
#include "ui/core/screen.hpp"

namespace ui::language {

struct ViewState : public ui::ScreenStateBase {
    std::string title;
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
        if (input.up_edge || input.down_edge) {
            cycle();
        }
        if (input.type_pressed || input.enter_pressed) return Command::Confirm;
        if (input.back_pressed) return Command::Cancel;
        return Command::None;
    }

   private:
    void cycle() {
        if (state_.options.empty()) {
            state_.selected = 0;
            return;
        }
        state_.selected = (state_.selected + 1) % static_cast<int>(state_.options.size());
    }

    ViewState& state_;
};

struct RenderApi {
    std::function<void()> begin_frame;
    std::function<void(const std::string&)> draw_title;
    std::function<void(int, const std::string&, bool)> draw_option;
    std::function<void()> present;
};

class Renderer : public ui::IScreenRenderer {
   public:
    void render(const ViewState& state, const RenderApi& api) {
        if (api.begin_frame) api.begin_frame();
        if (api.draw_title) api.draw_title(state.title);
        for (int i = 0; i < static_cast<int>(state.options.size()); ++i) {
            if (api.draw_option) api.draw_option(i, state.options[i], i == state.selected);
        }
        if (api.present) api.present();
    }
};

}  // namespace ui::language
