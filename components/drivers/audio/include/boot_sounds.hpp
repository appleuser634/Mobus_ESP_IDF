// Boot sound sequences using Max98357A (I2S amplifier)
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <vector>
#include <utility>
#include <cstdlib>

#include "max98357a.h"
#include "gb_synth.hpp"
#include <nvs_rw.hpp>

namespace boot_sounds {

// Simple helper: play a sequence of {freq Hz, duration ms}
inline void play_sequence(Max98357A& spk, const std::vector<std::pair<float,int>>& seq, float volume)
{
    spk.init();
    for (auto& nd : seq) {
        spk.play_tone(nd.first, nd.second, volume);
        vTaskDelay(pdMS_TO_TICKS(10)); // tiny gap
    }
    spk.disable();
}

// Cute chiptune-like boot (Game Boy-ish) — short arpeggios and staccato
inline void play_cute(Max98357A& spk, float volume = 0.5f)
{
    using P = std::pair<float,int>;
    // Frequencies (Hz)
    constexpr float C5=523.25f, E5=659.26f, G5=783.99f, C6=1046.50f;
    constexpr float A5=880.00f, F5=698.46f, D5=587.33f;
    std::vector<P> seq = {
        // Up arpeggio C-major
        {C5,80},{E5,80},{G5,80},{C6,100},
        // Little twinkle
        {G5,60},{C6,120},
        // Tiny fall
        {A5,70},{G5,70},{E5,90},
        // End ping
        {C6,120}
    };
    play_sequence(spk, seq, volume);
}

// Majestic boot (Windows 7-ish) — rising intervals and held notes
inline void play_majestic(Max98357A& spk, float volume = 0.55f)
{
    using P = std::pair<float,int>;
    // Frequencies (Hz)
    constexpr float D4=293.66f, A4=440.00f, D5=587.33f;
    constexpr float G4=392.00f, B4=493.88f, G5=783.99f;
    constexpr float Fs5=739.99f, E5=659.26f;
    std::vector<P> seq = {
        // Opening motif (D chord broken)
        {D4,180},{A4,180},{D5,260},
        // Transition (G chord broken)
        {G4,160},{B4,160},{G5,220},
        // Cadence
        {A4,180},{D5,220},{Fs5,260},
        // Grand finish
        {E5,240},{D5,420}
    };
    play_sequence(spk, seq, volume);
}

// Game Boy-style boot using 2 pulse + noise via chiptune::GBSynth
inline void play_gb(Max98357A& spk, float volume = 0.9f)
{
    using namespace chiptune;
    // Match synth to speaker's configured sample rate
    GBSynth synth(spk.sample_rate);
    Pattern pat;
    pat.steps = 16;
    pat.pulse1.assign(pat.steps, -1);
    pat.pulse2.assign(pat.steps, -1);
    pat.noise.assign(pat.steps, -1);

    // Simple C major arpeggio across 4 beats
    auto set = [&](std::vector<int>& v, int step, int midi){ if(step>=0 && step<pat.steps) v[step]=midi; };
    set(pat.pulse1, 0, 60);  // C4
    set(pat.pulse1, 4, 64);  // E4
    set(pat.pulse1, 8, 67);  // G4
    set(pat.pulse1,12, 72);  // C5
    // Harmony hits
    set(pat.pulse2, 2, 67);  // G4
    set(pat.pulse2, 6, 71);  // B4
    set(pat.pulse2,10, 74);  // D5
    set(pat.pulse2,14, 76);  // E5
    // Noise taps
    pat.noise[3] = 3; pat.noise[7] = 2; pat.noise[11] = 3; pat.noise[15] = 1;

    // 12.5% and 50% duties feel GB-like
    synth.play(spk, pat, 140, 0.125f, 0.5f, true, volume);
}

// Helper: parse serialized song from NVS (tempo=..;d2=..;ns=0/1;p1=..;p2=..;nz=..;)
inline bool load_song_from_nvs(int slot, chiptune::Pattern& pat, int& tempo, int& duty2, bool& noise_short)
{
    std::string key = slot==1?"song1":(slot==2?"song2":"song3");
    std::string s = get_nvs((char*)key.c_str());
    if (s.empty()) return false;
    pat.steps = 16; pat.pulse1.assign(16,-1); pat.pulse2.assign(16,-1); pat.noise.assign(16,-1);
    tempo = 120; duty2 = 2; noise_short = false;
    auto get = [&](const char* k)->std::string {
        size_t p = s.find(std::string(k) + "="); if (p==std::string::npos) return std::string();
        size_t q = s.find(';', p); size_t vpos = p + strlen(k) + 1; if (q==std::string::npos) q = s.size();
        return s.substr(vpos, q - vpos);
    };
    auto to_int = [](const std::string& t){ return (int)strtol(t.c_str(), nullptr, 10); };
    auto tempo_s = get("tempo"); if(!tempo_s.empty()) tempo = to_int(tempo_s);
    auto d2s = get("d2"); if(!d2s.empty()) duty2 = to_int(d2s);
    auto nss = get("ns"); if(!nss.empty()) noise_short = (to_int(nss)!=0);
    auto parse_arr = [&](const char* k, std::vector<int>& out){
        auto v = get(k); if (v.empty()) return; out.clear(); out.reserve(16);
        size_t i=0; while (i<v.size()) { size_t c=v.find(',', i); if (c==std::string::npos) c=v.size(); out.push_back(to_int(v.substr(i, c-i))); i = c+1; }
        while(out.size()<16) out.push_back(-1); while(out.size()>16) out.pop_back();
    };
    parse_arr("p1", pat.pulse1); parse_arr("p2", pat.pulse2); parse_arr("nz", pat.noise);
    // Clamp drums to 0..2 if older data existed
    for (auto &v : pat.noise) { if (v < -1) v = -1; if (v > 2) v = 2; }
    return true;
}

// Play a saved song slot as boot sound
inline void play_song(Max98357A& spk, int slot, float volume = 0.9f)
{
    using namespace chiptune;
    Pattern pat; int tempo=120, d2=2; bool ns=false;
    if (!load_song_from_nvs(slot, pat, tempo, d2, ns)) return;
    GBSynth synth(spk.sample_rate);

    // Streaming playback to avoid large allocations
    struct Ctx { GBSynth* sy; const Pattern* pt; int bpm; float d1; float d2; bool ns; GBSynth::StreamState st; };
    Ctx ctx { &synth, &pat, tempo, 0.5f, (d2==0?0.125f:d2==1?0.25f:d2==2?0.5f:0.75f), ns, {} };
    auto fill = +[](int16_t* dst, size_t max, void* u)->size_t{
        Ctx* c=(Ctx*)u; return c->sy->render_block(*c->pt, c->bpm, c->d1, c->d2, c->ns, c->st, dst, max, /*ch1_sine=*/true);
    };
    const float step_sec = 60.0f / (float)tempo / 4.0f;
    const int step_samples = std::max(1, (int)std::round(step_sec * spk.sample_rate));
    const size_t total_samples = (size_t)step_samples * (size_t)pat.steps;
    spk.play_pcm_mono16_stream(total_samples, volume, nullptr, fill, &ctx);
}

} // namespace boot_sounds
