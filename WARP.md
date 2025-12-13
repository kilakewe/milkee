# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Project type
- ESP-IDF firmware project (CMake-based). Entry point is `main/main.cc` (`app_main`).
- Uses ESP-IDF Component Manager for dependencies (`main/idf_component.yml`).
- This repo is based on upstream `78/xiaozhi-esp32` and includes PhotoPainter-specific code (mode switching + e-paper/SD/AI image features).

## Common commands
Prereq: run inside an ESP-IDF environment where `idf.py` works (i.e., `IDF_PATH` is set and the toolchain/Python env is configured).

### Configure + build
- Set target (default config is esp32s3):
  - `idf.py set-target esp32s3`
- Open configuration UI (board selection, language, assets flashing live under the `Xiaozhi Assistant` menu):
  - `idf.py menuconfig`
- Build:
  - `idf.py build`

### Flash / monitor
- Flash:
  - `idf.py -p {{PORT}} flash`
- Monitor serial output:
  - `idf.py -p {{PORT}} monitor`
- Flash + monitor:
  - `idf.py -p {{PORT}} flash monitor`
- Erase:
  - `idf.py -p {{PORT}} erase-flash`

### Release artifact (merged binary)
- Produce a single merged firmware image:
  - `idf.py merge-bin`
  - output: `build/merged-binary.bin`

### Tests / lint
- No dedicated unit-test runner or lint target was found in this repo.
- The primary “check” is a clean `idf.py build`, followed by on-device validation via `flash monitor`.

## Building for a specific board / packaging firmware zips
Board selection is compile-time Kconfig (`CONFIG_BOARD_TYPE_*` in `main/Kconfig.projbuild`) and maps to a directory in `main/boards/<board-type>/`.

This repo includes a board build/packaging helper:
- List supported board types (derived from `main/CMakeLists.txt`):
  - `python3 scripts/release.py --list-boards`
- Build + package one board:
  - `python3 scripts/release.py <board-type>`
  - example in this repo: `python3 scripts/release.py waveshare-s3-PhotoPainter`
- Build + package all boards that have a matching `config.json`:
  - `python3 scripts/release.py all`

Outputs are written to `releases/` as `v<PROJECT_VER>_<BOARD_NAME>.zip` (contains `merged-binary.bin`).

Notes:
- `scripts/release.py` appends build flags into `sdkconfig` (so repeated runs can accumulate config lines). If builds behave oddly, remove `sdkconfig` and/or the `build/` directory and re-run.
- `main/boards/README.md` warns not to overwrite an existing board definition for a custom board (OTA channels/identifiers must remain unique). Prefer creating a new board type or distinct `BOARD_NAME`.

## Assets & localization
### Partitioning + assets
- Partition tables live under `partitions/`.
- v2 partition layouts add an `assets` SPIFFS partition used for downloadable/flashable content (models, fonts, emoji packs, audio, etc.). See `partitions/v2/README.md`.

Asset flashing is controlled by Kconfig (`main/Kconfig.projbuild`):
- `Flash Assets` choice:
  - none / default assets / custom assets
- `CONFIG_CUSTOM_ASSETS_FILE`:
  - can be a local path (relative to repo root) or an `http(s)://` URL

Implementation details:
- `main/CMakeLists.txt` wires up assets download/selection and uses `esptool_py_flash_to_partition(... "assets" ...)` so flashing assets happens as part of `idf.py flash`.
- There is a standalone assets packer under `scripts/spiffs_assets/` that can produce an `assets.bin` suitable for the `assets` partition.

### Localization
- Language selection is Kconfig (`CONFIG_LANGUAGE_*`).
- Build generates `main/assets/lang_config.h` from `main/assets/locales/<lang>/language.json` (with `en-US` fallback) via `scripts/gen_lang.py`.

## High-level architecture
### 1) Entry point + PhotoPainter mode selection
`main/main.cc` initializes NVS and chooses a runtime mode based on NVS namespace `PhotoPainter`:
- Key `PhotPainterMode`:
  - `0x03`: “xiaozhi mode” (starts voice assistant via `Application::Start()`)
  - `0x01`: Basic mode (`User_Basic_mode_app_init()`)
  - `0x02`: Network mode (`User_Network_mode_app_init()`)
  - `0x04`: Mode selection UI (`Mode_Selection_Init()`)

PhotoPainter mode logic and tasks live in `components/user_app_bsp/`.

### 2) Board abstraction (hardware wiring)
Core board abstraction is in `main/boards/common/`:
- `board.h/.cc` and subclasses like `wifi_board.h/.cc`, `ml307_board.h/.cc`
- Common peripherals: buttons, backlight, camera, power-save timers, etc.

Each concrete board directory under `main/boards/<board-type>/` typically contains:
- `config.h`: pin maps and device-specific constants
- `config.json`: build metadata used by `scripts/release.py`
- `*.cc`: board class that derives from `WifiBoard`/`Ml307Board` and is registered via `DECLARE_BOARD(...)`

Build-time selection:
- `main/Kconfig.projbuild` defines `CONFIG_BOARD_TYPE_*`.
- `main/CMakeLists.txt` maps the selected Kconfig to `BOARD_TYPE`, includes the matching sources, and defines compile-time macros `BOARD_TYPE` and `BOARD_NAME`.

### 3) Voice assistant runtime (`Application`)
`main/application.*` orchestrates the high-level boot + runtime:
- starts networking via `Board::StartNetwork()`
- validates/downloads/applies assets (`main/assets.*` + `partitions/v2/*` layout)
- checks for firmware updates and server/protocol config (`main/ota.*`, `CONFIG_OTA_URL`)
- chooses a protocol implementation (`main/protocols/`): MQTT (`MqttProtocol`) or WebSocket (`WebsocketProtocol`)
- runs an event-loop task that:
  - ships Opus packets from `AudioService` to the selected protocol
  - updates UI/status
  - reacts to wake-word/VAD events

Audio pipeline:
- `main/audio/` implements the capture/process/encode/decode pipeline (multiple FreeRTOS tasks). See `main/audio/README.md` for the architecture.

### 4) MCP tool integration (`McpServer`)
`main/mcp_server.*` is a tool registry/dispatcher (MCP spec referenced in file header).
- `Application::Start()` registers built-in tools via:
  - `McpServer::AddCommonTools()`
  - `McpServer::AddUserOnlyTools()`
- Boards add device-specific tools inside their board implementation (see `InitializeTools()` patterns).
- Incoming JSON messages of type `"mcp"` are forwarded by `Application` to `McpServer::ParseMessage(...)`.

PhotoPainter-specific MCP tools are registered in:
- `main/boards/waveshare-s3-PhotoPainter/esp32-s3-PhotoPainter.cc`
  - tools like `self.disp.aiIMG`, SD image switching/count, scoring, and temperature/humidity are backed by `components/user_app_bsp/` tasks/event groups.
