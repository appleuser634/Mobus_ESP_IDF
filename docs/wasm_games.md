# WASM Mini-Game Pipeline

This project builds WebAssembly mini-games from Zig and stores them on the SPIFFS filesystem so they can be streamed at runtime via the [wasm3](https://github.com/wasm3/wasm3) interpreter. This document explains the runtime API, how to build games, and how to integrate new titles.

## Runtime Overview

- WASM runtime: [`wasm_runtime::run_game`](../components/wasm_runtime/include/wasm_game_runtime.hpp)
- Host bindings expose drawing, timing, randomness, and input helpers.
- Games live on SPIFFS under `/spiffs/games/<name>.wasm` (see `fs/games/mopping.wasm`).
- The menu’s “Game” entry launches the Zig-ported **MOPPING** mini-game.

### Host APIs (`env` module)

| Function | Signature | Description |
|----------|-----------|-------------|
| `host_clear_screen` | `() -> void` | Fill the 128×64 canvas with black. |
| `host_present` | `() -> void` | Push the current sprite buffer to the OLED. |
| `host_fill_rect` | `(x, y, w, h, color) -> void` | Fill rectangle with 16-bit color (use `0xFFFF` for white). |
| `host_draw_text` | `(x, y, ptr, len, invert) -> void` | Draw UTF-8 text. Set `invert = 1` for inverted colors. |
| `host_draw_sprite` | `(sprite_id, frame, x, y) -> void` | Draw a predefined sprite (id `0` = kuina, frame `0/1`). |
| `host_get_input` | `() -> u32` | Returns a bitmask of button/joystick edges (see below). |
| `host_random` | `(max) -> i32` | Pseudo-random integer range `[0, max)`. |
| `host_sleep` | `(ms) -> void` | Delay task for `ms` milliseconds (rounded up to RTOS tick). |
| `host_time_ms` | `() -> u32` | Milliseconds since boot (wraps after ~49 days). |

Input bit mask (`host_get_input` result):

```zig
const Input = struct {
    pub const action = 1 << 0; // Type button short-press
    pub const enter = 1 << 1;  // Enter button
    pub const back = 1 << 2;   // Back button
    pub const joy_left = 1 << 3;
    pub const joy_right = 1 << 4;
    pub const joy_up = 1 << 5;
    pub const joy_down = 1 << 6;
};
```

Bits fire once per edge (debounced) and are cleared automatically.

## Building a Game in Zig

1. Add a Zig source file under `wasm_games/` (see [`mopping.zig`](../wasm_games/mopping.zig)).
2. Use the host API declarations:
   ```zig
   extern "env" fn host_clear_screen() void;
   extern "env" fn host_draw_text(x: i32, y: i32, ptr: [*]const u8, len: i32, invert: i32) void;
   extern "env" fn host_draw_sprite(id: i32, frame: i32, x: i32, y: i32) void;
   extern "env" fn host_get_input() u32;
   // ... etc
   ```
3. Export two entry points:
   ```zig
   pub export fn game_init() void { /* initialisation */ }
   pub export fn game_update(dt_ms: u32) u32 {
       // return 0 to continue, non-zero to exit
   }
   ```
4. Compile to Wasm (Zig 0.15+):
   ```bash
   zig build-exe wasm_games/your_game.zig \
       -target wasm32-freestanding \
       -O ReleaseSmall \
       -mcpu=baseline \
       --import-symbols \
       --export=game_init \
       --export=game_update \
       -femit-bin=wasm_games/your_game.wasm
   ```
   *(Earlier versions of Zig allowed `zig build-lib`; newer releases emit an
   archive by default, so use `build-exe` with explicit exports/imports.)*
5. Copy the resulting `.wasm` into the SPIFFS image directory (default `fs/games/`):
   ```bash
   mkdir -p fs/games
   cp wasm_games/your_game.wasm fs/games/
   ```
   *(Remember to export a no-op `_start` in your Zig code so the linker is satisfied: `pub export fn _start() void {}`.)*
6. Build the SPIFFS image and flash it:
   ```bash
   pio run -t buildfs
   pio run -t uploadfs
   ```
7. Launch the game from firmware by supplying the filesystem path, e.g.
   ```cpp
   wasm_runtime::run_game("/spiffs/games/your_game.wasm");
   ```

## Adding New Games

- Place the compiled `.wasm` under `fs/games/` so it is bundled into SPIFFS.
- Add a wrapper similar to [`mopping.h`](../components/Display/include/mopping.h) or call `run_game("/spiffs/games/<name>.wasm")` directly.
- Extend `host_draw_sprite` (in `wasm_game_runtime.cpp`) to expose additional sprite IDs or utilities as needed.
- Update the UI/menu to launch the new game.

## Development Notes

- The runtime uses a 32 KB stack for Wasm3; adjust in `wasm_game_runtime.cpp` if larger modules are required.
- All drawing occurs on the existing `sprite` buffer; remember to call `host_present()` once per frame.
- Keep per-frame computation small (<16 ms) to maintain smooth animation.
- `host_sleep(16)` is optional but recommended to yield the RTOS task.

---

For further reference, check wasm3’s documentation and examples: <https://github.com/wasm3/wasm3>.
