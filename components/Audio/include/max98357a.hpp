// MAX98357A I2S amplifier helper (ESP-IDF v5 I2S new driver)
// Wiring (per todo.md):
//  - LRC  -> GPIO39 (WS/LRCLK)
//  - BCLK -> GPIO40 (BCLK)
//  - DIN  -> GPIO41 (DOUT)

#pragma once

#include <stdint.h>
#include <cmath>
#include <vector>
#include <algorithm>

#include <sound_settings.hpp>
#include "esp_heap_caps.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "freertos/FreeRTOS.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"

class Max98357A {
   public:
    // Default pins from todo.md
    int pin_bclk = 40;
    int pin_lrck = 39;  // WS
    int pin_din = 41;   // DOUT
    int pin_sd = 42;    // SD/SHDN (amplifier enable)

    uint32_t sample_rate = 44100;  // Typical audio sample rate
    i2s_chan_handle_t tx_chan = nullptr;
    bool initialized = false;
    bool is_enabled = false;
    static inline bool audio_blacklisted = false;

    // Continuous tone state
    TaskHandle_t tone_task_handle = nullptr;
    volatile bool tone_running = false;
    float tone_freq = 2300.0f;
    float tone_volume = 0.5f;
    float tone_phase = 0.0f;

    Max98357A() = default;
    Max98357A(int bclk, int lrck, int din, int rate = 44100)
        : pin_bclk(bclk), pin_lrck(lrck), pin_din(din), sample_rate(rate) {}

    ~Max98357A() { deinit(); }

