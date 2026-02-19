#pragma once

#include <functional>

#include "app/menu/navigation_usecase.hpp"
#include "ui/core/input_adapter.hpp"
#include "ui/core/screen.hpp"

namespace ui::menu {

struct ViewState : public ui::ScreenStateBase {
    int cursor_index = 0;
    int menu_count = 3;
    int radio_level = 0;
    int power_per_pix = 0;
    bool charging = false;
    bool has_notification = false;
};

class Presenter : public ui::IScreenPresenter {
   public:
    explicit Presenter(ViewState& state) : state_(state) {}

    bool handle_input(const ui::InputSnapshot& input) {
        const int prev = state_.cursor_index;
        state_.cursor_index = app::menu::move_cursor(
            state_.cursor_index, state_.menu_count, input.left_edge,
            input.right_edge);
        return prev != state_.cursor_index;
    }

    app::menu::MenuAction resolve_action(const ui::InputSnapshot& input) const {
        return app::menu::resolve_menu_action(state_.cursor_index,
                                              input.type_pressed);
    }

   private:
    ViewState& state_;
};

struct RenderApi {
    std::function<void()> begin_frame;
    std::function<void(int, int, bool)> draw_status;
    std::function<void(int, int, int, int, int)> draw_selection;
    std::function<void(int, bool)> draw_menu_icon;
    std::function<void(bool)> draw_notification;
    std::function<void()> present;
};

class Renderer : public ui::IScreenRenderer {
   public:
    void render(const ViewState& state, const RenderApi& api) {
        if (api.begin_frame) api.begin_frame();
        if (api.draw_status) {
            api.draw_status(state.radio_level, state.power_per_pix,
                            state.charging);
        }
        if (api.draw_selection) {
            const int x = (state.cursor_index == 0)
                              ? 7
                              : ((state.cursor_index == 1) ? 49 : 91);
            api.draw_selection(x, 20, 34, 34, 5);
        }
        if (api.draw_menu_icon) {
            for (int i = 0; i < state.menu_count; ++i) {
                api.draw_menu_icon(i, i == state.cursor_index);
            }
        }
        if (state.has_notification && api.draw_notification) {
            api.draw_notification(state.cursor_index == 0);
        }
        if (api.present) api.present();
    }
};

}  // namespace ui::menu
