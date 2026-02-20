#include <cstdio>
#include <iterator>
#include <string>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <array>
#include <functional>
#include <vector>
#include <cctype>
#include <memory>
#include <unordered_set>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <ArduinoJson.h>
#include <button.h>
#include <max98357a.h>
#include <gb_synth.hpp>
#include <boot_sounds.hpp>
#include <sound_settings.hpp>
#include <images.hpp>
#include <haptic_motor.hpp>
#include <joystick_haptics.hpp>
#include <http_client.hpp>
#include <app/contact/actions_service.hpp>
#include <app/contact/fetch_service.hpp>
#include <app/contact/menu_usecase.hpp>
#include <app/contact/menu_view_service.hpp>
#include <app/setting/bluetooth_pairing_service.hpp>
#include <app/menu/navigation_usecase.hpp>
#include <app/menu/status_service.hpp>
#include <app/setting/boot_sound_service.hpp>
#include <app/setting/firmware_info_service.hpp>
#include <app/setting/language_service.hpp>
#include <app/setting/ota_manifest_service.hpp>
#include <app/setting/action_router.hpp>
#include <app/setting/task_runner.hpp>
#include <app/setting/action_service.hpp>
#include <app/setting/menu_view_service.hpp>
#include <app/setting/menu_label_service.hpp>
#include <nvs_rw.hpp>
#include <app/contact/domain.hpp>
#include <headupdaisy_font.hpp>
#include <misaki_font.hpp>
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "ui_strings.hpp"
#include "ui/contact/book_mvp.hpp"
#include "ui/common/confirm_dialog.hpp"
#include "ui/common/choice_dialog_mvp.hpp"
#include "ui/contact/action_runners.hpp"
#include "ui/contact/pending_mvp.hpp"
#include "ui/setting/boot_sound_dialog.hpp"
#include "ui/setting/bluetooth_pairing_mvp.hpp"
#include "ui/setting/firmware_info_dialog.hpp"
#include "ui/core/input_adapter.hpp"
#include "ui/setting/language_dialog.hpp"
#include "ui/menu/display_mvp.hpp"
#include "ui/contact/message_box_mvp.hpp"
#include "ui/setting/menu_mvp.hpp"
#include "ui/setting/dialog_runners.hpp"
#include "ui/setting/sound_settings_mvp.hpp"
#include "ui/common/text_modal.hpp"
#include "ui/core/screen.hpp"
#include "ui/common/status_panel.hpp"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>

#pragma once

extern "C" void mobus_request_factory_reset();
extern "C" void mobus_request_ota_minimal_mode();

inline std::string resolve_chat_backend_id(const std::string &fallback);

