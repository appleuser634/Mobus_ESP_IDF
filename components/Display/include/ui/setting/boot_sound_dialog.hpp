#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/core/input_adapter.hpp"
#include "ui/core/screen.hpp"

namespace ui::bootsound {

struct ViewState : public ui::ScreenStateBase {
    std::vector<std::string> options;
    int selected = 0;
};

class Presenter : public ui::IScreenPresenter {
   public:
    enum class Command {
        None,
        Preview,
        SaveAndExit,
    };

    explicit Presenter(ViewState& state) : state_(state) {}

    Command handle_input(const ui::InputSnapshot& input) {
        if (input.type_pressed && !state_.options.empty()) {
            state_.selected = (state_.selected + 1) %
                              static_cast<int>(state_.options.size());
        }
        if (input.enter_pressed) return Command::Preview;
        if (input.back_pressed) return Command::SaveAndExit;
        return Command::None;
    }

    std::string selected_id() const {
        if (state_.options.empty()) return "cute";
        if (state_.selected < 0 ||
            state_.selected >= static_cast<int>(state_.options.size())) {
            return "cute";
        }
        return state_.options[state_.selected];
    }

   private:
    ViewState& state_;
};

struct RenderApi {
    std::function<void()> begin_frame;
    std::function<void(const std::string&)> draw_title;
    std::function<void(const std::string&)> draw_selected;
    std::function<void(const std::string&)> draw_hint1;
    std::function<void(const std::string&)> draw_hint2;
    std::function<void()> present;
};

class Renderer : public ui::IScreenRenderer {
   public:
    void render(const ViewState& state, const RenderApi& api,
                const std::string& selected_name) {
        if (api.begin_frame) api.begin_frame();
        if (api.draw_title) api.draw_title("Boot Sound");
        if (api.draw_selected) api.draw_selected(selected_name);
        if (api.draw_hint1) api.draw_hint1("Type:Next  Enter:Preview");
        if (api.draw_hint2) api.draw_hint2("Back:Save");
        if (api.present) api.present();
    }
};

}  // namespace ui::bootsound
