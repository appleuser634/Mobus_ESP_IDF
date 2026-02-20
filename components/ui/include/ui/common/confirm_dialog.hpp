#pragma once

#include <functional>
#include <string>

#include "ui/core/input_adapter.hpp"
#include "ui/core/screen.hpp"

namespace ui::confirmdialog {

struct ViewState : public ui::ScreenStateBase {
    std::string title;
    int selected = 0;  // 0: No, 1: Yes
};

class Presenter : public ui::IScreenPresenter {
   public:
    enum class Command {
        None,
        Cancel,
        Confirm,
    };

    explicit Presenter(ViewState& state) : state_(state) {}

    Command handle_input(const ui::InputSnapshot& input) {
        if (input.left_edge || input.up_edge) state_.selected = 0;
        if (input.right_edge || input.down_edge) state_.selected = 1;
        if (input.back_pressed) return Command::Cancel;
        if (input.type_pressed || input.enter_pressed) {
            return state_.selected == 1 ? Command::Confirm : Command::Cancel;
        }
        return Command::None;
    }

   private:
    ViewState& state_;
};

struct RenderApi {
    std::function<void()> begin_frame;
    std::function<void(const std::string&)> draw_title;
    std::function<void(int selected)> draw_buttons;
    std::function<void()> present;
};

class Renderer : public ui::IScreenRenderer {
   public:
    void render(const ViewState& state, const RenderApi& api) {
        if (api.begin_frame) api.begin_frame();
        if (api.draw_title) api.draw_title(state.title);
        if (api.draw_buttons) api.draw_buttons(state.selected);
        if (api.present) api.present();
    }
};

}  // namespace ui::confirmdialog
