#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ui::talk {

struct InputViewState {
    std::string morse_text;
    std::string message_text;
    std::string alphabet_text;
    int input_lang = -1;
    size_t input_switch_pos = 0;
};

class InputPresenter {
   public:
    explicit InputPresenter(InputViewState &state) : state_(state) {}

    void handle_type_push(char push_type, bool back_pushing,
                          const std::string &short_push_text,
                          const std::string &long_push_text) {
        if (back_pushing) return;
        if (push_type == 's') {
            state_.morse_text += short_push_text;
        } else if (push_type == 'l') {
            state_.morse_text += long_push_text;
        }
    }

    void decode_release(int64_t release_sec, bool joystick_up,
                        const std::map<std::string, std::string> &morse_code) {
        if (release_sec <= 200000) return;
        if (state_.morse_text.empty()) return;
        auto it = morse_code.find(state_.morse_text);
        if (it != morse_code.end()) {
            state_.alphabet_text = it->second;
        }
        if (joystick_up) {
            std::transform(state_.alphabet_text.begin(), state_.alphabet_text.end(),
                           state_.alphabet_text.begin(), ::toupper);
        }
        state_.morse_text.clear();
    }

    void append_newline() { state_.message_text += "\n"; }

    void delete_last_char() {
        if (state_.message_text.empty()) return;
        size_t i = state_.message_text.size();
        do {
            --i;
        } while (i > 0 &&
                 (static_cast<unsigned char>(state_.message_text[i]) & 0xC0u) ==
                     0x80u);
        state_.message_text.erase(i);
        state_.input_switch_pos = state_.message_text.size();
    }

    void toggle_language() {
        state_.input_lang *= -1;
        state_.input_switch_pos = state_.message_text.size();
    }

    std::string display_text(bool cursor_on) const {
        std::string out = state_.message_text + state_.morse_text + state_.alphabet_text;
        if (cursor_on) out += "|";
        return out;
    }

    void commit_alphabet(const std::vector<std::pair<std::string, std::string>> &romaji_kana) {
        state_.message_text += state_.alphabet_text;
        if (!state_.alphabet_text.empty() && state_.input_lang == 1) {
            size_t safe_pos = state_.input_switch_pos;
            if (safe_pos > state_.message_text.size()) safe_pos = state_.message_text.size();
            std::string target = state_.message_text.substr(safe_pos);
            state_.message_text = state_.message_text.substr(0, safe_pos) +
                                  transliterate(target, romaji_kana);
        }
        state_.alphabet_text.clear();
    }

   private:
    static std::string transliterate(
        const std::string &src,
        const std::vector<std::pair<std::string, std::string>> &romaji_kana) {
        std::string out;
        out.reserve(src.size() * 3);
        size_t i = 0;
        while (i < src.size()) {
            size_t best_len = 0;
            const std::string *best_value = nullptr;
            for (const auto &kv : romaji_kana) {
                const std::string &k = kv.first;
                if (k.empty()) continue;
                if (k.size() <= src.size() - i && src.compare(i, k.size(), k) == 0 &&
                    k.size() > best_len) {
                    best_len = k.size();
                    best_value = &kv.second;
                }
            }
            if (best_value) {
                out += *best_value;
                i += best_len;
            } else {
                out += src[i++];
            }
        }
        return out;
    }

    InputViewState &state_;
};

}  // namespace ui::talk
