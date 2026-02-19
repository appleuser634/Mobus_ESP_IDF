#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/core/input_adapter.hpp"
#include "ui/core/screen.hpp"

namespace ui::textmodal {

struct ViewState : public ui::ScreenStateBase {
    std::string title;
    std::vector<std::string> lines;
};

class Presenter : public ui::IScreenPresenter {
   public:
    enum class Command {
        None,
        Exit,
    };

    Command handle_input(const ui::InputSnapshot& input) {
        if (input.back_pressed || input.type_pressed || input.left_edge) {
            return Command::Exit;
        }
        return Command::None;
    }
};

struct RenderApi {
    std::function<void()> begin_frame;
    std::function<void(const std::string&)> draw_title;
    std::function<void(int, const std::string&)> draw_line;
    std::function<void()> present;
};

class Renderer : public ui::IScreenRenderer {
   public:
    void render(const ViewState& state, const RenderApi& api,
                int first_line_y = 22, int line_height = 14) {
        if (api.begin_frame) api.begin_frame();
        if (api.draw_title) api.draw_title(state.title);
        if (api.draw_line) {
            int y = first_line_y;
            for (const auto& line : state.lines) {
                api.draw_line(y, line);
                y += line_height;
            }
        }
        if (api.present) api.present();
    }
};

}  // namespace ui::textmodal
