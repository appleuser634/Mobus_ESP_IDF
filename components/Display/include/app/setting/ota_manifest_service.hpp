#pragma once

#include <string>
#include <vector>

#include <nvs_rw.hpp>

namespace app::otamanifest {

inline std::string current_manifest_url() {
    std::string mf = get_nvs((char*)"ota_manifest");
    if (mf.empty()) {
        mf = "https://mimoc.jp/api/firmware/latest?device=esp32s3&channel=stable";
    }
    return mf;
}

inline std::vector<std::string> wrap_lines(const std::string& text,
                                           int max_chars,
                                           int max_lines) {
    std::vector<std::string> lines;
    if (max_chars <= 0 || max_lines <= 0) return lines;

    for (size_t p = 0; p < text.size(); p += static_cast<size_t>(max_chars)) {
        lines.emplace_back(text.substr(p, static_cast<size_t>(max_chars)));
        if (static_cast<int>(lines.size()) >= max_lines) break;
    }
    return lines;
}

}  // namespace app::otamanifest
