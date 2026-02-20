#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/core/input_adapter.hpp"

namespace ui::wifi {

struct TextInputViewState {
    std::string title;
    std::string text;
    int select_x = 0;
    int select_y = 0;
};

enum class TextInputCommand {
    None,
    Finish,
    Cancel,
    Type,
};

class TextInputPresenter {
   public:
    explicit TextInputPresenter(TextInputViewState &state) : state_(state) {}

    TextInputCommand handle_input(const ui::InputSnapshot &input) {
        if (input.back_pressed) return TextInputCommand::Cancel;
        if (input.enter_pressed) return TextInputCommand::Finish;
        if (input.left_edge) {
            state_.select_x -= 1;
        } else if (input.right_edge) {
            state_.select_x += 1;
        } else if (input.up_edge) {
            state_.select_y -= 1;
        } else if (input.down_edge) {
            state_.select_y += 1;
        } else if (input.type_pressed) {
            return TextInputCommand::Type;
        }
        return TextInputCommand::None;
    }

    void normalize_row_count(int row_count) {
        if (row_count <= 0) {
            state_.select_y = 0;
            return;
        }
        if (state_.select_y < 0) {
            state_.select_y = row_count - 1;
        } else if (state_.select_y >= row_count) {
            state_.select_y = 0;
        }
    }

    void normalize_col_count(int col_count) {
        if (col_count <= 0) {
            state_.select_x = 0;
            return;
        }
        if (state_.select_x < 0) {
            state_.select_x = col_count - 1;
        } else if (state_.select_x >= col_count) {
            state_.select_x = 0;
        }
    }

   private:
    TextInputViewState &state_;
};

struct TextInputRenderApi {
    std::function<void()> begin_frame;
    std::function<void(const std::string &)> draw_title;
    std::function<void(const std::string &)> draw_value;
    std::function<void(int, int)> draw_selector;
    std::function<void()> present;
};

class TextInputRenderer {
   public:
    void render(const TextInputViewState &state, const TextInputRenderApi &api) {
        if (api.begin_frame) api.begin_frame();
        if (api.draw_title) api.draw_title(state.title);
        if (api.draw_value) api.draw_value(state.text);
        if (api.draw_selector) api.draw_selector(state.select_x, state.select_y);
        if (api.present) api.present();
    }
};

struct WifiMenuViewState {
    int selected = 0;
    int max_index = 0;
};

enum class WifiMenuCommand {
    None,
    Exit,
    Select,
};

class WifiMenuPresenter {
   public:
    explicit WifiMenuPresenter(WifiMenuViewState &state) : state_(state) {}

    WifiMenuCommand handle_input(const ui::InputSnapshot &input) {
        if (input.left_edge || input.back_pressed) {
            return WifiMenuCommand::Exit;
        }
        if (input.up_edge) {
            state_.selected -= 1;
            clamp();
        } else if (input.down_edge) {
            state_.selected += 1;
            clamp();
        }
        if (input.type_pressed) return WifiMenuCommand::Select;
        return WifiMenuCommand::None;
    }

    void clamp() {
        if (state_.selected < 0) state_.selected = 0;
        if (state_.selected > state_.max_index) state_.selected = state_.max_index;
    }

   private:
    WifiMenuViewState &state_;
};

}  // namespace ui::wifi
