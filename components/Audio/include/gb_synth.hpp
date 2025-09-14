// Lightweight Game Boy style synth (2x pulse + noise) for MAX98357A
// Header-only: rendering into PCM and playback via Max98357A::play_pcm_mono16
#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <max98357a.h>

namespace chiptune {

struct Pattern {
    // MIDI note numbers per step, or -1 for rest. length = steps
    std::vector<int> pulse1;
    std::vector<int> pulse2;
    // Noise pitch index per step (0..7), or -1 for off
    std::vector<int> noise;
    int steps = 16;
};

class GBSynth {
   public:
    explicit GBSynth(int sample_rate_hz = 22050)
        : sample_rate(sample_rate_hz) {}

    // Render the provided pattern into a mono 16-bit PCM buffer
    std::vector<int16_t> render(const Pattern& pat, int bpm,
                                float duty1 = 0.5f,
                                float duty2 = 0.5f,
                                bool noise_short_mode = false,
                                bool ch1_sine = false) {
        const int steps = pat.steps;
        const float step_sec = 60.0f / (float)bpm / 4.0f; // 16th note per step
        const int step_samples = std::max(1, (int)std::round(step_sec * sample_rate));
        const int total_samples = step_samples * steps;

        std::vector<int16_t> out;
        out.resize(total_samples);

        // Osc states
        float phase1 = 0.0f, phase2 = 0.0f;
        uint16_t lfsr = 0x7FFFu; // 15-bit LFSR
        int current_noise = 1;
        int noise_countdown = 0;

        // Mix gains
        const float g_pulse = 0.50f; // stronger level
        const float g_noise = 0.40f;

        // Simple fade at step edges to tame clicks
        const int env_len = std::min(128, step_samples / 6);

        int w = 0; // write index
        for (int s = 0; s < steps; ++s) {
            const int n1 = (s < (int)pat.pulse1.size() ? pat.pulse1[s] : -1);
            const int n2 = (s < (int)pat.pulse2.size() ? pat.pulse2[s] : -1);
            const int nn = (s < (int)pat.noise.size() ? pat.noise[s] : -1);

            // Precompute per-step freqs & phase inc
            float inc1 = 0.0f, inc2 = 0.0f;
            if (n1 >= 0) inc1 = note_inc(n1);
            if (n2 >= 0) inc2 = note_inc(n2);

            // Noise clock divider according to pitch index
            int noise_period = 1;
            if (nn >= 0) {
                // Base around ~4000 Hz then divide by 2^k
                float nf = 4000.0f / (1 << std::clamp(nn, 0, 7));
                noise_period = std::max(1, (int)std::round((float)sample_rate / nf));
                noise_countdown = 0; // force update on first sample
            }

            for (int i = 0; i < step_samples; ++i) {
                float mix = 0.0f;
                if (n1 >= 0) {
                    phase1 += inc1;
                    if (phase1 >= 1.0f) phase1 -= 1.0f;
                    if (ch1_sine) {
                        mix += sinf(phase1 * 6.28318530718f) * g_pulse;
                    } else {
                        mix += square_from_phase(phase1, duty1) * g_pulse;
                    }
                }
                if (n2 >= 0) {
                    phase2 += inc2;
                    if (phase2 >= 1.0f) phase2 -= 1.0f;
                    mix += square_from_phase(phase2, duty2) * g_pulse;
                }
                if (nn >= 0) {
                    if (--noise_countdown <= 0) {
                        // Step LFSR
                        // Game Boy uses XOR of bit0 and bit1; 15-bit or 7-bit modes
                        uint16_t fb = ((lfsr ^ (lfsr >> 1)) & 0x1);
                        lfsr >>= 1;
                        lfsr |= (fb << 14);
                        if (noise_short_mode) {
                            // Also mirror into bit6 to emulate 7-bit mode flavor
                            lfsr &= ~(1u << 6);
                            lfsr |= (fb << 6);
                        }
                        current_noise = (lfsr & 1u) ? 1 : -1;
                        noise_countdown = noise_period;
                    }
                    mix += (float)current_noise * g_noise;
                }

                // Edge fade
                float env = 1.0f;
                if (env_len > 0) {
                    if (i < env_len) env = (float)i / (float)env_len;
                    else if (i > (step_samples - env_len)) env = (float)(step_samples - i) / (float)env_len;
                    if (env < 0.0f) env = 0.0f;
                    if (env > 1.0f) env = 1.0f;
                }

                float sflt = mix * env;
                // Clamp and convert
                if (sflt > 1.0f) sflt = 1.0f;
                if (sflt < -1.0f) sflt = -1.0f;
                out[w++] = (int16_t)std::lround(sflt * 32767.0f);
            }
        }

        return out;
    }

