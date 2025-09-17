#pragma once

#include "wasm_game_runtime.hpp"

inline void mopping_main() {
    (void)wasm_runtime::run_game("/spiffs/games/mopping.wasm");
}
