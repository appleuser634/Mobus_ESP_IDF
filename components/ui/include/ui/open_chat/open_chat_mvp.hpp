#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ui/core/input_adapter.hpp"

namespace ui::openchat {

inline void remove_last_utf8_codepoint(std::string &s) {
    if (s.empty()) return;
    size_t i = s.size();
    do {
        --i;
    } while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0u) == 0x80u);
    s.erase(i);
}

struct ComposerViewState {
    std::string header;
    std::string message_text;
    std::string morse_text;
    std::string preview;
    std::string footer = "Enter=Send Left+Type=Del";
};

enum class ComposerCommand {
    None,
    Cancel,
    Confirm,
};

class ComposerPresenter {
   public:
    explicit ComposerPresenter(ComposerViewState &state) : state_(state) {}

    bool handle_type_push(char push_type, bool back_pushing,
                          const std::string &short_push_text,
                          const std::string &long_push_text) {
        if (back_pushing) return false;
        if (push_type == 's') {
            state_.morse_text += short_push_text;
            state_.preview.clear();
            return true;
        }
        if (push_type == 'l') {
            state_.morse_text += long_push_text;
            state_.preview.clear();
            return true;
        }
        return false;
    }

    bool resolve_morse_release(bool type_pushing, int64_t release_sec,
                               const std::map<std::string, std::string> &morse_map) {
        if (type_pushing || release_sec <= 200000 || state_.morse_text.empty()) {
            return false;
        }
        auto it = morse_map.find(state_.morse_text);
        if (it != morse_map.end()) {
            state_.message_text += it->second;
            state_.preview = it->second;
        } else {
            state_.preview = "?";
        }
        state_.morse_text.clear();
        return true;
    }

    bool handle_delete() {
        if (state_.message_text.empty()) return false;
        remove_last_utf8_codepoint(state_.message_text);
        state_.preview.clear();
        return true;
    }

    bool handle_up() {
        state_.message_text.push_back('\n');
        state_.preview.clear();
        return true;
    }

    bool handle_down() {
        state_.message_text.push_back(' ');
        state_.preview.clear();
        return true;
    }

    ComposerCommand resolve_command(const ui::InputSnapshot &input) const {
        if (input.back_pressed && state_.message_text.empty()) {
            return ComposerCommand::Cancel;
        }
        if (input.enter_pressed && !state_.message_text.empty()) {
            return ComposerCommand::Confirm;
        }
        return ComposerCommand::None;
    }

   private:
    ComposerViewState &state_;
};

struct ComposerRenderApi {
    std::function<void()> begin_frame;
    std::function<void(const std::string &)> draw_header;
    std::function<void(const std::string &)> draw_message;
    std::function<void(const std::string &, const std::string &)> draw_morse;
    std::function<void(const std::string &)> draw_footer;
    std::function<void()> present;
};

class ComposerRenderer {
   public:
    void render(const ComposerViewState &state, const ComposerRenderApi &api) {
        if (api.begin_frame) api.begin_frame();
        if (api.draw_header) api.draw_header(state.header);
        if (api.draw_message) api.draw_message(state.message_text);
        if (api.draw_morse) api.draw_morse(state.morse_text, state.preview);
        if (api.draw_footer) api.draw_footer(state.footer);
        if (api.present) api.present();
    }
};

struct RoomSelectorViewState {
    std::string title = "Open Chat";
    std::vector<std::string> rooms;
    int selected = 0;
    std::string footer = "Type:Join  Back:Exit";
};

enum class RoomSelectorCommand {
    None,
    EnterRoom,
    Exit,
};

class RoomSelectorPresenter {
   public:
    explicit RoomSelectorPresenter(RoomSelectorViewState &state) : state_(state) {}

    bool move(const ui::InputSnapshot &input) {
        if (state_.rooms.empty()) return false;
        const int count = static_cast<int>(state_.rooms.size());
        const int prev = state_.selected;
        if (input.down_edge) {
            state_.selected = (state_.selected + 1) % count;
        } else if (input.up_edge) {
            state_.selected = (state_.selected - 1 + count) % count;
        }
        return prev != state_.selected;
    }

    RoomSelectorCommand resolve_command(const ui::InputSnapshot &input) const {
        if (input.back_pressed) return RoomSelectorCommand::Exit;
        if (input.type_pressed) return RoomSelectorCommand::EnterRoom;
        return RoomSelectorCommand::None;
    }

   private:
    RoomSelectorViewState &state_;
};

struct RoomSelectorRenderApi {
    std::function<void()> begin_frame;
    std::function<void(const std::string &)> draw_header;
    std::function<void(int, const std::string &, bool)> draw_row;
    std::function<void(const std::string &)> draw_footer;
    std::function<void()> present;
};

class RoomSelectorRenderer {
   public:
    void render(const RoomSelectorViewState &state,
                const RoomSelectorRenderApi &api) {
        if (api.begin_frame) api.begin_frame();
        if (api.draw_header) api.draw_header(state.title);
        if (api.draw_row) {
            for (size_t i = 0; i < state.rooms.size(); ++i) {
                api.draw_row(static_cast<int>(i), state.rooms[i],
                             static_cast<int>(i) == state.selected);
            }
        }
        if (api.draw_footer) api.draw_footer(state.footer);
        if (api.present) api.present();
    }
};

}  // namespace ui::openchat
