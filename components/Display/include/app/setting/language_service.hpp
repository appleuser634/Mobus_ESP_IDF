#pragma once

#include <string>

#include "ui_strings.hpp"

namespace app::languagesetting {

inline int current_index() {
    return (ui::current_lang() == ui::Lang::Ja) ? 1 : 0;
}

inline const char* index_to_nvs_value(int index) {
    return (index == 0) ? "en" : "ja";
}

}  // namespace app::languagesetting
