#pragma once

#include <functional>
#include <string>

#include "ui/core/input_adapter.hpp"
#include "ui/core/screen.hpp"

namespace ui::blepair {

struct ViewState : public ui::ScreenStateBase {
    bool pairing = false;
    std::string code;
    long long remain_s = 0;
};

class Presenter : public ui::IScreenPresenter {
   public:
    enum class Command {
        None,
        Exit,
        TogglePairing,
    };

    Command handle_input(const ui::InputSnapshot& input) {
        if (input.back_pressed || input.left_edge) return Command::Exit;
        if (input.type_pressed || input.enter_pressed) return Command::TogglePairing;
        return Command::None;
    }
};

struct RenderApi {
    std::function<void()> begin_frame;
    std::function<void(const std::string&)> draw_title;
    std::function<void(const std::string&)> draw_status;
    std::function<void(const std::string&)> draw_code;
    std::function<void(const std::string&)> draw_ttl;
    std::function<void(const std::string&)> draw_hint;
    std::function<void()> present;
};

class Renderer : public ui::IScreenRenderer {
   public:
    void render(const ViewState& state, const RenderApi& api) {
        if (api.begin_frame) api.begin_frame();
        if (api.draw_title) api.draw_title("Bluetooth Pairing");
        if (api.draw_status) {
            api.draw_status(state.pairing ? "Status: ON" : "Status: OFF");
        }
        if (state.pairing && !state.code.empty()) {
            if (api.draw_code) api.draw_code("Code: " + state.code);
            if (api.draw_ttl) {
                api.draw_ttl("Expires: " + std::to_string(state.remain_s) + "s");
            }
        } else {
            if (api.draw_code) api.draw_code("Press to start pairing");
        }
        if (api.draw_hint) api.draw_hint("Back=Exit");
        if (api.present) api.present();
    }
};

}  // namespace ui::blepair
