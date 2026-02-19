#pragma once

#include <functional>
#include <string>

#include "ui/core/input_adapter.hpp"
#include "ui/core/screen.hpp"

namespace ui::firmwareinfo {

struct ViewState : public ui::ScreenStateBase {
    std::string title = "Firmware Info";
    std::string line1;
    std::string line2;
    std::string line3;
    std::string hint = "Back=Exit";
};

class Presenter : public ui::IScreenPresenter {
   public:
    enum class Command {
        None,
        Exit,
    };

    Command handle_input(const ui::InputSnapshot& input) {
        if (input.type_pressed || input.back_pressed) return Command::Exit;
        return Command::None;
    }
};

struct RenderApi {
    std::function<void()> begin_frame;
    std::function<void(const std::string&)> draw_title;
    std::function<void(int, const std::string&)> draw_line;
    std::function<void(const std::string&)> draw_hint;
    std::function<void()> present;
};

class Renderer : public ui::IScreenRenderer {
   public:
    void render(const ViewState& state, const RenderApi& api) {
        if (api.begin_frame) api.begin_frame();
        if (api.draw_title) api.draw_title(state.title);
        if (api.draw_line) {
            api.draw_line(16, state.line1);
            api.draw_line(28, state.line2);
            api.draw_line(40, state.line3);
        }
        if (api.draw_hint) api.draw_hint(state.hint);
        if (api.present) api.present();
    }
};

}  // namespace ui::firmwareinfo
