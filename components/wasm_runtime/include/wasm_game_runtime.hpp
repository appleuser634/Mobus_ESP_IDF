#pragma once

#include <cstddef>
#include <cstdint>

namespace wasm_runtime {

constexpr uint32_t INPUT_ACTION = 1u << 0;     // Type button
constexpr uint32_t INPUT_ENTER = 1u << 1;      // Enter button
constexpr uint32_t INPUT_BACK = 1u << 2;       // Back button
constexpr uint32_t INPUT_JOY_LEFT = 1u << 3;   // Joystick left edge
constexpr uint32_t INPUT_JOY_RIGHT = 1u << 4;  // Joystick right edge
constexpr uint32_t INPUT_JOY_UP = 1u << 5;     // Joystick up edge
constexpr uint32_t INPUT_JOY_DOWN = 1u << 6;   // Joystick down edge

// Runs a Wasm mini-game from the provided binary blob stored in SPIFFS or other
// filesystem. Returns true when the game exits normally.
bool run_game(const char* path);

}  // namespace wasm_runtime
