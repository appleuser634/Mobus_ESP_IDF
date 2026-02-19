#pragma once

namespace ui {

enum class UiAction {
    None,
    Up,
    Down,
    Left,
    Right,
    Type,
    Enter,
    Back,
};

struct InputSnapshot {
    bool up_edge = false;
    bool down_edge = false;
    bool left_edge = false;
    bool right_edge = false;
    bool type_pressed = false;
    bool enter_pressed = false;
    bool back_pressed = false;
};

}  // namespace ui
