#pragma once

#include <algorithm>
#include <functional>
#include <string>

#include "ui/core/input_adapter.hpp"
#include "ui/core/screen.hpp"

namespace ui::soundsettings {

struct ViewState : public ui::ScreenStateBase {
    bool enabled = true;
    float volume = 1.0f;
};

struct InputResult {
    bool toggled = false;
    bool volume_changed = false;
    bool exit = false;
};

class Presenter : public ui::IScreenPresenter {
   public:
    explicit Presenter(ViewState& state) : state_(state) {}

    InputResult handle_input(const ui::InputSnapshot& input) {
        InputResult result;
        if (input.type_pressed) {
            state_.enabled = !state_.enabled;
            result.toggled = true;
        }

        constexpr float kStep = 0.05f;
        if (input.up_edge || input.right_edge) {
            state_.volume = std::min(1.0f, state_.volume + kStep);
            result.volume_changed = true;
        } else if (input.down_edge || input.left_edge) {
            state_.volume = std::max(0.0f, state_.volume - kStep);
            result.volume_changed = true;
        }

        if (input.enter_pressed || input.back_pressed) {
            result.exit = true;
        }
        return result;
    }

   private:
    ViewState& state_;
};

struct RenderApi {
    std::function<void()> begin_frame;
    std::function<void(const std::string&)> draw_title;
    std::function<void(const std::string&)> draw_status;
    std::function<void(const std::string&)> draw_volume;
    std::function<void(const std::string&)> draw_hint1;
    std::function<void(const std::string&)> draw_hint2;
    std::function<void()> present;
};

class Renderer : public ui::IScreenRenderer {
   public:
    void render(const ViewState& state, const RenderApi& api) {
        if (api.begin_frame) api.begin_frame();

        if (api.draw_title) api.draw_title("Sound Settings");
        if (api.draw_status) {
            api.draw_status(state.enabled ? "Status: ON" : "Status: OFF");
        }
        if (api.draw_volume) {
            const int vol_pct = static_cast<int>(state.volume * 100.0f + 0.5f);
            api.draw_volume("Volume: " + std::to_string(vol_pct) + "%");
        }
        if (api.draw_hint1) api.draw_hint1("Type:Toggle  Up/Down:Vol");
        if (api.draw_hint2) api.draw_hint2("Back/Enter:Exit");

        if (api.present) api.present();
    }
};

}  // namespace ui::soundsettings
