#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <nvs_rw.hpp>

namespace sound_settings {
namespace detail {
struct State {
    bool loaded = false;
    bool enabled = true;
    float volume = 1.0f;  // default to full volume
};

inline State& state() {
    static State s;
    return s;
}

inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

inline void persist_state();

inline void ensure_loaded() {
    State& s = state();
    if (s.loaded) return;

    std::string enabled = get_nvs((char*)"sound_enabled");
    if (!enabled.empty()) {
        if (enabled == "false" || enabled == "0") {
            s.enabled = false;
        } else {
            s.enabled = true;
        }
    }

    std::string vol = get_nvs((char*)"sound_volume");
    if (!vol.empty()) {
        float parsed = std::strtof(vol.c_str(), nullptr);
        if (!std::isnan(parsed)) {
            s.volume = clampf(parsed, 0.0f, 1.0f);
        }
    }

    s.loaded = true;
}

inline void persist_state() {
    State& s = state();
    save_nvs((char*)"sound_enabled", s.enabled ? "true" : "false");
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.3f", s.volume);
    save_nvs((char*)"sound_volume", std::string(buf));
}

}  // namespace detail

inline bool enabled() {
    detail::ensure_loaded();
    return detail::state().enabled;
}

inline void set_enabled(bool value, bool persist = true) {
    detail::ensure_loaded();
    detail::State& s = detail::state();
    if (s.enabled == value) return;
    s.enabled = value;
    if (persist) detail::persist_state();
}

inline float volume() {
    detail::ensure_loaded();
    return detail::state().volume;
}

inline void set_volume(float value, bool persist = true) {
    detail::ensure_loaded();
    float clamped = detail::clampf(value, 0.0f, 1.0f);
    detail::State& s = detail::state();
    if (std::fabs(s.volume - clamped) < 0.001f) return;
    s.volume = clamped;
    if (persist) detail::persist_state();
}

inline void reload_from_storage() {
    detail::state() = detail::State{};
    detail::ensure_loaded();
}

inline void persist() {
    detail::ensure_loaded();
    detail::persist_state();
}

}  // namespace sound_settings
