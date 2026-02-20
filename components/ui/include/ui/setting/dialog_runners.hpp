#pragma once

#include <functional>
#include <string>

#include <button.h>
#include <sound_settings.hpp>

#include "app/setting/action_service.hpp"
#include "app/setting/bluetooth_pairing_service.hpp"
#include "app/setting/boot_sound_service.hpp"
#include "app/setting/firmware_info_service.hpp"
#include "app/setting/language_service.hpp"
#include "app/setting/ota_manifest_service.hpp"
#include "misaki_font.hpp"
#include "nvs_rw.hpp"
#include "ui/common/confirm_dialog.hpp"
#include "ui/common/text_modal.hpp"
#include "ui/setting/bluetooth_pairing_mvp.hpp"
#include "ui/setting/boot_sound_dialog.hpp"
#include "ui/setting/firmware_info_dialog.hpp"
#include "ui/setting/language_dialog.hpp"
#include "ui/setting/sound_settings_mvp.hpp"
#include "ui_strings.hpp"

extern "C" void mobus_request_factory_reset();

namespace ui::settingrunners {

struct DialogContext {
    LGFX_Sprite& sprite;
    Button& type_button;
    Button& enter_button;
    Button& back_button;
    Joystick& joystick;
    std::function<void()> feed_wdt;
    std::function<void()> present;
    std::function<void()> delete_sprite;
    std::function<void()> recreate_sprite;
    std::function<void()> mqtt_pause;
    std::function<void()> mqtt_resume;
    std::function<bool()> wifi_connected;
};

inline void clear_controls(DialogContext& ctx, bool clear_enter) {
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

inline void run_language(DialogContext& ctx, ui::Lang lang_now) {
    clear_controls(ctx, true);

    ui::language::ViewState lang_state;
    lang_state.title = ui::text(ui::Key::TitleLanguage, lang_now);
    lang_state.options = {
        ui::text(ui::Key::LangEnglish, lang_now),
        ui::text(ui::Key::LangJapanese, lang_now),
    };
    lang_state.selected = app::languagesetting::current_index();
    ui::language::Presenter lang_presenter(lang_state);
    ui::language::Renderer lang_renderer;
    ui::language::RenderApi lang_render_api{
        .begin_frame = [&]() { ctx.sprite.fillRect(0, 0, 128, 64, 0); },
        .draw_title =
            [&](const std::string& title) {
                const lgfx::IFont* lang_font =
                    (lang_now == ui::Lang::Ja)
                        ? static_cast<const lgfx::IFont*>(&mobus_fonts::MisakiGothic8())
                        : static_cast<const lgfx::IFont*>(&fonts::Font2);
                ctx.sprite.setFont(lang_font);
                ctx.sprite.setTextColor(0xFFFFFFu, 0x000000u);
                ctx.sprite.drawCenterString(title.c_str(), 64, 6);
            },
        .draw_option =
            [&](int i, const std::string& text, bool selected) {
                const int y = 24 + i * 16;
                if (selected) {
                    ctx.sprite.fillRect(8, y - 2, 112, 14, 0xFFFF);
                    ctx.sprite.setTextColor(0x000000u, 0xFFFFFFu);
                } else {
                    ctx.sprite.setTextColor(0xFFFFFFu, 0x000000u);
                }
                ctx.sprite.drawCenterString(text.c_str(), 64, y);
            },
        .present = [&]() { ctx.present(); }};

    while (1) {
        if (ctx.feed_wdt) ctx.feed_wdt();
        auto js = ctx.joystick.get_joystick_state();
        auto tbs = ctx.type_button.get_button_state();
        auto ebs = ctx.enter_button.get_button_state();
        auto bbs = ctx.back_button.get_button_state();

        ui::InputSnapshot lang_input;
        lang_input.up_edge = js.pushed_up_edge;
        lang_input.down_edge = js.pushed_down_edge;
        lang_input.type_pressed = tbs.pushed;
        lang_input.enter_pressed = ebs.pushed;
        lang_input.back_pressed = bbs.pushed || js.left;
        auto cmd = lang_presenter.handle_input(lang_input);
        lang_renderer.render(lang_state, lang_render_api);

        if (cmd == ui::language::Presenter::Command::Confirm) {
            save_nvs("ui_lang", app::languagesetting::index_to_nvs_value(lang_state.selected));
            clear_controls(ctx, true);
            break;
        }
        if (cmd == ui::language::Presenter::Command::Cancel) {
            clear_controls(ctx, true);
            break;
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

inline void run_sound(DialogContext& ctx, bool& sound_dirty_flag) {
    clear_controls(ctx, true);

    ui::soundsettings::ViewState sound_state;
    sound_state.enabled = sound_settings::enabled();
    sound_state.volume = sound_settings::volume();
    ui::soundsettings::Presenter sound_presenter(sound_state);
    ui::soundsettings::Renderer sound_renderer;
    ui::soundsettings::RenderApi sound_render_api{
        .begin_frame = [&]() { ctx.sprite.fillRect(0, 0, 128, 64, 0); },
        .draw_title =
            [&](const std::string& text) {
                ctx.sprite.setFont(&fonts::Font2);
                ctx.sprite.setTextColor(0xFFFFFFu, 0x000000u);
                ctx.sprite.drawCenterString(text.c_str(), 64, 6);
            },
        .draw_status =
            [&](const std::string& text) { ctx.sprite.drawCenterString(text.c_str(), 64, 22); },
        .draw_volume =
            [&](const std::string& text) { ctx.sprite.drawCenterString(text.c_str(), 64, 36); },
        .draw_hint1 =
            [&](const std::string& text) { ctx.sprite.drawCenterString(text.c_str(), 64, 50); },
        .draw_hint2 =
            [&](const std::string& text) { ctx.sprite.drawCenterString(text.c_str(), 64, 58); },
        .present = [&]() { ctx.present(); }};

    while (1) {
        if (ctx.feed_wdt) ctx.feed_wdt();
        auto tbs = ctx.type_button.get_button_state();
        auto ebs = ctx.enter_button.get_button_state();
        auto bbs = ctx.back_button.get_button_state();
        auto js = ctx.joystick.get_joystick_state();

        ui::InputSnapshot sound_input;
        sound_input.up_edge = js.pushed_up_edge;
        sound_input.down_edge = js.pushed_down_edge;
        sound_input.left_edge = js.pushed_left_edge;
        sound_input.right_edge = js.pushed_right_edge;
        sound_input.type_pressed = tbs.pushed;
        sound_input.enter_pressed = ebs.pushed;
        sound_input.back_pressed = bbs.pushed;
        auto result = sound_presenter.handle_input(sound_input);
        sound_renderer.render(sound_state, sound_render_api);

        if (result.toggled) {
            sound_settings::set_enabled(sound_state.enabled, false);
            sound_dirty_flag = true;
            ctx.type_button.clear_button_state();
            ctx.type_button.reset_timer();
        }
        if (result.volume_changed) {
            sound_settings::set_volume(sound_state.volume, false);
            sound_dirty_flag = true;
            sound_state.volume = sound_settings::volume();
        }
        if (result.exit) {
            ctx.enter_button.clear_button_state();
            ctx.back_button.clear_button_state();
            break;
        }

        if (ctx.feed_wdt) ctx.feed_wdt();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    ctx.joystick.reset_timer();
    ctx.type_button.clear_button_state();
    ctx.type_button.reset_timer();
    if (sound_dirty_flag) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

inline void run_boot_sound(DialogContext& ctx) {
    clear_controls(ctx, true);

    std::string cur = get_nvs((char*)"boot_sound");
    if (cur.empty()) cur = "cute";

    ui::bootsound::ViewState bs_state;
    bs_state.options = app::bootsound::available_options();
    bs_state.selected = app::bootsound::index_of(bs_state.options, cur);
    ui::bootsound::Presenter bs_presenter(bs_state);
    ui::bootsound::Renderer bs_renderer;
    ui::bootsound::RenderApi bs_render_api{
        .begin_frame = [&]() { ctx.sprite.fillRect(0, 0, 128, 64, 0); },
        .draw_title =
            [&](const std::string& text) {
                ctx.sprite.setFont(&fonts::Font2);
                ctx.sprite.setTextColor(0xFFFFFFu, 0x000000u);
                ctx.sprite.drawCenterString(text.c_str(), 64, 6);
            },
        .draw_selected =
            [&](const std::string& text) { ctx.sprite.drawCenterString(text.c_str(), 64, 24); },
        .draw_hint1 =
            [&](const std::string& text) { ctx.sprite.drawCenterString(text.c_str(), 64, 40); },
        .draw_hint2 =
            [&](const std::string& text) { ctx.sprite.drawCenterString(text.c_str(), 64, 52); },
        .present = [&]() { ctx.present(); }};

    while (1) {
        if (ctx.feed_wdt) ctx.feed_wdt();
        auto tbs = ctx.type_button.get_button_state();
        auto ebs = ctx.enter_button.get_button_state();
        auto bbs = ctx.back_button.get_button_state();
        auto js = ctx.joystick.get_joystick_state();

        ui::InputSnapshot bs_input;
        bs_input.type_pressed = tbs.pushed;
        bs_input.enter_pressed = ebs.pushed;
        bs_input.back_pressed = bbs.pushed || js.left;
        auto cmd = bs_presenter.handle_input(bs_input);
        const std::string selected_id = bs_presenter.selected_id();
        bs_renderer.render(bs_state, bs_render_api, app::bootsound::display_name(selected_id));

        if (cmd == ui::bootsound::Presenter::Command::SaveAndExit) {
            save_nvs((char*)"boot_sound", selected_id);
            ctx.type_button.clear_button_state();
            ctx.enter_button.clear_button_state();
            ctx.back_button.clear_button_state();
            break;
        }
        if (cmd == ui::bootsound::Presenter::Command::Preview) {
            app::bootsound::play_preview(selected_id);
            ctx.enter_button.clear_button_state();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    ctx.joystick.reset_timer();
}

inline void run_bluetooth_pairing(DialogContext& ctx) {
    ctx.type_button.clear_button_state();
    ctx.joystick.reset_timer();
    app::blepair::PairingState pairing_state = app::blepair::load_state();
    app::blepair::normalize_state(pairing_state, esp_timer_get_time());
    if (pairing_state.pairing) {
        ble_uart_enable();
    }

    ui::blepair::Presenter ble_presenter;
    ui::blepair::Renderer ble_renderer;
    ui::blepair::RenderApi ble_render_api{
        .begin_frame = [&]() { ctx.sprite.fillRect(0, 0, 128, 64, 0); },
        .draw_title =
            [&](const std::string& text) {
                ctx.sprite.setFont(&fonts::Font2);
                ctx.sprite.setTextColor(0xFFFFFFu, 0x000000u);
                ctx.sprite.drawCenterString(text.c_str(), 64, 4);
            },
        .draw_status =
            [&](const std::string& text) {
                ctx.sprite.setCursor(6, 22);
                ctx.sprite.print(text.c_str());
            },
        .draw_code =
            [&](const std::string& text) {
                ctx.sprite.setCursor(6, 36);
                ctx.sprite.print(text.c_str());
            },
        .draw_ttl =
            [&](const std::string& text) {
                ctx.sprite.setCursor(6, 50);
                ctx.sprite.print(text.c_str());
            },
        .draw_hint = [&](const std::string&) {},
        .present = [&]() { ctx.present(); }};

    while (1) {
        Joystick::joystick_state_t jst = ctx.joystick.get_joystick_state();
        Button::button_state_t tbs = ctx.type_button.get_button_state();
        Button::button_state_t bbs = ctx.back_button.get_button_state();
        Button::button_state_t ebs = ctx.enter_button.get_button_state();

        const long long now_us = esp_timer_get_time();
        app::blepair::normalize_state(pairing_state, now_us);
        if (!pairing_state.pairing && ble_uart_is_ready()) {
            ble_uart_disable();
            app::blepair::restore_wifi_if_needed();
            if (ctx.mqtt_resume) ctx.mqtt_resume();
        }

        ui::blepair::ViewState ble_view_state;
        ble_view_state.pairing = pairing_state.pairing;
        ble_view_state.code = pairing_state.code;
        ble_view_state.remain_s = app::blepair::remaining_seconds(pairing_state, now_us);

        ui::InputSnapshot ble_input;
        ble_input.left_edge = jst.left;
        ble_input.back_pressed = bbs.pushed;
        ble_input.type_pressed = tbs.pushed;
        ble_input.enter_pressed = ebs.pushed;
        auto cmd = ble_presenter.handle_input(ble_input);
        ble_renderer.render(ble_view_state, ble_render_api);

        if (cmd == ui::blepair::Presenter::Command::Exit) {
            break;
        }
        if (cmd == ui::blepair::Presenter::Command::TogglePairing) {
            if (!pairing_state.pairing) {
                const bool had_wifi = ctx.wifi_connected ? ctx.wifi_connected() : false;
                app::blepair::disable_wifi_for_pairing(had_wifi);
                app::blepair::start_pairing(pairing_state, now_us);

                ctx.sprite.fillRect(0, 0, 128, 64, 0);
                ctx.sprite.setFont(&fonts::Font2);
                ctx.sprite.setTextColor(0xFFFFFFu, 0x000000u);
                ctx.sprite.drawCenterString("Enabling BLE...", 64, 26);
                ctx.present();

                if (ctx.mqtt_pause) ctx.mqtt_pause();
                if (ctx.delete_sprite) ctx.delete_sprite();
                ble_uart_enable();
                if (ble_uart_last_err() != 0) {
                    app::blepair::stop_pairing(pairing_state);
                    app::blepair::restore_wifi_if_needed();

                    if (ctx.recreate_sprite) ctx.recreate_sprite();
                    ctx.sprite.fillRect(0, 0, 128, 64, 0);
                    ctx.sprite.setFont(&fonts::Font2);
                    ctx.sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    ctx.sprite.drawCenterString("BLE Init Failed", 64, 22);
                    ctx.sprite.drawCenterString("Check memory/CFG", 64, 40);
                    ctx.present();
                    vTaskDelay(1200 / portTICK_PERIOD_MS);
                    if (ctx.mqtt_resume) ctx.mqtt_resume();
                    break;
                }
                if (ctx.recreate_sprite) ctx.recreate_sprite();
            } else {
                app::blepair::stop_pairing(pairing_state);
                app::blepair::restore_wifi_if_needed();
                if (ctx.mqtt_resume) ctx.mqtt_resume();
            }
            ctx.type_button.clear_button_state();
            ctx.type_button.reset_timer();
            ctx.enter_button.clear_button_state();
            ctx.enter_button.reset_timer();
            ctx.joystick.reset_timer();
        }

        if (ctx.feed_wdt) ctx.feed_wdt();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

inline void run_ota_manifest(DialogContext& ctx) {
    ui::textmodal::ViewState modal_state;
    modal_state.title = "OTA Manifest";
    modal_state.lines =
        app::otamanifest::wrap_lines(app::otamanifest::current_manifest_url(), 21, 3);
    clear_controls(ctx, false);
    ui::textmodal::Presenter modal_presenter;
    ui::textmodal::Renderer modal_renderer;
    ui::textmodal::RenderApi modal_render_api{
        .begin_frame = [&]() { ctx.sprite.fillRect(0, 0, 128, 64, 0); },
        .draw_title =
            [&](const std::string& text) {
                ctx.sprite.setFont(&fonts::Font2);
                ctx.sprite.setTextColor(0xFFFFFFu, 0x000000u);
                ctx.sprite.drawCenterString(text.c_str(), 64, 4);
            },
        .draw_line =
            [&](int y, const std::string& text) { ctx.sprite.drawCenterString(text.c_str(), 64, y); },
        .present = [&]() { ctx.present(); }};
    while (1) {
        if (ctx.feed_wdt) ctx.feed_wdt();
        Joystick::joystick_state_t js = ctx.joystick.get_joystick_state();
        Button::button_state_t tb = ctx.type_button.get_button_state();
        Button::button_state_t bb = ctx.back_button.get_button_state();

        ui::InputSnapshot modal_input;
        modal_input.type_pressed = tb.pushed;
        modal_input.back_pressed = bb.pushed;
        modal_input.left_edge = js.left;
        auto cmd = modal_presenter.handle_input(modal_input);
        modal_renderer.render(modal_state, modal_render_api);
        if (cmd == ui::textmodal::Presenter::Command::Exit) {
            break;
        }
        if (ctx.feed_wdt) ctx.feed_wdt();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    clear_controls(ctx, false);
}

inline void run_firmware_info(DialogContext& ctx) {
    ctx.type_button.clear_button_state();
    ctx.type_button.reset_timer();
    ctx.back_button.clear_button_state();
    ctx.back_button.reset_timer();
    vTaskDelay(pdMS_TO_TICKS(120));

    ui::firmwareinfo::Presenter fw_presenter;
    ui::firmwareinfo::Renderer fw_renderer;
    ui::firmwareinfo::RenderApi fw_render_api{
        .begin_frame = [&]() {
            ctx.sprite.fillRect(0, 0, 128, 64, 0);
            ctx.sprite.setFont(&fonts::Font2);
            ctx.sprite.setTextColor(0xFFFFFFu, 0x000000u);
        },
        .draw_title = [&](const std::string& text) { ctx.sprite.drawCenterString(text.c_str(), 64, 0); },
        .draw_line = [&](int y, const std::string& text) { ctx.sprite.drawString(text.c_str(), 2, y); },
        .draw_hint = [&](const std::string& text) { ctx.sprite.drawString(text.c_str(), 2, 54); },
        .present = [&]() { ctx.present(); }};

    while (1) {
        ui::firmwareinfo::ViewState fw_state;
        const auto info = app::firmwareinfo::collect();
        fw_state.line1 = info.line1;
        fw_state.line2 = info.line2;
        fw_state.line3 = info.line3;
        fw_renderer.render(fw_state, fw_render_api);

        Button::button_state_t tbs = ctx.type_button.get_button_state();
        Button::button_state_t bbs = ctx.back_button.get_button_state();
        ui::InputSnapshot fw_input;
        fw_input.type_pressed = tbs.pushed;
        fw_input.back_pressed = bbs.pushed;
        if (fw_presenter.handle_input(fw_input) == ui::firmwareinfo::Presenter::Command::Exit) {
            ctx.type_button.clear_button_state();
            ctx.type_button.reset_timer();
            ctx.back_button.clear_button_state();
            ctx.back_button.reset_timer();
            break;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    ctx.joystick.reset_timer();
}

inline void run_factory_reset(DialogContext& ctx) {
    clear_controls(ctx, true);

    ui::confirmdialog::ViewState confirm_state;
    confirm_state.title = "Factory Reset?";
    confirm_state.selected = 0;
    ui::confirmdialog::Presenter confirm_presenter(confirm_state);
    ui::confirmdialog::Renderer confirm_renderer;
    ui::confirmdialog::RenderApi confirm_render_api;
    confirm_render_api.begin_frame = [&]() {
        ctx.sprite.fillRect(0, 0, 128, 64, 0);
        ctx.sprite.setFont(&fonts::Font2);
        ctx.sprite.setTextColor(0xFFFFFFu, 0x000000u);
    };
    confirm_render_api.draw_title = [&](const std::string& title) {
        ctx.sprite.drawCenterString(title.c_str(), 64, 10);
    };
    confirm_render_api.draw_buttons = [&](int selected) {
        uint16_t noFg = (selected == 0) ? 0x0000 : 0xFFFF;
        uint16_t noBg = (selected == 0) ? 0xFFFF : 0x0000;
        ctx.sprite.fillRoundRect(12, 34, 40, 18, 3, noBg);
        ctx.sprite.drawRoundRect(12, 34, 40, 18, 3, 0xFFFF);
        ctx.sprite.setTextColor(noFg, noBg);
        ctx.sprite.drawCenterString("No", 12 + 20, 36);

        uint16_t ysFg = (selected == 1) ? 0x0000 : 0xFFFF;
        uint16_t ysBg = (selected == 1) ? 0xFFFF : 0x0000;
        ctx.sprite.fillRoundRect(76, 34, 40, 18, 3, ysBg);
        ctx.sprite.drawRoundRect(76, 34, 40, 18, 3, 0xFFFF);
        ctx.sprite.setTextColor(ysFg, ysBg);
        ctx.sprite.drawCenterString("Yes", 76 + 20, 36);
    };
    confirm_render_api.present = [&]() { ctx.present(); };

    while (1) {
        Joystick::joystick_state_t jst = ctx.joystick.get_joystick_state();
        Button::button_state_t tbs = ctx.type_button.get_button_state();
        Button::button_state_t bbs = ctx.back_button.get_button_state();
        Button::button_state_t ebs = ctx.enter_button.get_button_state();

        ui::InputSnapshot confirm_input;
        confirm_input.left_edge = jst.pushed_left_edge;
        confirm_input.right_edge = jst.pushed_right_edge;
        confirm_input.up_edge = jst.pushed_up_edge;
        confirm_input.down_edge = jst.pushed_down_edge;
        confirm_input.type_pressed = tbs.pushed;
        confirm_input.enter_pressed = ebs.pushed;
        confirm_input.back_pressed = bbs.pushed;

        auto cmd = confirm_presenter.handle_input(confirm_input);
        confirm_renderer.render(confirm_state, confirm_render_api);

        if (cmd == ui::confirmdialog::Presenter::Command::Cancel) {
            break;
        }
        if (cmd == ui::confirmdialog::Presenter::Command::Confirm) {
            if (confirm_state.selected == 1) {
                ctx.sprite.fillRect(0, 0, 128, 64, 0);
                ctx.sprite.setTextColor(0xFFFFFFu, 0x000000u);
                ctx.sprite.drawCenterString("Resetting...", 64, 22);
                ctx.sprite.drawCenterString("Erasing NVS", 64, 40);
                ctx.present();
                ctx.sprite.fillRect(0, 0, 128, 64, 0);
                ctx.sprite.drawCenterString("Rebooting...", 64, 30);
                ctx.present();
                vTaskDelay(200 / portTICK_PERIOD_MS);
                mobus_request_factory_reset();
            }
            break;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

}  // namespace ui::settingrunners