    // Convenience: render + play synchronously
    void play(Max98357A& spk, const Pattern& pat, int bpm,
              float duty1 = 0.5f, float duty2 = 0.5f,
              bool noise_short_mode = false, float volume = 1.0f,
              bool ch1_sine = false) {
        auto pcm = render(pat, bpm, duty1, duty2, noise_short_mode, ch1_sine);
        spk.play_pcm_mono16(pcm.data(), pcm.size(), volume);
    }

    int sample_rate = 22050;

    // --- Streaming support (for low-RAM and DMA-safe playback) ---
    struct StreamState {
        int step = 0;
        int sample_in_step = 0;
        float phase1 = 0.0f;
        float phase2 = 0.0f;
        uint16_t lfsr = 0x7FFFu;
        int current_noise = 1;
        int noise_countdown = 0;
    };

    size_t render_block(const Pattern& pat, int bpm,
                        float duty1, float duty2, bool noise_short_mode,
                        StreamState& st, int16_t* out, size_t max_out,
                        bool ch1_sine = false) {
        if (max_out == 0) return 0;
        const int steps = pat.steps;
        const float step_sec = 60.0f / (float)bpm / 4.0f; // 16th note per step
        const int step_samples = std::max(1, (int)std::round(step_sec * sample_rate));

        const float g_pulse = 0.50f;
        const float g_noise = 0.40f;

        size_t written = 0;
        while (written < max_out && st.step < steps) {
            const int n1 = (st.step < (int)pat.pulse1.size() ? pat.pulse1[st.step] : -1);
            const int n2 = (st.step < (int)pat.pulse2.size() ? pat.pulse2[st.step] : -1);
            const int nn = (st.step < (int)pat.noise.size() ? pat.noise[st.step] : -1);

            float inc1 = (n1 >= 0) ? note_inc(n1) : 0.0f;
            float inc2 = (n2 >= 0) ? note_inc(n2) : 0.0f;
            int noise_period = st.noise_countdown > 0 ? st.noise_countdown : 1;
            if (st.sample_in_step == 0) {
                if (nn >= 0) {
                    float nf = 4000.0f / (1 << std::clamp(nn, 0, 7));
                    noise_period = std::max(1, (int)std::round((float)sample_rate / nf));
                    st.noise_countdown = 0;
                }
            }

            const int remain_in_step = step_samples - st.sample_in_step;
            const int to_gen = std::min<int>((int)(max_out - written), remain_in_step);

            for (int i = 0; i < to_gen; ++i) {
                float mix = 0.0f;
                if (n1 >= 0) {
                    st.phase1 += inc1;
                    if (st.phase1 >= 1.0f) st.phase1 -= 1.0f;
                    if (ch1_sine) {
                        mix += sinf(st.phase1 * 6.28318530718f) * g_pulse;
                    } else {
                        mix += square_from_phase(st.phase1, duty1) * g_pulse;
                    }
                }
                if (n2 >= 0) {
                    st.phase2 += inc2;
                    if (st.phase2 >= 1.0f) st.phase2 -= 1.0f;
                    mix += square_from_phase(st.phase2, duty2) * g_pulse;
                }
                if (nn >= 0) {
                    if (--st.noise_countdown <= 0) {
                        uint16_t fb = ((st.lfsr ^ (st.lfsr >> 1)) & 0x1);
                        st.lfsr >>= 1;
                        st.lfsr |= (fb << 14);
                        if (noise_short_mode) {
                            st.lfsr &= ~(1u << 6);
                            st.lfsr |= (fb << 6);
                        }
                        st.current_noise = (st.lfsr & 1u) ? 1 : -1;
                        st.noise_countdown = noise_period;
                    }
                    mix += (float)st.current_noise * g_noise;
                }
                // optional simple per-step fade edges to reduce clicks
                int si = st.sample_in_step + i;
                int env_len = std::min(128, step_samples / 6);
                if (env_len > 0) {
                    float env = 1.0f;
                    if (si < env_len) env = (float)si / (float)env_len;
                    else if (si > (step_samples - env_len)) env = (float)(step_samples - si) / (float)env_len;
                    if (env < 0.0f) env = 0.0f; if (env > 1.0f) env = 1.0f;
                    mix *= env;
                }
                if (mix > 1.0f) mix = 1.0f; if (mix < -1.0f) mix = -1.0f;
                out[written++] = (int16_t)std::lround(mix * 32767.0f);
            }

            st.sample_in_step += to_gen;
            if (st.sample_in_step >= step_samples) {
                st.sample_in_step = 0;
                st.step++;
            }
        }
        return written;
    }

   private:
    inline float note_inc(int midi_note) const {
        // Convert MIDI to frequency and convert to phase increment per sample
        float f = 440.0f * std::pow(2.0f, (midi_note - 69) / 12.0f);
        return f / (float)sample_rate;
    }

    static inline float square_from_phase(float phase01, float duty) {
        return (phase01 < duty) ? 1.0f : -1.0f;
    }
};

} // namespace chiptune
