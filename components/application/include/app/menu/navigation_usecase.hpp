#pragma once

namespace app::menu {

enum class MenuAction {
    None,
    OpenContactBook,
    OpenSettings,
    OpenGame,
};

inline int move_cursor(int cursor_index, int menu_count, bool left_edge,
                       bool right_edge) {
    if (menu_count <= 0) return 0;

    int next = cursor_index;
    if (left_edge) {
        next -= 1;
        if (next < 0) next = menu_count - 1;
    } else if (right_edge) {
        next += 1;
        if (next >= menu_count) next = 0;
    }
    return next;
}

inline MenuAction resolve_menu_action(int cursor_index, bool type_pressed) {
    if (!type_pressed) return MenuAction::None;
    if (cursor_index == 0) return MenuAction::OpenContactBook;
    if (cursor_index == 1) return MenuAction::OpenSettings;
    if (cursor_index == 2) return MenuAction::OpenGame;
    return MenuAction::None;
}

inline bool should_enter_sleep(int button_free_sec, int joystick_free_sec,
                               bool enter_pressed,
                               int idle_threshold_sec = 30) {
    if (enter_pressed) return true;
    return button_free_sec >= idle_threshold_sec &&
           joystick_free_sec >= idle_threshold_sec;
}

inline bool is_idle_timeout(int button_free_sec, int joystick_free_sec,
                            int idle_threshold_sec = 30) {
    return button_free_sec >= idle_threshold_sec &&
           joystick_free_sec >= idle_threshold_sec;
}

}  // namespace app::menu
