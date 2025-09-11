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

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "freertos/FreeRTOS.h"

#include "driver/i2s_std.h"

class Max98357A {
   public:
    // Default pins from todo.md
    int pin_bclk = 40;
    int pin_lrck = 39;  // WS
    int pin_din = 41;   // DOUT

    uint32_t sample_rate = 44100;  // Typical audio sample rate
    i2s_chan_handle_t tx_chan = nullptr;
    bool initialized = false;
    bool is_enabled = false;

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
        if (initialized) return ESP_OK;

        // Create TX channel (master)
        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_chan, nullptr), TAG, "i2s_new_channel failed");

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

        ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_chan, &std_cfg), TAG, "i2s_channel_init_std_mode failed");
        ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_chan), TAG, "i2s_channel_enable failed");

        initialized = true;
        is_enabled = true;
        return ESP_OK;
    }

    esp_err_t enable() {
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
        return ESP_OK;
    }

    // Play a simple sine tone via MAX98357A
    // freq_hz: tone frequency
    // duration_ms: how long to play
    // volume: 0.0 to 1.0
    esp_err_t play_tone(float freq_hz, int duration_ms, float volume = 0.5f, bool stop_after = true) {
        ESP_RETURN_ON_ERROR(enable(), TAG, "enable failed");

        const int channels = 2;                // stereo (L/R duplicated)
        const float v = clampf(volume, 0.0f, 1.0f);
        const float two_pi = 6.283185307179586f;
        const float phase_inc = two_pi * freq_hz / static_cast<float>(sample_rate);

        // Choose a moderate chunk size (in samples per channel)
        const size_t chunk_samples = 512;
        std::vector<int16_t> buf(chunk_samples * channels);

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
            esp_err_t err = i2s_channel_write(tx_chan, buf.data(), bytes_to_write, &bytes_written, portMAX_DELAY);
            if (err != ESP_OK) return err;
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
            ESP_RETURN_ON_ERROR(i2s_channel_write(tx_chan, buf.data(), bytes_to_write, &bytes_written, portMAX_DELAY), TAG, "fade write failed");
        }
        // write a short silence
        {
            const int n = std::min<int>(chunk_samples, static_cast<int>(sample_rate / 200)); // ~5ms silence
            std::fill(buf.begin(), buf.begin() + n * channels, 0);
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            ESP_RETURN_ON_ERROR(i2s_channel_write(tx_chan, buf.data(), bytes_to_write, &bytes_written, portMAX_DELAY), TAG, "silence write failed");
        }
        if (stop_after) {
            // stop clock to ensure amplifier shuts down
            disable();
        }
        return ESP_OK;
    }

    // Write raw mono 16-bit PCM samples; they will be duplicated to stereo
    esp_err_t play_pcm_mono16(const int16_t* samples, size_t sample_count, float volume = 1.0f) {
        ESP_RETURN_ON_ERROR(init(), TAG, "init failed");
        const float v = clampf(volume, 0.0f, 1.0f);
        const int channels = 2;

        std::vector<int16_t> buf(std::min<size_t>(1024, sample_count) * channels);
        size_t idx = 0;

        while (idx < sample_count) {
            const size_t n = std::min<size_t>(buf.size() / channels, sample_count - idx);
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
            esp_err_t err = i2s_channel_write(tx_chan, buf.data(), bytes_to_write, &bytes_written, portMAX_DELAY);
            if (err != ESP_OK) return err;
            idx += n;
        }
        return ESP_OK;
    }

    // Start a continuous tone in background task
    esp_err_t start_tone(float freq_hz = 2300.0f, float volume = 0.5f) {
        ESP_RETURN_ON_ERROR(enable(), TAG, "enable failed");
        if (tone_task_handle) {
            // already running; update parameters
            tone_freq = freq_hz;
            tone_volume = clampf(volume, 0.0f, 1.0f);
            return ESP_OK;
        }
        tone_freq = freq_hz;
        tone_volume = clampf(volume, 0.0f, 1.0f);
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

    static void tone_task_trampoline(void* arg) {
        reinterpret_cast<Max98357A*>(arg)->tone_task_main();
    }

    void tone_task_main() {
        const int channels = 2;
        const float two_pi = 6.283185307179586f;
        const size_t chunk_samples = 256;
        std::vector<int16_t> buf(chunk_samples * channels);

        while (tone_running) {
            float local_freq = tone_freq;
            float v = tone_volume;
            float phase_inc = two_pi * local_freq / static_cast<float>(sample_rate);
            for (size_t i = 0; i < chunk_samples; ++i) {
                float s = sinf(tone_phase) * v;
                int16_t smp = static_cast<int16_t>(s * 32767.0f);
                buf[i * channels + 0] = smp;
                buf[i * channels + 1] = smp;
                tone_phase += phase_inc;
                if (tone_phase > two_pi) tone_phase -= two_pi;
            }
            size_t bytes_to_write = chunk_samples * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            if (i2s_channel_write(tx_chan, buf.data(), bytes_to_write, &bytes_written, portMAX_DELAY) != ESP_OK) {
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
            i2s_channel_write(tx_chan, buf.data(), bytes_to_write, &bytes_written, pdMS_TO_TICKS(50));
        }
        {
            const int n = 64;
            std::fill(buf.begin(), buf.begin() + n * channels, 0);
            size_t bytes_to_write = n * channels * sizeof(int16_t);
            size_t bytes_written = 0;
            i2s_channel_write(tx_chan, buf.data(), bytes_to_write, &bytes_written, pdMS_TO_TICKS(20));
        }
        // stop clock
        disable();
        // mark task done
        TaskHandle_t self = tone_task_handle;
        tone_task_handle = nullptr;
        vTaskDelete(self);
    }
};
