#pragma once

#include "app/menu/navigation_usecase.hpp"

namespace app::menu {

template <typename OpenContactBook, typename OpenSettings, typename OpenGame,
          typename AfterReturn>
inline bool execute_menu_action(MenuAction action, OpenContactBook &&open_contact_book,
                                OpenSettings &&open_settings,
                                OpenGame &&open_game,
                                AfterReturn &&after_return) {
    if (action == MenuAction::None) return false;
    if (action == MenuAction::OpenContactBook) {
        open_contact_book();
    } else if (action == MenuAction::OpenSettings) {
        open_settings();
    } else if (action == MenuAction::OpenGame) {
        open_game();
    }
    after_return();
    return true;
}

template <typename PrepareFrame, typename EnableGpioWakeup,
          typename EnableSleepWakeup, typename StartLightSleep,
          typename DisableSleepWakeup, typename DisableGpioWakeup,
          typename ResetInputs, typename MarkDisplayDirty>
inline bool execute_light_sleep(const int *wake_pins, int pin_count,
                                PrepareFrame &&prepare_frame,
                                EnableGpioWakeup &&enable_gpio_wakeup,
                                EnableSleepWakeup &&enable_sleep_wakeup,
                                StartLightSleep &&start_light_sleep,
                                DisableSleepWakeup &&disable_sleep_wakeup,
                                DisableGpioWakeup &&disable_gpio_wakeup,
                                ResetInputs &&reset_inputs,
                                MarkDisplayDirty &&mark_display_dirty) {
    if (!wake_pins || pin_count <= 0) return false;
    prepare_frame();
    for (int i = 0; i < pin_count; ++i) {
        if (wake_pins[i] >= 0) enable_gpio_wakeup(wake_pins[i]);
    }
    enable_sleep_wakeup();
    start_light_sleep();
    disable_sleep_wakeup();
    for (int i = 0; i < pin_count; ++i) {
        if (wake_pins[i] >= 0) disable_gpio_wakeup(wake_pins[i]);
    }
    reset_inputs();
    mark_display_dirty();
    return true;
}

}  // namespace app::menu
