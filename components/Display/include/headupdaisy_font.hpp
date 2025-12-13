#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

extern const uint8_t fontHeadUpDaisy14x16_data[];
extern const uint8_t fontHeadUpDaisy14x8_data[];

namespace mobus_fonts {
inline const lgfx::U8g2font &HeadUpDaisy14x16() {
    static const lgfx::U8g2font font(fontHeadUpDaisy14x16_data);
    return font;
}

inline const lgfx::U8g2font &HeadUpDaisy14x8() {
    static const lgfx::U8g2font font(fontHeadUpDaisy14x8_data);
    return font;
}
}  // namespace mobus_fonts