    esp_err_t init() {
        if (audio_blacklisted) {
            ESP_LOGW(TAG, "audio blacklisted; skipping init");
            return ESP_ERR_INVALID_STATE;
        }
        if (initialized) return ESP_OK;

        constexpr size_t kMinDmaBlock = 1024;
        if (heap_caps_get_largest_free_block(MALLOC_CAP_DMA) < kMinDmaBlock) {
            ESP_LOGW(TAG, "insufficient DMA-capable heap (largest=%u)",
                     (unsigned)heap_caps_get_largest_free_block(
                         MALLOC_CAP_DMA));
            return ESP_ERR_NO_MEM;
        }

        // Ensure amplifier SD pin is configured and enabled (HIGH).
        if (pin_sd >= 0) {
            gpio_config_t io_conf = {};
            io_conf.pin_bit_mask = (1ULL << static_cast<uint64_t>(pin_sd));
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.intr_type = GPIO_INTR_DISABLE;
            ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&io_conf));
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                gpio_set_level(static_cast<gpio_num_t>(pin_sd), 1));
        }

        // Create TX channel (master)
        i2s_chan_config_t chan_cfg =
            I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        // Lower DMA footprint to improve robustness under memory pressure.
        chan_cfg.dma_desc_num = 3;
        chan_cfg.dma_frame_num = 96;
        esp_err_t err = i2s_new_channel(&chan_cfg, &tx_chan, nullptr);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
            if (err != ESP_ERR_NO_MEM) {
                audio_blacklisted = true;
            }
            return err;
        }

        // Configure standard I2S (Philips) in stereo, 16-bit slots
        i2s_std_config_t std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = static_cast<gpio_num_t>(pin_bclk),
                .ws = static_cast<gpio_num_t>(pin_lrck),
                .dout = static_cast<gpio_num_t>(pin_din),
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        };

        err = i2s_channel_init_std_mode(tx_chan, &std_cfg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s_channel_init_std_mode failed: %s",
                     esp_err_to_name(err));
            if (err != ESP_ERR_NO_MEM) {
                audio_blacklisted = true;
            }
            i2s_del_channel(tx_chan);
            tx_chan = nullptr;
            return err;
        }
        err = i2s_channel_enable(tx_chan);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s_channel_enable failed: %s",
                     esp_err_to_name(err));
            if (err != ESP_ERR_NO_MEM) {
                audio_blacklisted = true;
            }
            i2s_del_channel(tx_chan);
            tx_chan = nullptr;
            return err;
        }

        initialized = true;
        is_enabled = true;
        return ESP_OK;
    }

    esp_err_t enable() {
        if (audio_blacklisted) return ESP_ERR_INVALID_STATE;
        // Amplifier SD/SHDN must be HIGH before audio output.
        if (pin_sd >= 0) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                gpio_set_level(static_cast<gpio_num_t>(pin_sd), 1));
        }
        if (!initialized) {
            esp_err_t err = init();
            if (err != ESP_OK) return err;
            return ESP_OK;
        }
        if (!is_enabled) {
            ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_chan), TAG, "i2s_channel_enable failed");
            is_enabled = true;
        }
        return ESP_OK;
    }

    esp_err_t disable() {
        if (initialized && is_enabled) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(i2s_channel_disable(tx_chan));
            is_enabled = false;
        }
        // Best-effort: put amplifier into shutdown when audio is stopped.
        if (pin_sd >= 0) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                gpio_set_level(static_cast<gpio_num_t>(pin_sd), 0));
        }
        return ESP_OK;
    }

    // Play a simple sine tone via MAX98357A
    // freq_hz: tone frequency
    // duration_ms: how long to play
    // volume: 0.0 to 1.0
    esp_err_t play_tone(float freq_hz, int duration_ms, float volume = 0.5f, bool stop_after = true) {
        const float v = effective_volume(volume);
        if (v <= 0.0f) {
            return ESP_OK;
        }
        if (audio_blacklisted) return ESP_ERR_INVALID_STATE;

        ESP_RETURN_ON_ERROR(enable(), TAG, "enable failed");

        const int channels = 2;                // stereo (L/R duplicated)
        const float two_pi = 6.283185307179586f;
        const float phase_inc = two_pi * freq_hz / static_cast<float>(sample_rate);

        // Choose a moderate chunk size (in samples per channel)
        const size_t chunk_samples = 512;
        int16_t* buf = (int16_t*)heap_caps_malloc(chunk_samples * channels * sizeof(int16_t), MALLOC_CAP_DMA);
        if (!buf) return ESP_ERR_NO_MEM;

        const int total_samples = static_cast<int>((duration_ms / 1000.0f) * sample_rate);
        int samples_done = 0;
        float phase = 0.0f;

        while (samples_done < total_samples) {
            const int n = std::min<int>(chunk_samples, total_samples - samples_done);
            for (int i = 0; i < n; ++i) {
                const float s = sinf(phase) * v;
                const int16_t sample = static_cast<int16_t>(s * 32767.0f);
                // duplicate to stereo
                buf[i * channels + 0] = sample;
                buf[i * channels + 1] = sample;
                phase += phase_inc;
                if (phase > two_pi) phase -= two_pi;
            }
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            esp_err_t err = i2s_channel_write(tx_chan, buf, bytes_to_write, &bytes_written, portMAX_DELAY);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "i2s_channel_write failed: %s", esp_err_to_name(err));
                return err;
            }
            samples_done += n;
        }
        // fade out briefly to avoid pop
        {
            const int fade_ms = 20;
            const int fade_samples = std::max(1, static_cast<int>(sample_rate * (fade_ms / 1000.0f)));
            const int n = std::min<int>(fade_samples, chunk_samples);
            for (int i = 0; i < n; ++i) {
                const float amp = v * (1.0f - (static_cast<float>(i) / static_cast<float>(n)));
                const float s = sinf(phase) * amp;
                const int16_t sample = static_cast<int16_t>(s * 32767.0f);
                buf[i * channels + 0] = sample;
                buf[i * channels + 1] = sample;
                phase += phase_inc;
                if (phase > two_pi) phase -= two_pi;
            }
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            ESP_RETURN_ON_ERROR(i2s_channel_write(tx_chan, buf, bytes_to_write, &bytes_written, portMAX_DELAY), TAG, "fade write failed");
        }
        // write a short silence
        {
            const int n = std::min<int>(chunk_samples, static_cast<int>(sample_rate / 200)); // ~5ms silence
            for (int i = 0; i < n * channels; ++i) buf[i] = 0;
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            ESP_RETURN_ON_ERROR(i2s_channel_write(tx_chan, buf, bytes_to_write, &bytes_written, portMAX_DELAY), TAG, "silence write failed");
        }
        if (stop_after) {
            // stop clock to ensure amplifier shuts down
            disable();
        }
        heap_caps_free(buf);
        return ESP_OK;
    }

    // Write raw mono 16-bit PCM samples; they will be duplicated to stereo
    esp_err_t play_pcm_mono16(const int16_t* samples, size_t sample_count, float volume = 1.0f) {
        const float v = effective_volume(volume);
        if (v <= 0.0f) return ESP_OK;
        ESP_RETURN_ON_ERROR(enable(), TAG, "enable failed");
        const int channels = 2;

        size_t buf_samples_per_ch = std::min<size_t>(1024, sample_count);
        int16_t* buf = (int16_t*)heap_caps_malloc(buf_samples_per_ch * channels * sizeof(int16_t), MALLOC_CAP_DMA);
        if (!buf) return ESP_ERR_NO_MEM;

        // Prime I2S with a tiny silence to stabilize clock/amp
        {
            const int n = std::min<int>(static_cast<int>(sample_rate / 500), (int)buf_samples_per_ch); // ~2ms
            for (int i = 0; i < n * channels; ++i) buf[i] = 0;
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            (void)i2s_channel_write(tx_chan, buf, bytes_to_write, &bytes_written, pdMS_TO_TICKS(20));
        }
        size_t idx = 0;

        while (idx < sample_count) {
            const size_t n = std::min<size_t>(buf_samples_per_ch, sample_count - idx);
            for (size_t i = 0; i < n; ++i) {
                int32_t s = static_cast<int32_t>(samples[idx + i] * v);
                if (s > 32767) s = 32767;
                if (s < -32768) s = -32768;
                const int16_t smp = static_cast<int16_t>(s);
                buf[i * channels + 0] = smp;
                buf[i * channels + 1] = smp;
            }
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            esp_err_t err = i2s_channel_write(tx_chan, buf, bytes_to_write, &bytes_written, portMAX_DELAY);
            if (err != ESP_OK) return err;
            idx += n;
        }
        // Short tail silence
        {
            const int n = std::min<int>(static_cast<int>(sample_rate / 500), (int)buf_samples_per_ch);
            for (int i = 0; i < n * channels; ++i) buf[i] = 0;
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            (void)i2s_channel_write(tx_chan, buf, bytes_to_write, &bytes_written, pdMS_TO_TICKS(20));
        }
        heap_caps_free(buf);
        return ESP_OK;
    }

    // Abortable variant: checks abortp between chunks and exits early if set
    esp_err_t play_pcm_mono16_abortable(const int16_t* samples, size_t sample_count, float volume, volatile bool* abortp) {
        const float v = effective_volume(volume);
        if (v <= 0.0f) return ESP_OK;
        ESP_RETURN_ON_ERROR(enable(), TAG, "enable failed");
        const int channels = 2;

        size_t buf_samples_per_ch = std::min<size_t>(1024, sample_count);
        int16_t* buf = (int16_t*)heap_caps_malloc(buf_samples_per_ch * channels * sizeof(int16_t), MALLOC_CAP_DMA);
        if (!buf) return ESP_ERR_NO_MEM;
        size_t idx = 0;

        // Prime I2S with a tiny silence
        {
            const int n = std::min<int>(static_cast<int>(sample_rate / 1000), (int)buf_samples_per_ch); // ~1ms
            for (int i = 0; i < n * channels; ++i) buf[i] = 0;
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            (void)i2s_channel_write(tx_chan, buf, bytes_to_write, &bytes_written, pdMS_TO_TICKS(10));
        }

        while (idx < sample_count) {
            if (abortp && *abortp) break;
            const size_t n = std::min<size_t>(buf_samples_per_ch, sample_count - idx);
            for (size_t i = 0; i < n; ++i) {
                int32_t s = static_cast<int32_t>(samples[idx + i] * v);
                if (s > 32767) s = 32767;
                if (s < -32768) s = -32768;
                const int16_t smp = static_cast<int16_t>(s);
                buf[i * channels + 0] = smp;
                buf[i * channels + 1] = smp;
            }
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            esp_err_t err = i2s_channel_write(tx_chan, buf, bytes_to_write, &bytes_written, pdMS_TO_TICKS(100));
            if (err != ESP_OK) return err;
            idx += n;
        }

        // Tail silence to settle
        {
            const int n = std::min<int>(static_cast<int>(sample_rate / 1000), (int)buf_samples_per_ch);
            for (int i = 0; i < n * channels; ++i) buf[i] = 0;
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            (void)i2s_channel_write(tx_chan, buf, bytes_to_write, &bytes_written, pdMS_TO_TICKS(10));
        }
        heap_caps_free(buf);
        return ESP_OK;
    }

    // Stream mono samples via callback into DMA-safe buffer (duplicates to stereo)
    typedef size_t (*fill_mono_cb_t)(int16_t* dst, size_t max, void* user);
    esp_err_t play_pcm_mono16_stream(size_t total_samples, float volume, volatile bool* abortp,
                                     fill_mono_cb_t cb, void* user) {
        const float v = effective_volume(volume);
        if (v <= 0.0f) return ESP_OK;
        ESP_RETURN_ON_ERROR(enable(), TAG, "enable failed");
        const int channels = 2;
        const size_t mono_chunk = 512;
        int16_t* mono = (int16_t*)heap_caps_malloc(mono_chunk * sizeof(int16_t), MALLOC_CAP_INTERNAL);
        if (!mono) return ESP_ERR_NO_MEM;
        int16_t* dma = (int16_t*)heap_caps_malloc(mono_chunk * channels * sizeof(int16_t), MALLOC_CAP_DMA);
        if (!dma) { heap_caps_free(mono); return ESP_ERR_NO_MEM; }

        size_t done = 0;
        // Prime with short silence
        {
            const int n = std::min<int>(static_cast<int>(sample_rate / 1000), (int)mono_chunk);
            for (int i=0;i<n*channels;++i) dma[i]=0;
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            (void)i2s_channel_write(tx_chan, dma, bytes_to_write, &bytes_written, pdMS_TO_TICKS(10));
        }

        while (done < total_samples) {
            if (abortp && *abortp) break;
            size_t need = std::min(mono_chunk, total_samples - done);
            size_t got = cb ? cb(mono, need, user) : 0;
            if (got == 0) break;
            // volume + duplicate to stereo DMA buffer
            for (size_t i=0;i<got;++i) {
                int32_t s = (int32_t)(mono[i] * v);
                if (s > 32767) s = 32767; if (s < -32768) s = -32768;
                int16_t smp = (int16_t)s;
                dma[i*channels+0] = smp;
                dma[i*channels+1] = smp;
            }
            size_t bytes_to_write = got * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            esp_err_t err = i2s_channel_write(tx_chan, dma, bytes_to_write, &bytes_written, pdMS_TO_TICKS(200));
            if (err != ESP_OK) { heap_caps_free(mono); heap_caps_free(dma); return err; }
            done += got;
        }
        // Tail silence
        {
            const int n = std::min<int>(static_cast<int>(sample_rate / 1000), (int)mono_chunk);
            for (int i=0;i<n*channels;++i) dma[i]=0;
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            (void)i2s_channel_write(tx_chan, dma, bytes_to_write, &bytes_written, pdMS_TO_TICKS(10));
        }

        heap_caps_free(mono);
        heap_caps_free(dma);
        return ESP_OK;
    }

    // Start a continuous tone in background task
    esp_err_t start_tone(float freq_hz = 2300.0f, float volume = 0.5f) {
        float base_volume = clampf(volume, 0.0f, 1.0f);
        float eff = effective_volume(base_volume);
        tone_freq = freq_hz;
        tone_volume = base_volume;

        if (tone_task_handle) {
            if (eff > 0.0f) {
                ESP_RETURN_ON_ERROR(enable(), TAG, "enable failed");
            }
            return ESP_OK;
        }
        if (eff <= 0.0f) {
            return ESP_OK;
        }

        ESP_RETURN_ON_ERROR(enable(), TAG, "enable failed");
        tone_phase = 0.0f;
        tone_running = true;
        BaseType_t ok = xTaskCreatePinnedToCore(
            &Max98357A::tone_task_trampoline, "i2s_tone_task", 2048, this, 5, &tone_task_handle, 1);
        if (ok != pdPASS) {
            tone_task_handle = nullptr;
            tone_running = false;
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    esp_err_t stop_tone() {
        if (!tone_task_handle) return ESP_OK;
        tone_running = false;
        // wait until task exits and clears handle
        while (tone_task_handle != nullptr) {
            vTaskDelay(1);
        }
        return ESP_OK;
    }

    esp_err_t deinit() {
        // ensure tone task stopped
        stop_tone();
        if (!initialized) return ESP_OK;
        if (is_enabled) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(i2s_channel_disable(tx_chan));
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2s_del_channel(tx_chan));
        tx_chan = nullptr;
        initialized = false;
        is_enabled = false;
        return ESP_OK;
    }

   private:
    static constexpr const char* TAG = "MAX98357A";

    static inline float clampf(float x, float lo, float hi) {
        if (x < lo) return lo;
        if (x > hi) return hi;
        return x;
    }

    static inline float effective_volume(float requested) {
        float base = clampf(requested, 0.0f, 1.0f);
        if (!sound_settings::enabled()) return 0.0f;
        float global = clampf(sound_settings::volume(), 0.0f, 1.0f);
        return clampf(base * global, 0.0f, 1.0f);
    }

    static void tone_task_trampoline(void* arg) {
        reinterpret_cast<Max98357A*>(arg)->tone_task_main();
    }

    void tone_task_main() {
        const int channels = 2;
        const float two_pi = 6.283185307179586f;
        const size_t chunk_samples = 128;
        int16_t* buf = (int16_t*)heap_caps_malloc(
            chunk_samples * channels * sizeof(int16_t), MALLOC_CAP_DMA);
        if (!buf) {
            // Fallback: non-DMA internal RAM still works with i2s_channel_write.
            buf = (int16_t*)heap_caps_malloc(
                chunk_samples * channels * sizeof(int16_t),
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!buf) {
            ESP_LOGW(TAG, "tone_task_main: buffer alloc failed");
            tone_running = false;
            disable();
            TaskHandle_t self = tone_task_handle;
            tone_task_handle = nullptr;
            vTaskDelete(self);
            return;
        }

        while (tone_running) {
            float local_freq = tone_freq;
            float v = effective_volume(tone_volume);
            float phase_inc = two_pi * local_freq / static_cast<float>(sample_rate);
            if (v <= 0.0f) {
                for (size_t i = 0; i < chunk_samples * channels; ++i) {
                    buf[i] = 0;
                }
            } else {
                for (size_t i = 0; i < chunk_samples; ++i) {
                    float s = sinf(tone_phase) * v;
                    int16_t smp = static_cast<int16_t>(s * 32767.0f);
                    buf[i * channels + 0] = smp;
                    buf[i * channels + 1] = smp;
                    tone_phase += phase_inc;
                    if (tone_phase > two_pi) tone_phase -= two_pi;
                }
            }
            size_t bytes_to_write = chunk_samples * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            if (i2s_channel_write(tx_chan, buf, bytes_to_write, &bytes_written, portMAX_DELAY) != ESP_OK) {
                break;
            }
        }

        // fade out then silence
        {
            const int n = 128;
            float v = tone_volume;
            float phase_inc = two_pi * tone_freq / static_cast<float>(sample_rate);
            for (int i = 0; i < n; ++i) {
                float amp = v * (1.0f - (static_cast<float>(i) / static_cast<float>(n)));
                float s = sinf(tone_phase) * amp;
                int16_t smp = static_cast<int16_t>(s * 32767.0f);
                buf[i * channels + 0] = smp;
                buf[i * channels + 1] = smp;
                tone_phase += phase_inc;
                if (tone_phase > two_pi) tone_phase -= two_pi;
            }
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            i2s_channel_write(tx_chan, buf, bytes_to_write, &bytes_written, pdMS_TO_TICKS(50));
        }
        {
            const int n = 64;
            for (int i = 0; i < n * channels; ++i) buf[i] = 0;
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            i2s_channel_write(tx_chan, buf, bytes_to_write, &bytes_written, pdMS_TO_TICKS(20));
        }
        // Keep I2S/amplifier enabled for low-latency retrigger.
        // Power-down is handled by explicit disable()/deinit() or one-shot APIs.
        heap_caps_free(buf);
        // mark task done
        TaskHandle_t self = tone_task_handle;
        tone_task_handle = nullptr;
        vTaskDelete(self);
    }
};

namespace audio {

inline Max98357A& speaker() {
    static Max98357A instance;
    return instance;
}

}  // namespace audio
