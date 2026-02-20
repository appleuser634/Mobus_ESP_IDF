#pragma once

#include <string>
#include <vector>

#include <boot_sounds.hpp>
#include <esp_timer.h>
#include <nvs_rw.hpp>

namespace app::bootsound {

inline std::vector<std::string> available_options() {
    std::vector<std::string> opts = {"cute", "majestic", "gb", "random"};
    if (!get_nvs((char*)"song1").empty()) opts.push_back("song1");
    if (!get_nvs((char*)"song2").empty()) opts.push_back("song2");
    if (!get_nvs((char*)"song3").empty()) opts.push_back("song3");
    return opts;
}

inline int index_of(const std::vector<std::string>& opts,
                    const std::string& value) {
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        if (opts[i] == value) return i;
    }
    return 0;
}

inline std::string display_name(const std::string& id) {
    if (id == "majestic") return "Majestic";
    if (id == "gb") return "GB Synth";
    if (id == "random") return "Random";
    if (id == "song1") return "Song 1";
    if (id == "song2") return "Song 2";
    if (id == "song3") return "Song 3";
    return "Cute";
}

inline void play_preview(const std::string& id) {
    auto& sp = audio::speaker();
    if (id == "majestic") {
        boot_sounds::play_majestic(sp, 0.5f);
    } else if (id == "gb") {
        boot_sounds::play_gb(sp, 0.9f);
    } else if (id == "song1") {
        boot_sounds::play_song(sp, 1, 0.9f);
    } else if (id == "song2") {
        boot_sounds::play_song(sp, 2, 0.9f);
    } else if (id == "song3") {
        boot_sounds::play_song(sp, 3, 0.9f);
    } else if (id == "random") {
        uint32_t r = static_cast<uint32_t>(esp_timer_get_time() & 3);
        if (r == 0) {
            boot_sounds::play_cute(sp, 0.5f);
        } else if (r == 1) {
            boot_sounds::play_majestic(sp, 0.5f);
        } else {
            boot_sounds::play_gb(sp, 0.9f);
        }
    } else {
        boot_sounds::play_cute(sp, 0.5f);
    }
}

}  // namespace app::bootsound
