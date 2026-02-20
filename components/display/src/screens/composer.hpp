#pragma once

class Composer {
   public:
    static constexpr uint32_t kTaskStackWords = 8384;
    static bool running_flag;
    static TaskHandle_t s_play_task;
    static volatile bool s_abort;
    static volatile int s_play_pos_step;
    static long long s_pitch_popup_until;
    static char s_pitch_popup_text[16];
    static volatile int s_popup_kind;  // 0: note, 1: noise
    static volatile int s_popup_val;   // midi or noise index

    void start_composer_task() {
        if (s_task_handle) {
            ESP_LOGW(TAG, "composer_task already running");
            return;
        }
        if (!allocate_internal_stack(s_task_stack_, kTaskStackWords,
                                     "Composer")) {
            ESP_LOGE(TAG, "Failed to alloc composer stack");
            return;
        }
        s_task_handle = xTaskCreateStaticPinnedToCore(
            &composer_task, "composer_task", kTaskStackWords, NULL, 6,
            s_task_stack_, &s_task_buffer_, 1);
        if (!s_task_handle) {
            ESP_LOGE(TAG, "Failed to start composer_task (free_heap=%u)",
                     static_cast<unsigned>(heap_caps_get_free_size(
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        }
    }

    static void composer_task(void *pv) {
        lcd.init();
        lcd.setRotation(2);
        sprite.setColorDepth(8);
        sprite.setFont(&fonts::Font2);
        sprite.setTextWrap(false);
        sprite.createSprite(lcd.width(), lcd.height());

        Joystick joystick;
        Button type_button(GPIO_NUM_46);
        Button back_button(GPIO_NUM_3);
        Button enter_button(GPIO_NUM_5);

        // Pattern data (16 steps)
        static constexpr int STEPS = 16;
        int p1[STEPS];
        int p2[STEPS];
        int nz[STEPS];  // -1 off, else 0..7
        for (int i = 0; i < STEPS; ++i) {
            p1[i] = -1;
            p2[i] = -1;
            nz[i] = -1;
        }
        // Small default groove
        p1[0] = 60;
        p1[4] = 64;
        p1[8] = 67;
        p1[12] = 72;
        nz[4] = 3;
        nz[12] = 3;

        int tempo = 120;
        int cur_chan = 0;  // 0:SIN 1:SQR 2:NOI
        int cur_step = 0;
        int cur_note_p1 = 60;      // C4
        int cur_note_p2 = 67;      // G4
        int cur_noise = 1;         // default drum: SN (0=HH,1=SN,2=BD)
        int duty_idx1 = 2;         // 50%
        int duty_idx2 = 2;         // 50%
        bool noise_short = false;  // 7-bit flavor
        const float duties[4] = {0.125f, 0.25f, 0.5f, 0.75f};

        // playback control
        s_play_task = nullptr;
        s_abort = false;

        auto draw = [&]() {
            sprite.fillRect(0, 0, 128, 64, 0);
            sprite.setTextColor(0xFFFFFFu, 0x000000u);
            sprite.setFont(&fonts::Font2);

            // Header: BPM + play state only
            char hdr[40];
            bool playing = (s_play_task != nullptr) || (s_play_pos_step >= 0);
            snprintf(hdr, sizeof(hdr), "BPM:%d %s", tempo,
                     playing ? "PLAY" : "STOP");
            sprite.setCursor(2, 0);
            sprite.print(hdr);

            // Pitch readout (note names for selected step per channel)
            auto note_name = [](int midi) -> std::string {
                if (midi < 0) return std::string("--");
                static const char *names[12] = {"C",  "C#", "D",  "D#",
                                                "E",  "F",  "F#", "G",
                                                "G#", "A",  "A#", "B"};
                int n = midi % 12;
                if (n < 0) n += 12;
                int oct = midi / 12 - 1;  // MIDI 60 -> C4
                char buf[8];
                snprintf(buf, sizeof(buf), "%s%d", names[n], oct);
                return std::string(buf);
            };
            // remove inline pitch readout; show via popup only

            // Grid geometry (fit 128x64 nicely)
            const int label_w = 12;             // space for icons
            const int x0 = label_w + 2;         // grid left
            const int step_w = 7;               // 16 * 7 = 112px
            const int grid_w = step_w * STEPS;  // 112
            const int row_h = 14;               // larger rows
            const int y0 = 18;             // add a bit of space after header
            const int box_w = step_w - 2;  // 5
            const int box_h = row_h - 3;   // 11

            // Row icons (8x8 area)
            auto draw_icon = [&](int row, int type) {
                int ix = 2;
                int iy = y0 + row * row_h + (row_h - 8) / 2;
                if (type == 0) {
                    // SIN: approximate sine curve with small polyline
                    int px = ix;
                    int py = iy + 4;
                    for (int k = 0; k < 7; ++k) {
                        int nx = ix + k + 1;
                        // simple quarter-wave shape in 8px
                        float t = (k + 1) / 7.0f;
                        int ny = iy + 4 - (int)(3.0f * sinf(t * 3.14159f));
                        sprite.drawLine(px, py, nx, ny, 0xFFFF);
                        px = nx;
                        py = ny;
                    }
                } else if (type == 1) {
                    // SQR: square step
                    sprite.drawFastVLine(ix + 2, iy + 1, 6, 0xFFFF);
                    sprite.drawFastHLine(ix + 2, iy + 1, 4, 0xFFFF);
                    sprite.drawFastVLine(ix + 6, iy + 1, 6, 0xFFFF);
                    sprite.drawFastHLine(ix + 2, iy + 7, 4, 0xFFFF);
                } else {
                    // NOI: random-like dots pattern
                    sprite.drawPixel(ix + 1, iy + 2, 0xFFFF);
                    sprite.drawPixel(ix + 3, iy + 1, 0xFFFF);
                    sprite.drawPixel(ix + 5, iy + 4, 0xFFFF);
                    sprite.drawPixel(ix + 2, iy + 6, 0xFFFF);
                    sprite.drawPixel(ix + 6, iy + 3, 0xFFFF);
                }
            };
            draw_icon(0, 0);  // SIN
            draw_icon(1, 1);  // SQR
            draw_icon(2, 2);  // NOI

            // Group separators every 4 steps for readability
            for (int g = 0; g <= STEPS; g += 4) {
                int gx = x0 + g * step_w;
                if (gx >= x0 && gx <= x0 + grid_w) {
                    sprite.drawFastVLine(gx, y0 - 1, row_h * 3 + 2,
                                         0x7BEF /*gray*/);
                }
            }

            auto draw_row = [&](int row, const int *arr, bool noise = false) {
                for (int s = 0; s < STEPS; ++s) {
                    int x = x0 + s * step_w;
                    int y = y0 + row * row_h;
                    bool on = arr[s] >= 0;
                    bool sel = (s == cur_step) &&
                               ((cur_chan == row) || (noise && cur_chan == 2));

                    // Base cell
                    if (on) {
                        // Strongly filled cell (clearly ON)
                        sprite.fillRect(x, y, box_w, box_h, 0xFFFF);
                        if (sel) {
                            // selection: invert small bottom mark
                            sprite.fillRect(x + 1, y + box_h - 3, box_w - 2, 2,
                                            0x0000);
                        }
                    } else {
                        // outline cell
                        sprite.drawRect(x, y, box_w, box_h, 0xFFFF);
                        if (sel) {
                            // selection highlight: inner bar
                            sprite.fillRect(x + 1, y + 1, box_w - 2, box_h - 2,
                                            0x7BEF);
                        }
                    }
                }
            };

            draw_row(0, p1, false);
            draw_row(1, p2, false);
            draw_row(2, nz, true);

            // Draw playhead line when playing
            if (s_play_pos_step >= 0 && s_play_pos_step < STEPS) {
                int px = x0 + ((int)s_play_pos_step) * step_w;
                sprite.drawFastVLine(px, y0 - 1, row_h * 3 + 2, 0xFFFF);
            }

            // No footer: maximize grid area

            // Pitch popup dialog (on recent pitch change)
            if (esp_timer_get_time() < s_pitch_popup_until) {
                int bw = 110, bh = 32;
                int bx = (128 - bw) / 2;
                int by = (64 - bh) / 2;
                sprite.fillRoundRect(bx, by, bw, bh, 4, 0x0000);
                sprite.drawRoundRect(bx, by, bw, bh, 4, 0xFFFF);
                // Small note text
                sprite.setFont(&fonts::Font2);
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                sprite.drawCenterString(s_pitch_popup_text, 64, by + 2);
                // Bar zone
                int barx = bx + 8, bary = by + 18, barw = bw - 16, barh = 8;
                sprite.drawRect(barx, bary, barw, barh, 0xFFFF);
                if (s_popup_kind == 0) {
                    // Single-octave piano keyboard (C..B)
                    // White keys: C D E F G A B (7 keys equally spaced)
                    int ww = barw / 7;
                    if (ww < 4) ww = 4;       // minimal width
                    int rem = barw - ww * 7;  // leftover pixels to distribute
                    int x = barx;
                    int white_x[7];
                    int white_w[7];
                    for (int i = 0; i < 7; i++) {
                        int w = ww + (i < rem ? 1 : 0);
                        white_x[i] = x;
                        white_w[i] = w;
                        // draw white key outline
                        sprite.drawRect(x, bary, w - 1, barh, 0xFFFF);
                        x += w;
                    }
                    // Black keys: C#,D#,F#,G#,A# over corresponding gaps
                    auto draw_black = [&](int left_white_idx) {
                        int lw = left_white_idx;
                        int rw = left_white_idx + 1;
                        if (lw < 0 || rw > 6) return;
                        int cx = (white_x[lw] + white_w[lw] / 2 + white_x[rw] +
                                  white_w[rw] / 2) /
                                 2;
                        int bw = std::min(white_w[lw], white_w[rw]) * 2 / 3;
                        if (bw < 2) bw = 2;
                        int bx2 = cx - bw / 2;
                        int bh = (barh * 3) / 5;
                        sprite.fillRect(bx2, bary, bw, bh, 0xFFFF);
                    };
                    // place black keys at gaps (C# between C-D -> 0, D# -> 1,
                    // skip E-F, F#->3, G#->4, A#->5)
                    draw_black(0);
                    draw_black(1); /*E-F gap none*/
                    draw_black(3);
                    draw_black(4);
                    draw_black(5);
                    // Current note caret (map to single octave by semitone
                    // index)
                    int t = s_popup_val % 12;
                    if (t < 0) t += 12;
                    // Determine caret center
                    int caret_x = barx;
                    auto white_center = [&](int wi) {
                        return white_x[wi] + white_w[wi] / 2;
                    };
                    switch (t) {
                        case 0:
                            caret_x = white_center(0);
                            break;  // C
                        case 2:
                            caret_x = white_center(1);
                            break;  // D
                        case 4:
                            caret_x = white_center(2);
                            break;  // E
                        case 5:
                            caret_x = white_center(3);
                            break;  // F
                        case 7:
                            caret_x = white_center(4);
                            break;  // G
                        case 9:
                            caret_x = white_center(5);
                            break;  // A
                        case 11:
                            caret_x = white_center(6);
                            break;  // B
                        case 1:
                            caret_x = (white_center(0) + white_center(1)) / 2;
                            break;  // C#
                        case 3:
                            caret_x = (white_center(1) + white_center(2)) / 2;
                            break;  // D#
                        case 6:
                            caret_x = (white_center(3) + white_center(4)) / 2;
                            break;  // F#
                        case 8:
                            caret_x = (white_center(4) + white_center(5)) / 2;
                            break;  // G#
                        case 10:
                            caret_x = (white_center(5) + white_center(6)) / 2;
                            break;  // A#
                    }
                    sprite.drawFastVLine(caret_x, bary - 3, 3, 0xFFFF);
                } else {
                    // drums: 3 segments (HH/SN/BD)
                    int segw = barw / 3;
                    for (int i = 0; i < 3; i++) {
                        int rx = barx + i * segw;
                        sprite.drawRect(rx, bary, segw - 1, barh, 0x7BEF);
                        if (i == s_popup_val)
                            sprite.fillRect(rx + 1, bary + 1, segw - 3,
                                            barh - 2, 0xFFFF);
                    }
                }
                sprite.setTextColor(0xFFFFFFu, 0x000000u);
            }

            push_sprite_safe(0, 0);
        };

        draw();

        auto clamp_midi = [](int n) { return std::max(36, std::min(84, n)); };

        while (1) {
            Joystick::joystick_state_t js = joystick.get_joystick_state();
            Button::button_state_t tb = type_button.get_button_state();
            Button::button_state_t bb = back_button.get_button_state();
            Button::button_state_t eb = enter_button.get_button_state();

            // Exit (Back button only)
            if (bb.pushed) {
                break;
            }

            // (Enter+Left/Right for tempo is disabled; use Type+Left/Right)

            // Move step
            if (js.pushed_left_edge && !eb.pushing) {
                cur_step = (cur_step + STEPS - 1) % STEPS;
                draw();
            } else if (js.pushed_right_edge && !tb.pushing && !eb.pushing) {
                cur_step = (cur_step + 1) % STEPS;
                draw();
            }

            // While holding Type: tempo change (Left/Right)
            if (tb.pushing && js.pushed_right_edge) {
                tempo = std::min(440, tempo + 5);
                draw();
            }
            if (tb.pushing && js.pushed_left_edge) {
                tempo = std::max(40, tempo - 5);
                draw();
            }

            // Pitch adjust on up/down (only when holding Type)
            if (tb.pushing && js.pushed_up_edge) {
                if (cur_chan == 0) {
                    if (p1[cur_step] >= 0)
                        p1[cur_step] = clamp_midi(p1[cur_step] + 1);
                    else
                        cur_note_p1 = clamp_midi(cur_note_p1 + 1);
                } else if (cur_chan == 1) {
                    if (p2[cur_step] >= 0)
                        p2[cur_step] = clamp_midi(p2[cur_step] + 1);
                    else
                        cur_note_p2 = clamp_midi(cur_note_p2 + 1);
                } else {
                    if (nz[cur_step] >= 0)
                        nz[cur_step] = std::min(2, nz[cur_step] + 1);
                    else
                        cur_noise = std::min(2, cur_noise + 1);
                }
                // pitch popup
                auto note_name = [](int midi) -> std::string {
                    if (midi < 0) return std::string("--");
                    static const char *names[12] = {"C",  "C#", "D",  "D#",
                                                    "E",  "F",  "F#", "G",
                                                    "G#", "A",  "A#", "B"};
                    int n = midi % 12;
                    if (n < 0) n += 12;
                    int oct = midi / 12 - 1;
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%s%d", names[n], oct);
                    return std::string(buf);
                };
                std::string label;
                if (cur_chan == 2) {
                    const char *nm =
                        (nz[cur_step] == 0 ? "HH"
                                           : (nz[cur_step] == 1 ? "SN" : "BD"));
                    char b[8];
                    snprintf(b, sizeof(b), "%s", nm);
                    label = b;
                    s_popup_kind = 1;
                    s_popup_val = nz[cur_step];
                } else {
                    label =
                        note_name(cur_chan == 0 ? p1[cur_step] : p2[cur_step]);
                    s_popup_kind = 0;
                    s_popup_val = (cur_chan == 0 ? p1[cur_step] : p2[cur_step]);
                }
                strncpy(s_pitch_popup_text, label.c_str(),
                        sizeof(s_pitch_popup_text) - 1);
                s_pitch_popup_text[sizeof(s_pitch_popup_text) - 1] = '\0';
                s_pitch_popup_until = esp_timer_get_time() + 900000;  // 900ms
                draw();
            } else if (tb.pushing && js.pushed_down_edge) {
                if (cur_chan == 0) {
                    if (p1[cur_step] >= 0)
                        p1[cur_step] = clamp_midi(p1[cur_step] - 1);
                    else
                        cur_note_p1 = clamp_midi(cur_note_p1 - 1);
                } else if (cur_chan == 1) {
                    if (p2[cur_step] >= 0)
                        p2[cur_step] = clamp_midi(p2[cur_step] - 1);
                    else
                        cur_note_p2 = clamp_midi(cur_note_p2 - 1);
                } else {
                    if (nz[cur_step] >= 0)
                        nz[cur_step] = std::max(0, nz[cur_step] - 1);
                    else
                        cur_noise = std::max(0, cur_noise - 1);
                }
                auto note_name = [](int midi) -> std::string {
                    if (midi < 0) return std::string("--");
                    static const char *names[12] = {"C",  "C#", "D",  "D#",
                                                    "E",  "F",  "F#", "G",
                                                    "G#", "A",  "A#", "B"};
                    int n = midi % 12;
                    if (n < 0) n += 12;
                    int oct = midi / 12 - 1;
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%s%d", names[n], oct);
                    return std::string(buf);
                };
                std::string label;
                if (cur_chan == 2) {
                    const char *nm =
                        (nz[cur_step] == 0 ? "HH"
                                           : (nz[cur_step] == 1 ? "SN" : "BD"));
                    char b[8];
                    snprintf(b, sizeof(b), "%s", nm);
                    label = b;
                    s_popup_kind = 1;
                    s_popup_val = nz[cur_step];
                } else {
                    label =
                        note_name(cur_chan == 0 ? p1[cur_step] : p2[cur_step]);
                    s_popup_kind = 0;
                    s_popup_val = (cur_chan == 0 ? p1[cur_step] : p2[cur_step]);
                }
                strncpy(s_pitch_popup_text, label.c_str(),
                        sizeof(s_pitch_popup_text) - 1);
                s_pitch_popup_text[sizeof(s_pitch_popup_text) - 1] = '\0';
                s_pitch_popup_until = esp_timer_get_time() + 900000;
                draw();
            }

            // Channel change on Up/Down (without Type)
            if (!tb.pushing && js.pushed_up_edge) {
                cur_chan = (cur_chan + 2) % 3;
                draw();
            }
            if (!tb.pushing && js.pushed_down_edge) {
                cur_chan = (cur_chan + 1) % 3;
                draw();
            }

            // Toggle note on/off
            if (tb.pushed && !tb.pushed_same_time) {
                if (tb.push_type == 'l') {
                    // Long: change channel
                    cur_chan = (cur_chan + 1) % 3;
                } else {
                    if (cur_chan == 0) {
                        if (p1[cur_step] >= 0)
                            p1[cur_step] = -1;
                        else
                            p1[cur_step] = cur_note_p1;
                    } else if (cur_chan == 1) {
                        if (p2[cur_step] >= 0)
                            p2[cur_step] = -1;
                        else
                            p2[cur_step] = cur_note_p2;
                    } else {
                        if (nz[cur_step] >= 0)
                            nz[cur_step] = -1;
                        else
                            nz[cur_step] = std::min(2, std::max(0, cur_noise));
                    }
                }
                draw();
                type_button.clear_button_state();
            }

            // Save/Load dialog (Enter long)
            if (eb.pushed && eb.push_type == 'l') {
                // Clear current press/release events to avoid immediate confirm
                enter_button.clear_button_state();
                type_button.clear_button_state();
                back_button.clear_button_state();
                joystick.reset_timer();
                int slot = 1;
                bool save_mode = true;  // true: Save, false: Load
                while (1) {
                    // draw dialog
                    sprite.fillRect(0, 0, 128, 64, 0);
                    sprite.setFont(&fonts::Font2);
                    sprite.setTextColor(0xFFFFFFu, 0x000000u);
                    sprite.drawCenterString(
                        save_mode ? "Save Song" : "Load Song", 64, 4);
                    // mode toggle hint
                    sprite.drawCenterString("Type:Toggle  Enter:Confirm", 64,
                                            18);
                    // slot buttons
                    for (int i = 1; i <= 3; i++) {
                        int x = 10 + (i - 1) * 38;
                        int y = 32;
                        int w = 34;
                        int h = 18;
                        bool sel = (slot == i);
                        sprite.fillRoundRect(x, y, w, h, 3,
                                             sel ? 0xFFFF : 0x0000);
                        sprite.drawRoundRect(x, y, w, h, 3, 0xFFFF);
                        sprite.setTextColor(sel ? 0x000000u : 0xFFFFFFu,
                                            sel ? 0xFFFFu : 0x0000u);
                        char lab[8];
                        snprintf(lab, sizeof(lab), "S%d", i);
                        sprite.drawCenterString(lab, x + w / 2, y + 2);
                    }
                    push_sprite_safe(0, 0);

                    // input
                    auto js2 = joystick.get_joystick_state();
                    auto tb2 = type_button.get_button_state();
                    auto bb2 = back_button.get_button_state();
                    if (js2.pushed_left_edge) {
                        slot = (slot == 1) ? 3 : slot - 1;
                    }
                    if (js2.pushed_right_edge) {
                        slot = (slot == 3) ? 1 : slot + 1;
                    }
                    if (bb2.pushed) break;
                    if (tb2.pushed) {
                        save_mode = !save_mode;
                        type_button.clear_button_state();
                    }
                    if (enter_button.get_button_state().pushed) {
                        if (save_mode) {
                            // serialize pattern and save
                            std::string s;
                            s += "tempo=" + std::to_string(tempo) + ";";
                            s += "d2=" + std::to_string(duty_idx2) + ";";
                            s += std::string("ns=") +
                                 (noise_short ? "1" : "0") + ";";
                            auto arr = [&](const char *key, const int *a) {
                                s += key;
                                s += "=";
                                for (int i = 0; i < STEPS; i++) {
                                    s += std::to_string(a[i]);
                                    if (i != STEPS - 1) s += ",";
                                }
                                s += ";";
                            };
                            arr("p1", p1);
                            arr("p2", p2);
                            arr("nz", nz);
                            save_nvs(
                                (char *)(slot == 1
                                             ? "song1"
                                             : (slot == 2 ? "song2" : "song3")),
                                s);
                            sprite.fillRect(0, 0, 128, 64, 0);
                            sprite.setFont(&fonts::Font2);
                            sprite.setTextColor(0xFFFFFFu, 0x000000u);
                            char m[20];
                            snprintf(m, sizeof(m), "Saved S%d", slot);
                            sprite.drawCenterString(m, 64, 22);
                            push_sprite_safe(0, 0);
                            vTaskDelay(600 / portTICK_PERIOD_MS);
                            break;
                        } else {
                            // load from NVS
                            chiptune::Pattern lpat;
                            int ltempo = tempo;
                            int ld2 = duty_idx2;
                            bool lns = noise_short;
                            if (boot_sounds::load_song_from_nvs(
                                    slot, lpat, ltempo, ld2, lns)) {
                                // apply
                                for (int i = 0; i < STEPS; i++) {
                                    p1[i] = (i < (int)lpat.pulse1.size()
                                                 ? lpat.pulse1[i]
                                                 : -1);
                                }
                                for (int i = 0; i < STEPS; i++) {
                                    p2[i] = (i < (int)lpat.pulse2.size()
                                                 ? lpat.pulse2[i]
                                                 : -1);
                                }
                                for (int i = 0; i < STEPS; i++) {
                                    nz[i] = (i < (int)lpat.noise.size()
                                                 ? lpat.noise[i]
                                                 : -1);
                                }
                                tempo = std::max(40, std::min(440, ltempo));
                                duty_idx2 = std::max(0, std::min(3, ld2));
                                noise_short = lns;
                                // confirmation
                                sprite.fillRect(0, 0, 128, 64, 0);
                                sprite.setFont(&fonts::Font2);
                                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                                char m[20];
                                snprintf(m, sizeof(m), "Loaded S%d", slot);
                                sprite.drawCenterString(m, 64, 22);
                                push_sprite_safe(0, 0);
                                vTaskDelay(600 / portTICK_PERIOD_MS);
                                draw();
                                break;
                            } else {
                                // not found
                                sprite.fillRect(0, 0, 128, 64, 0);
                                sprite.setFont(&fonts::Font2);
                                sprite.setTextColor(0xFFFFFFu, 0x000000u);
                                sprite.drawCenterString("No Data", 64, 22);
                                push_sprite_safe(0, 0);
                                vTaskDelay(600 / portTICK_PERIOD_MS);
                            }
                        }
                    }
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }
                enter_button.clear_button_state();
                joystick.reset_timer();
                type_button.reset_timer();
            }

            // Play/Stop（Enter短押し）
            if (eb.pushed && eb.push_type == 's' && s_play_task == nullptr) {
                // Copy pattern to heap for playback task
                struct PlayArgs {
                    chiptune::Pattern pat;
                    int bpm;
                    float d1;
                    float d2;
                    bool nshort;
                    volatile bool *abortp;
                };
                PlayArgs *args = (PlayArgs *)heap_caps_malloc(
                    sizeof(PlayArgs), MALLOC_CAP_DEFAULT);
                new (args) PlayArgs();
                args->pat.steps = STEPS;
                args->pat.pulse1.assign(p1, p1 + STEPS);
                args->pat.pulse2.assign(p2, p2 + STEPS);
                args->pat.noise.assign(nz, nz + STEPS);
                args->bpm = tempo;
                args->d1 = duties[duty_idx1];
                args->d2 = duties[duty_idx2];
                args->nshort = noise_short;
                args->abortp = &s_abort;

                auto task = +[](void *pv) {
                    PlayArgs *a = (PlayArgs *)pv;
                    auto &spk = audio::speaker();
                    chiptune::GBSynth synth(spk.sample_rate);
                    // Streaming render to DMA buffers (no large PSRAM
                    // allocations)
                    struct Ctx {
                        chiptune::GBSynth *synth;
                        const chiptune::Pattern *pat;
                        int bpm;
                        float d1;
                        float d2;
                        bool ns;
                        chiptune::GBSynth::StreamState st;
                    } ctx{&synth, &a->pat, a->bpm, a->d1, a->d2, a->nshort, {}};

                    auto fill =
                        +[](int16_t *dst, size_t max, void *u) -> size_t {
                        Ctx *c = (Ctx *)u;
                        // ch1 = Sine, ch2 = Square, noise as configured
                        size_t w = c->synth->render_block(
                            *c->pat, c->bpm, c->d1, c->d2, c->ns, c->st, dst,
                            max, /*ch1_sine=*/true);
                        int step = c->st.step;
                        int last = c->pat->steps - 1;
                        if (step > last) step = last;
                        Composer::s_play_pos_step = (w > 0) ? step : -1;
                        return w;
                    };
                    // Total samples to render
                    const float step_sec = 60.0f / (float)a->bpm / 4.0f;
                    const int step_samples = std::max(
                        1, (int)std::round(step_sec * spk.sample_rate));
                    const size_t total_samples =
                        (size_t)step_samples * (size_t)a->pat.steps;
                    spk.play_pcm_mono16_stream(total_samples, 1.0f, a->abortp,
                                               fill, &ctx);
                    spk.deinit();
                    a->~PlayArgs();
                    heap_caps_free(a);
                    Composer::s_play_task = nullptr;
                    vTaskDelete(NULL);
                };
                xTaskCreatePinnedToCore(task, "compose_play", 4096, args, 5,
                                        &s_play_task, 1);
                // Do not block UI; task will self-delete. We can't easily reset
                // handle here, so we just ignore until user presses again after
                // a while. A tiny debounce
                vTaskDelay(30 / portTICK_PERIOD_MS);
            }

            // Continuous redraw to animate playhead / popup
            draw();
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        // Ensure playback stopped before exit
        s_abort = true;
        int wait_ms = 0;
        while (s_play_task != nullptr && wait_ms < 1500) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            wait_ms += 10;
        }
        s_play_pos_step = -1;
        running_flag = false;
        s_task_handle = nullptr;
        vTaskDelete(NULL);
    }

   private:
    static TaskHandle_t s_task_handle;
    static StaticTask_t s_task_buffer_;
    static StackType_t *s_task_stack_;
};

bool Composer::running_flag = false;
TaskHandle_t Composer::s_play_task = nullptr;
volatile bool Composer::s_abort = false;
volatile int Composer::s_play_pos_step = -1;
long long Composer::s_pitch_popup_until = 0;
char Composer::s_pitch_popup_text[16] = {0};
volatile int Composer::s_popup_kind = 0;
volatile int Composer::s_popup_val = 60;
TaskHandle_t Composer::s_task_handle = nullptr;
StaticTask_t Composer::s_task_buffer_;
StackType_t *Composer::s_task_stack_ = nullptr;

