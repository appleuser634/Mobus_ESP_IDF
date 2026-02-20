#define Oled MvpOled
#define MenuDisplay MvpMenuDisplay
#define ProfileSetting MvpProfileSetting
#include "../components/display/src/runtime/bridge_entry.hpp"
#undef ProfileSetting
#undef MenuDisplay
#undef Oled

namespace {
MvpOled& mvp_oled() {
    static MvpOled instance;
    return instance;
}

MvpMenuDisplay& mvp_menu() {
    static MvpMenuDisplay instance;
    return instance;
}
}  // namespace

void Oled::BootDisplay() { mvp_oled().BootDisplay(); }
void Oled::WatchDisplay() { mvp_oled().WatchDisplay(); }
void Oled::RecvNotif() { mvp_oled().RecvNotif(); }
void Oled::ShowImage(const unsigned char img[]) { mvp_oled().ShowImage(img); }

void MenuDisplay::start_menu_task() { mvp_menu().start_menu_task(); }
void ProfileSetting::profile_setting_task() {
    MvpProfileSetting::profile_setting_task();
}

void display_render_ota_progress(int percent, const char* phase) {
    if (!ensure_sprite_surface(128, 64, 1, "OTAProgress")) return;
    sprite.fillRect(0, 0, 128, 64, 0);
    sprite.setFont(&fonts::Font2);
    sprite.setTextColor(1, 0);
    sprite.drawCenterString("OTA Update", 64, 2);

    if (strcmp(phase, "retry") == 0) {
        sprite.drawCenterString("Retrying...", 64, 18);
    } else if (strcmp(phase, "validating_failed") == 0) {
        sprite.drawCenterString("Validate fallback", 64, 18);
    } else if (strcmp(phase, "rebooting") == 0) {
        sprite.drawCenterString("Rebooting...", 64, 18);
    } else if (strcmp(phase, "failed") == 0) {
        sprite.drawCenterString("Download failed", 64, 18);
    } else {
        sprite.drawCenterString("Downloading...", 64, 18);
    }

    const int bar_x = 10;
    const int bar_y = 36;
    const int bar_w = 108;
    const int bar_h = 12;
    sprite.drawRect(bar_x, bar_y, bar_w, bar_h, 1);
    const int fill_w = ((bar_w - 2) * percent) / 100;
    if (fill_w > 0) {
        sprite.fillRect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, 1);
    }

    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    sprite.setTextColor(1, 0);
    sprite.drawCenterString(pct, 64, 50);
    push_sprite_safe(0, 0);
}
