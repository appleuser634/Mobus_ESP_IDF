#pragma once

namespace ui {

struct ScreenStateBase {
    virtual ~ScreenStateBase() = default;
};

class IScreenPresenter {
   public:
    virtual ~IScreenPresenter() = default;
    virtual void on_enter() {}
    virtual void on_exit() {}
};

class IScreenRenderer {
   public:
    virtual ~IScreenRenderer() = default;
};

}  // namespace ui
