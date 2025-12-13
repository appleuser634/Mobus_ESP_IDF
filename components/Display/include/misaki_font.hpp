#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

extern const uint8_t fontMisakiGothic16_data[];
extern const uint8_t fontMisakiGothic8_data[];

namespace mobus_fonts {
inline const lgfx::U8g2font &MisakiGothic16() {
    static const lgfx::U8g2font font(fontMisakiGothic16_data);
    return font;
}

inline const lgfx::U8g2font &MisakiGothic8() {
    static const lgfx::U8g2font font(fontMisakiGothic8_data);
    return font;
}
}  // namespace mobus_fonts
