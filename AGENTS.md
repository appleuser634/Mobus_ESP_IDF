# Repository Guidelines

## Project Structure & Module Organization
The firmware entry point lives in `main/main.cpp`, with supporting assets in `fs/` packed into SPIFFS at build time. Feature code sits under `components/`, separated by capability (e.g., `Network`, `Display`, `Haptics`, `wasm_runtime`); each component exposes public headers in `include/` and implementation in `src/`. Shared reference data lives in `data/`, `images/`, and `sounds/`. Configuration templates (`sdkconfig*`, `partitions_two_ota.csv`) and docs (`docs/`, `memo.md`) round out the top level.

## Build, Flash, and Run
Use PlatformIO for day-to-day work:
- `~/.platformio/penv/bin/pio run` compiles the project for the `esp32dev` environment.
- `~/.platformio/penv/bin/pio run -t upload` flashes via the configured `upload_port`.
- `~/.platformio/penv/bin/pio device monitor --environment esp32dev` opens the serial monitor.
- `~/.platformio/penv/bin/pio run -t menuconfig` launches ESP-IDF Menuconfig with defaults from `sdkconfig.defaults`.

## Coding Style & Naming
Run `./format.sh` before committing; it applies `clang-format` using Google style with 4-space indentation. Keep include blocks grouped (standard, ESP-IDF, third-party, project). Public headers export PascalCase types (`HostContext`) while functions and free-standing helpers stay snake_case (`ensure_spiffs_mounted`). Prefer `constexpr`, scoped enums, and ESP-IDF logging macros (`ESP_LOGI`) for diagnostics.

## Testing Guidelines
ESP-IDF integration tests use the `ttfw_idf` harness. To exercise the sample test, activate a Python environment with ESP-IDF tools and run `python -m pytest example_test.py`. Add new tests beside the example, naming files `*_test.py` and targeting relevant tags (`env_tag`, `target`). When adding runtime features, include minimal hardware-in-the-loop steps in the PR description.

## Commit & Pull Request Workflow
Follow the existing `type: short description` commit style (`add: wasm runtime loader`). Keep messages ≤72 characters in the subject and expand in the body when rationale matters. PRs should explain user-visible changes, note required configuration (e.g., NVS keys `server_host`, `mqtt_host`), link issues, and include serial logs or photos when UI or hardware behavior is affected. Request at least one reviewer familiar with the touched component.

## Configuration & Security Notes
Store runtime endpoints and credentials in NVS keys (`server_host`, `server_port`, `mqtt_host`, `user_name`, `password`) rather than source files. Mirror essential Menuconfig changes into `sdkconfig.defaults` so new checkouts inherit the same baseline. Keep SPIFFS payloads lean to stay within the 16MB flash footprint, and avoid committing device-specific data under `fs/`.

# TODO

## 本プロジェクトはC/C++の初心者が実装を行っています。適切な実装になるように本プロジェクトの全体において以下を適用してください

## ** 動作に変更がないように修正を行ってください **

- クラスのインスタンスを不必要に作成している箇所を共通のインスタンスを使用するように修正
- DRYにするために同じような処理は共通化
- 他に修正すべき箇所があれば修正を行ってください 
