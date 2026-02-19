#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "ui/core/input_adapter.hpp"
#include "ui/core/screen.hpp"

namespace ui::messagebox {

struct ViewEntry {
    JsonObject obj;
};

struct ViewState : public ui::ScreenStateBase {
    std::string chat_to;
    std::string header_text;
    std::string my_name;
    int font_height = 16;
    int max_offset_y = 0;
    int min_offset_y = 0;
    int offset_y = 0;
    std::vector<ViewEntry> message_views;
};

class Presenter : public ui::IScreenPresenter {
   public:
    enum class Command {
        None,
        Exit,
        Compose,
    };

    explicit Presenter(ViewState& state) : state_(state) {}

    Command handle_input(const ui::InputSnapshot& input) {
        if (input.back_pressed) return Command::Exit;
        if (input.up_edge) {
            state_.offset_y += state_.font_height;
        } else if (input.down_edge) {
            state_.offset_y -= state_.font_height;
        }
        clamp_offset();
        if (input.type_pressed) return Command::Compose;
        return Command::None;
    }

    bool should_poll(int64_t now_us, int64_t last_poll_us,
                     int64_t interval_us) const {
        return (now_us - last_poll_us) >= interval_us;
    }

    void clamp_offset() {
        if (state_.offset_y > state_.max_offset_y) {
            state_.offset_y = state_.max_offset_y;
        }
        if (state_.offset_y < state_.min_offset_y) {
            state_.offset_y = state_.min_offset_y;
        }
    }

   private:
    ViewState& state_;
};

struct RenderApi {
    int screen_height = 64;
    std::function<void()> begin_frame;
    std::function<void(int, bool, int)> draw_prefix;
    std::function<void(int, const std::string&)> draw_text;
    std::function<void(const std::string&, const std::string&)> draw_header;
    std::function<void()> present;
};

class Renderer : public ui::IScreenRenderer {
   public:
    void render(const ViewState& state, const RenderApi& api) {
        if (api.begin_frame) api.begin_frame();

        for (size_t i = 0; i < state.message_views.size(); i++) {
            JsonObject msg = state.message_views[i].obj;
            std::string message = msg["message"].as<std::string>();
            std::string message_from = msg["from"].as<std::string>();

            int cursor_y = state.offset_y + (state.font_height * (i + 1));
            const int line_top = cursor_y;
            const int line_bottom = cursor_y + state.font_height;
            if (line_bottom <= 0 || line_top >= api.screen_height) continue;
            if (line_top < 0 || line_bottom > api.screen_height) continue;

            const bool incoming = (message_from != state.my_name);
            if (api.draw_prefix) api.draw_prefix(cursor_y, incoming, state.font_height);
            if (api.draw_text) api.draw_text(cursor_y, message);
        }

        if (api.draw_header) api.draw_header(state.header_text, state.chat_to);
        if (api.present) api.present();
    }
};

}  // namespace ui::messagebox