inline bool wifi_is_connected() {
    constexpr EventBits_t kWifiConnectedBit = BIT0;
    if (s_wifi_event_group) {
        EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
        if (bits & kWifiConnectedBit) return true;
    }
    wifi_ap_record_t ap = {};
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_SSD1306
        _panel_instance;  // SSD1309 は SH110x ドライバで動作可能
    lgfx::Bus_SPI _bus_instance;

   public:
    LGFX(void) {
        {  // SPIバス設定
            auto cfg = _bus_instance.config();

            cfg.spi_host =
                SPI3_HOST;     // ESP32-S3では SPI2_HOST または SPI3_HOST
            cfg.spi_mode = 0;  // SSD1309のSPIモードは0

            cfg.freq_write = 400000;  // 8MHz程度が安定（モジュールによる）
            cfg.freq_read = 0;        // dev/ 読み取り不要なため 0
            cfg.spi_3wire = false;    // DCピン使用するので false
            cfg.use_lock = true;

            // 接続ピン（あなたの配線に基づく）
            cfg.pin_sclk = 2;   // SCK
            cfg.pin_mosi = 13;  // MOSI
            cfg.pin_miso = -1;  // MISO 使用しない
            cfg.pin_dc = 1;     // DC

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {  // パネル設定
            auto cfg = _panel_instance.config();

            cfg.pin_cs = 11;    // CS
            cfg.pin_rst = 12;   // RST
            cfg.pin_busy = -1;  // BUSYピン未使用

            cfg.panel_width = 128;
            cfg.panel_height = 64;
            cfg.memory_width = 128;
            cfg.memory_height = 64;

            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 2;

            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;  // 読み取り機能なし
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;

            _panel_instance.config(cfg);
        }

        setPanel(&_panel_instance);
    }
};

static LGFX lcd;
static LGFX_Sprite sprite(
    &lcd);  // スプライトを使う場合はLGFX_Spriteのインスタンスを作成。

namespace {
struct SpriteInit {
    SpriteInit() { sprite.setPsram(true); }
};
static SpriteInit sprite_init;

inline uint16_t utf8_to_u16_codepoint(const std::string &utf8) {
    if (utf8.empty()) return 0;
    const uint8_t b0 = static_cast<uint8_t>(utf8[0]);
    if ((b0 & 0x80u) == 0) return b0;
    if ((b0 & 0xE0u) == 0xC0u && utf8.size() >= 2) {
        const uint8_t b1 = static_cast<uint8_t>(utf8[1]);
        return static_cast<uint16_t>(((b0 & 0x1Fu) << 6) | (b1 & 0x3Fu));
    }
    if ((b0 & 0xF0u) == 0xE0u && utf8.size() >= 3) {
        const uint8_t b1 = static_cast<uint8_t>(utf8[1]);
        const uint8_t b2 = static_cast<uint8_t>(utf8[2]);
        return static_cast<uint16_t>(((b0 & 0x0Fu) << 12) |
                                     ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu));
    }
    return 0;
}

inline const lgfx::IFont *select_display_font(const lgfx::IFont *base_font,
                                              const std::string &utf8_char) {
    if (!base_font) base_font = &fonts::Font0;
    const uint16_t cp = utf8_to_u16_codepoint(utf8_char);
    if (cp == 0) return base_font;

    // Only switch fonts for Japanese blocks (keep ASCII on the base font).
    const bool is_japanese_block = (cp >= 0x3000u && cp <= 0x30FFu);
    if (!is_japanese_block) return base_font;

    lgfx::FontMetrics base_metrics = {};
    base_font->getDefaultMetric(&base_metrics);
    const bool prefer_8px = base_metrics.height <= 8;
    const bool prefer_16px = base_metrics.height >= 15;

    const bool is_katakana = (cp >= 0x30A0u && cp <= 0x30FFu);
    if (prefer_8px && is_katakana) {
        lgfx::FontMetrics metrics = {};
        const auto &misaki = mobus_fonts::MisakiGothic8();
        if (misaki.updateFontMetric(&metrics, cp)) return &misaki;
    }
    if (prefer_16px && is_katakana) {
        lgfx::FontMetrics metrics = {};
        const auto &misaki = mobus_fonts::MisakiGothic16();
        if (misaki.updateFontMetric(&metrics, cp)) return &misaki;
    }

    const lgfx::IFont *fallback =
        prefer_8px
            ? static_cast<const lgfx::IFont *>(&fonts::lgfxJapanGothic_8)
            : static_cast<const lgfx::IFont *>(&fonts::lgfxJapanGothic_16);

    lgfx::FontMetrics metrics = {};
    const auto &headup = prefer_8px ? mobus_fonts::HeadUpDaisy14x8()
                                    : mobus_fonts::HeadUpDaisy14x16();
    if (headup.updateFontMetric(&metrics, cp)) return &headup;

    return fallback;
}

inline const lgfx::IFont *select_ime_font(int input_lang,
                                          const lgfx::IFont *base_font,
                                          const std::string &utf8_char) {
    if (!base_font) base_font = &fonts::Font0;
    if (input_lang != 1) return base_font;
    return select_display_font(base_font, utf8_char);
}

inline const lgfx::IFont *select_chat_input_font(const std::string &utf8_char) {
    const uint16_t cp = utf8_to_u16_codepoint(utf8_char);
    const bool is_japanese_block = (cp >= 0x3000u && cp <= 0x30FFu);
    if (!is_japanese_block) {
        return &fonts::Font2;
    }
    return &fonts::lgfxJapanGothic_12;
}

inline void wrap_char_set_index(int &select_y_index, int char_set_length) {
    if (char_set_length <= 0) {
        select_y_index = 0;
        return;
    }
    if (select_y_index >= char_set_length) select_y_index = 0;
    if (select_y_index < 0) select_y_index = char_set_length - 1;
}

inline int wrap_char_index(int &select_x_index, const char *row_chars,
                           bool enable_delete_button) {
    int row_len = static_cast<int>(strlen(row_chars));
    if (row_len <= 0) row_len = 1;
    const int selectable_count = row_len + (enable_delete_button ? 1 : 0);
    if (select_x_index >= selectable_count) select_x_index = 0;
    if (select_x_index < 0) select_x_index = selectable_count - 1;
    return row_len;
}

inline bool apply_char_or_delete(std::string &type_text, const char *row_chars,
                                 int select_x_index, size_t max_len,
                                 bool enable_delete_button) {
    const int row_len = static_cast<int>(strlen(row_chars));
    if (enable_delete_button && select_x_index == row_len) {
        if (!type_text.empty()) {
            type_text.pop_back();
            return true;
        }
        return false;
    }
    if (row_len <= 0) return false;
    if (select_x_index < 0 || select_x_index >= row_len) return false;
    if (max_len != 0 && type_text.size() >= max_len) return false;
    type_text.push_back(row_chars[select_x_index]);
    return true;
}

inline void draw_char_selector_row(const char *row_chars, int y,
                                   int select_x_index,
                                   bool enable_delete_button) {
    const int kCharSpacing = 8;
    int draw_x = 0;
    for (int i = 0; row_chars[i] != '\0'; i++) {
        sprite.setCursor(draw_x, y);
        if (select_x_index == i) {
            sprite.setTextColor(0x000000u, 0xFFFFFFu);
        } else {
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
        }
        sprite.print(row_chars[i]);
        draw_x += kCharSpacing;
    }

    if (!enable_delete_button) return;

    const int row_len = static_cast<int>(strlen(row_chars));
    const bool selected = (select_x_index == row_len);
    const uint16_t fg = selected ? 0x000000u : 0xFFFFFFu;
    const uint16_t bg = selected ? 0xFFFFFFu : 0x000000u;
    const int btn_top = y - 1;  // keep top aligned with previous design
    const int btn_bottom = 60;  // slightly shorter for better balance
    const int btn_left = 118;
    const int btn_right = 127;
    const int tip_x = 114;
    const int tip_y = (btn_top + btn_bottom) / 2;

    // Fill delete button shape (rect body + arrow tip)
    sprite.fillRect(btn_left, btn_top, btn_right - btn_left + 1,
                    btn_bottom - btn_top + 1, bg);
    sprite.fillTriangle(tip_x, tip_y, btn_left, btn_top, btn_left, btn_bottom,
                        bg);

    // Draw outline with pointed left edge
    sprite.drawLine(btn_left, btn_top, btn_right, btn_top, fg);
    sprite.drawLine(btn_right, btn_top, btn_right, btn_bottom, fg);
    sprite.drawLine(btn_right, btn_bottom, btn_left, btn_bottom, fg);
    sprite.drawLine(btn_left, btn_top, tip_x, tip_y, fg);
    sprite.drawLine(tip_x, tip_y, btn_left, btn_bottom, fg);

    // Draw "X" icon at center of the button
    const int cx = (btn_left + btn_right) / 2;
    const int cy = tip_y;
    sprite.drawLine(cx - 2, cy - 2, cx + 2, cy + 2, fg);
    sprite.drawLine(cx + 2, cy - 2, cx - 2, cy + 2, fg);
}

struct SpriteState {
    bool ready = false;
    int width = 0;
    int height = 0;
    int depth = 0;
};

bool g_oled_ready = false;
SpriteState g_sprite_state;

inline void log_memory_state(const char *reason, const char *context) {
    ESP_LOGE(TAG, "%s (%s free=%u, largest=%u, psram=%u)", reason, context,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                           MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(
                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

bool ensure_lcd_ready(const char *context) {
    if (g_oled_ready) return true;
    sprite.deleteSprite();
    g_sprite_state.ready = false;
    if (!lcd.init()) {
        g_oled_ready = false;
        log_memory_state("[OLED] lcd.init failed", context);
        return false;
    }
    lcd.clearDisplay();
    lcd.setRotation(2);
    lcd.fillScreen(0x000000u);
    g_oled_ready = true;
    return true;
}

bool ensure_sprite_surface(int width, int height, int depth,
                           const char *context) {
    if (!ensure_lcd_ready(context)) return false;
    if (g_sprite_state.ready && sprite.getBuffer() != nullptr &&
        g_sprite_state.width == width && g_sprite_state.height == height &&
        g_sprite_state.depth == depth) {
        return true;
    }

    sprite.deleteSprite();
    sprite.setPsram(true);
    sprite.setColorDepth(depth);
    if (!sprite.createSprite(width, height)) {
        g_sprite_state.ready = false;
        ESP_LOGE(
            TAG, "[OLED] sprite.createSprite failed (%s, free=%u, largest=%u)",
            context,
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DEFAULT)),
            static_cast<unsigned>(
                heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)));
        return false;
    }
    g_sprite_state.ready = true;
    g_sprite_state.width = width;
    g_sprite_state.height = height;
    g_sprite_state.depth = depth;
    return true;
}

inline void push_sprite_safe(int32_t x, int32_t y) {
    if (!g_oled_ready) return;
    if (sprite.getBuffer() == nullptr) return;
    sprite.pushSprite(&lcd, x, y);
}

StackType_t *allocate_internal_stack(StackType_t *&slot, size_t words,
                                     const char *label) {
    if (slot) return slot;
    size_t bytes = words * sizeof(StackType_t);
    slot = static_cast<StackType_t *>(
        heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!slot) {
        ESP_LOGE(
            TAG, "[OLED] stack alloc failed (%s, bytes=%u free=%u largest=%u)",
            label, static_cast<unsigned>(bytes),
            static_cast<unsigned>(
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    }
    return slot;
}

}  // namespace
#include <chat_api.hpp>
#include <ota_client.hpp>
#include <mqtt_runtime.h>
#include <ble_uart.hpp>
#include "esp_app_desc.h"
#include "esp_ota_ops.h"

// UTF-8 の1文字の先頭バイト数を調べる（UTF-8のみ対応）
int utf8_char_length(unsigned char ch) {
    if ((ch & 0x80) == 0x00) return 1;  // ASCII
    if ((ch & 0xE0) == 0xC0) return 2;  // 2バイト文字
    if ((ch & 0xF0) == 0xE0) return 3;  // 3バイト文字（例：カタカナ）
    if ((ch & 0xF8) == 0xF0) return 4;  // 4バイト文字（絵文字など）
    return 1;
}

void remove_last_utf8_char(std::string &str) {
    if (str.empty()) return;

    // 末尾の先頭バイトを探す
    size_t pos = str.size();
    while (pos > 0) {
        --pos;
        if ((str[pos] & 0xC0) != 0x80) break;  // UTF-8の先頭バイト検出
    }

    str.erase(pos);
}
