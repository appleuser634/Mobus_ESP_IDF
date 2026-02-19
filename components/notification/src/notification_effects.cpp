#include "notification_effects.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <haptic_motor.hpp>
#include <neopixel.hpp>

namespace {

constexpr const char* kTag = "NotifEffects";
// Stack usage spikes when LED driver and logging run together, so keep margin.
constexpr uint32_t kTaskStackWords = 4096;
constexpr UBaseType_t kTaskPriority = 6;

StaticTask_t g_task_buffer;
StackType_t* g_task_stack = nullptr;
TaskHandle_t g_task_handle = nullptr;

void fade_out_led(uint8_t r, uint8_t g, uint8_t b) {
    constexpr int kFadeSteps = 12;
    constexpr int kFadeStepDelayMs = 35;
    for (int step = kFadeSteps; step >= 0; --step) {
        uint8_t rr = static_cast<uint8_t>((r * step) / kFadeSteps);
        uint8_t gg = static_cast<uint8_t>((g * step) / kFadeSteps);
        uint8_t bb = static_cast<uint8_t>((b * step) / kFadeSteps);
        neopixel.set_color(rr, gg, bb);
        vTaskDelay(pdMS_TO_TICKS(kFadeStepDelayMs));
    }
    neopixel.set_color(0, 0, 0);
}

void run_effect(HapticMotor& motor) {
    constexpr int kBlinkCount = 3;
    const uint8_t kPrimaryR = 0;
    const uint8_t kPrimaryG = 24;
    const uint8_t kPrimaryB = 96;
    const uint8_t kIdleR = 0;
    const uint8_t kIdleG = 12;
    const uint8_t kIdleB = 24;

    for (int i = 0; i < kBlinkCount; ++i) {
        neopixel.set_color(kPrimaryR, kPrimaryG, kPrimaryB);
        motor.pulse(HapticMotor::kDefaultFrequencyHz, 120,
                    HapticMotor::kDefaultDuty);
        neopixel.set_color(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    neopixel.set_color(kIdleR, kIdleG, kIdleB);
    vTaskDelay(pdMS_TO_TICKS(120));
    fade_out_led(kIdleR, kIdleG, kIdleB);
}

void task_body(void*) {
    auto& motor = HapticMotor::instance();
    for (;;) {
        uint32_t notified = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (notified == 0) {
            continue;
        }
        while (ulTaskNotifyTake(pdTRUE, 0) > 0) {
            // Drain any additional notifications to coalesce bursts.
        }
        run_effect(motor);
    }
}

void ensure_task_started() {
    if (g_task_handle) {
        return;
    }

    if (!g_task_stack) {
        size_t bytes = kTaskStackWords * sizeof(StackType_t);
        g_task_stack = static_cast<StackType_t*>(heap_caps_malloc(
            bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!g_task_stack) {
            ESP_LOGE(kTag, "Failed to allocate task stack (%u bytes)",
                     static_cast<unsigned>(bytes));
            return;
        }
    }

    g_task_handle = xTaskCreateStaticPinnedToCore(
        &task_body, "notif_effects", kTaskStackWords, nullptr, kTaskPriority,
        g_task_stack, &g_task_buffer, tskNO_AFFINITY);
    if (!g_task_handle) {
        ESP_LOGE(kTag, "Failed to create notification effects task");
    }
}

}  // namespace

namespace notification_effects {

void init() {
    ensure_task_started();
}

void signal_new_message() {
    ensure_task_started();
    if (!g_task_handle) {
        return;
    }
    xTaskNotifyGive(g_task_handle);
}

}  // namespace notification_effects
