#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <button.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app/contact/actions_service.hpp"
#include "ui/common/confirm_dialog.hpp"
#include "ui/contact/pending_mvp.hpp"

namespace ui::contactrunners {

struct ActionContext {
    LGFX_Sprite& sprite;
    Button& type_button;
    Button& enter_button;
    Button& back_button;
    Joystick& joystick;
    std::function<void()> feed_wdt;
    std::function<void()> present;
    std::function<bool()> wifi_connected;
    std::function<std::string(const std::string&, const std::string&)> input_text;
};

inline bool should_use_ble(const ActionContext& ctx) {
    const bool wifi = ctx.wifi_connected ? ctx.wifi_connected() : false;
    return (!wifi) && ble_uart_is_ready();
}

inline void clear_controls(ActionContext& ctx, bool clear_enter = true) {
    ctx.type_button.clear_button_state();
    ctx.type_button.reset_timer();
    ctx.back_button.clear_button_state();
    ctx.back_button.reset_timer();
    if (clear_enter) {
        ctx.enter_button.clear_button_state();
        ctx.enter_button.reset_timer();
    }
    ctx.joystick.reset_timer();
}

inline void run_add_friend(ActionContext& ctx) {
    std::string friend_code =
        ctx.input_text ? ctx.input_text("Friend Code", "") : "";
    if (friend_code.empty()) {
        clear_controls(ctx);
        return;
    }

    ctx.sprite.fillRect(0, 0, 128, 64, 0);
    ctx.sprite.setFont(&fonts::Font2);
    ctx.sprite.setTextColor(0xFFFFFFu, 0x000000u);
    ctx.sprite.drawCenterString("Sending request...", 64, 22);
    if (ctx.present) ctx.present();

    auto send_result =
        app::contactbook::send_friend_request(friend_code, should_use_ble(ctx));
    const bool ok = send_result.ok;

    ctx.sprite.fillRect(0, 0, 128, 64, 0);
    ctx.sprite.setFont(&fonts::Font2);
    if (ok) {
        ctx.sprite.drawCenterString("Request sent!", 64, 22);
    } else {
        ctx.sprite.drawCenterString("Error:", 64, 16);
        const char* emsg = send_result.error_message.empty()
                               ? "Failed"
                               : send_result.error_message.c_str();
        ctx.sprite.drawCenterString(emsg, 64, 34);
    }
    if (ctx.present) ctx.present();
    vTaskDelay(1200 / portTICK_PERIOD_MS);
    clear_controls(ctx);
}

inline std::vector<std::pair<std::string, std::string>> fetch_pending(
    ActionContext& ctx) {
    return app::contactbook::fetch_pending_requests(should_use_ble(ctx),
                                                    ctx.feed_wdt);
}

inline void run_pending_requests(ActionContext& ctx) {
    clear_controls(ctx);
    std::vector<std::pair<std::string, std::string>> pending = fetch_pending(ctx);

    ui::contactpending::ViewState pending_state;
    pending_state.select_index = 0;
    ui::contactpending::Presenter pending_presenter(pending_state);
    ui::contactpending::Renderer pending_renderer;
    ui::contactpending::RenderApi pending_render_api;
    pending_render_api.begin_frame = [&]() {
        ctx.sprite.fillRect(0, 0, 128, 64, 0);
        ctx.sprite.setFont(&fonts::Font2);
        ctx.sprite.setTextColor(0xFFFFFFu, 0x000000u);
    };
    pending_render_api.draw_empty = [&](const std::string& msg) {
        ctx.sprite.drawCenterString(msg.c_str(), 64, 22);
    };
    pending_render_api.draw_row = [&](int y, const std::string& line, bool selected) {
        if (selected) {
            ctx.sprite.fillRect(0, y, 128, 18, 0xFFFF);
            ctx.sprite.setTextColor(0x0000, 0xFFFF);
        } else {
            ctx.sprite.setTextColor(0xFFFF, 0x0000);
        }
        ctx.sprite.setCursor(10, y);
        ctx.sprite.print(line.c_str());
    };
    pending_render_api.present = [&]() {
        if (ctx.present) ctx.present();
    };

    while (1) {
        pending_state.labels.clear();
        pending_state.labels.reserve(pending.size());
        for (const auto& item : pending) {
            pending_state.labels.push_back(item.second);
        }
        if (pending_state.labels.empty()) {
            pending_state.select_index = 0;
        } else if (pending_state.select_index >=
                   static_cast<int>(pending_state.labels.size())) {
            pending_state.select_index =
                static_cast<int>(pending_state.labels.size()) - 1;
        }
        pending_renderer.render(pending_state, pending_render_api);

        auto js = ctx.joystick.get_joystick_state();
        auto tbs = ctx.type_button.get_button_state();
        auto bbs = ctx.back_button.get_button_state();
        auto ebs = ctx.enter_button.get_button_state();
        ui::InputSnapshot pending_input;
        pending_input.up_edge = js.pushed_up_edge;
        pending_input.down_edge = js.pushed_down_edge;
        pending_input.left_edge = js.left;
        pending_input.type_pressed = tbs.pushed;
        pending_input.enter_pressed = ebs.pushed;
        pending_input.back_pressed = bbs.pushed;
        auto pending_cmd = pending_presenter.handle_input(pending_input);
        if (pending_cmd == ui::contactpending::Presenter::Command::Exit) {
            break;
        }

        if (!pending.empty() &&
            pending_cmd == ui::contactpending::Presenter::Command::Select) {
            bool accept = tbs.pushed;  // type=Accept, enter=Reject
            clear_controls(ctx);

            ui::confirmdialog::ViewState confirm_state;
            confirm_state.title = accept ? "Accept?" : "Reject?";
            confirm_state.selected = 0;
            ui::confirmdialog::Presenter confirm_presenter(confirm_state);
            ui::confirmdialog::Renderer confirm_renderer;
            ui::confirmdialog::RenderApi confirm_render_api;
            confirm_render_api.begin_frame = [&]() {
                ctx.sprite.fillRect(0, 0, 128, 64, 0);
                ctx.sprite.setTextColor(0xFFFF, 0x0000);
            };
            confirm_render_api.draw_title = [&](const std::string& title) {
                ctx.sprite.drawCenterString(title.c_str(), 64, 14);
            };
            confirm_render_api.draw_buttons = [&](int selected) {
                uint16_t noFg = (selected == 0) ? 0x0000 : 0xFFFF;
                uint16_t noBg = (selected == 0) ? 0xFFFF : 0x0000;
                uint16_t ysFg = (selected == 1) ? 0x0000 : 0xFFFF;
                uint16_t ysBg = (selected == 1) ? 0xFFFF : 0x0000;
                ctx.sprite.fillRoundRect(12, 34, 40, 18, 3, noBg);
                ctx.sprite.drawRoundRect(12, 34, 40, 18, 3, 0xFFFF);
                ctx.sprite.setTextColor(noFg, noBg);
                ctx.sprite.drawCenterString("No", 32, 36);
                ctx.sprite.fillRoundRect(76, 34, 40, 18, 3, ysBg);
                ctx.sprite.drawRoundRect(76, 34, 40, 18, 3, 0xFFFF);
                ctx.sprite.setTextColor(ysFg, ysBg);
                ctx.sprite.drawCenterString("Yes", 96, 36);
            };
            confirm_render_api.present = [&]() {
                if (ctx.present) ctx.present();
            };

            while (1) {
                auto js2 = ctx.joystick.get_joystick_state();
                auto t2 = ctx.type_button.get_button_state();
                auto e2 = ctx.enter_button.get_button_state();
                auto b2 = ctx.back_button.get_button_state();
                ui::InputSnapshot confirm_input;
                confirm_input.left_edge = js2.pushed_left_edge;
                confirm_input.right_edge = js2.pushed_right_edge;
                confirm_input.up_edge = js2.pushed_up_edge;
                confirm_input.down_edge = js2.pushed_down_edge;
                confirm_input.type_pressed = t2.pushed;
                confirm_input.enter_pressed = e2.pushed;
                confirm_input.back_pressed = b2.pushed || js2.left;

                auto confirm_cmd = confirm_presenter.handle_input(confirm_input);
                confirm_renderer.render(confirm_state, confirm_render_api);
                if (confirm_cmd == ui::confirmdialog::Presenter::Command::Cancel) {
                    break;
                }
                if (confirm_cmd == ui::confirmdialog::Presenter::Command::Confirm) {
                    std::string rid = pending[pending_state.select_index].first;
                    auto respond_result = app::contactbook::respond_friend_request(
                        rid, accept, should_use_ble(ctx));
                    const bool ok = respond_result.ok;
                    ctx.sprite.fillRect(0, 0, 128, 64, 0);
                    ctx.sprite.setTextColor(0xFFFF, 0x0000);
                    if (ok) {
                        ctx.sprite.drawCenterString("Done", 64, 22);
                    } else {
                        ctx.sprite.drawCenterString("Failed", 64, 22);
                    }
                    if (ctx.present) ctx.present();
                    vTaskDelay(800 / portTICK_PERIOD_MS);
                    pending = fetch_pending(ctx);
                    break;
                }
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
        }
        if (ctx.feed_wdt) ctx.feed_wdt();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    clear_controls(ctx);
}

}  // namespace ui::contactrunners
