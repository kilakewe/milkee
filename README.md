# Milkee

Milkee is firmware for the **Waveshare ESP32-S3-PhotoPainter** board.

At its core, Milkee turns the PhotoPainter into a low-power, Wi‑Fi-updatable **6‑color e‑paper photo frame**:

- The device starts its own Wi‑Fi access point.
- A browser UI lets you crop + dither images to the panel’s 6‑color palette and upload them.
- Every upload is stored on the SD card, so you can build a gallery and rotate through photos.
- Rotation, “current photo”, and slideshow settings are persisted in NVS.

## What Milkee does (user app)

### Photo frame over Wi‑Fi
- Runs a Wi‑Fi AP + HTTP server.
- Serves a static web UI **from the SD card**.
- Upload endpoint accepts **raw 24‑bit BMP bytes** and displays the result on the e‑paper panel.
- Image rotation supported: `0`, `90`, `180`, `270` degrees.

### SD card photo library
- Uploads are saved under `/sdcard/user/current-img/` (SD card path: `/user/current-img/`).
- The firmware maintains a simple library and lets the UI:
  - list photos
  - select a photo
  - advance to the next photo
  - delete photos
  - upload new photos

### Low power behavior
- If the device is idle for **~5 minutes**, it enters deep sleep.
- If slideshow mode is enabled, it can also wake periodically by timer, advance to the next photo, refresh the display, and sleep again.
- The key button can advance photos while awake.

## Hardware
- Target board: **Waveshare ESP32-S3-PhotoPainter** (`CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint`).
- Display: **800×480 6‑color e‑paper** (black/white/yellow/red/blue/green).
- SD card is required for:
  - web UI files
  - stored photos

## Build & flash (ESP-IDF)

Prerequisite: you must be in a working ESP‑IDF environment where `idf.py` works.

1. Select target:
   - `idf.py set-target esp32s3`
2. Configure:
   - `idf.py menuconfig`
   - In **Xiaozhi Assistant → Board Type**, select **Waveshare ESP32-S3-PhotoPainter**
3. Build:
   - `idf.py build`
4. Flash + monitor:
   - `idf.py -p {{PORT}} flash monitor`

## SD card setup

Milkee expects these paths on the SD card:

- Web UI (served at `/`):
  - `/sdcard/web-app/dist/`
- Photo storage:
  - `/sdcard/user/current-img/`
- Optional fallback images:
  - `/sdcard/fallback-frame/`

### Build the web UI and copy it to the SD card

The web UI source lives in `sd-content/web-app/` (Vue + Vite).

1. Build it:
   - `cd sd-content/web-app`
   - `pnpm install`
   - `pnpm build`
2. Copy the build output to the SD card:
   - copy `sd-content/web-app/dist/` → `/sdcard/web-app/dist/`

## Connect to the device

Milkee currently starts a Wi‑Fi AP with:
- SSID: `esp_network`
- Password: `1234567890`

After connecting, open the web UI:
- `http://192.168.4.1/` (typical ESP-IDF softAP gateway)

## HTTP APIs (used by the UI)

- `POST /dataUP`
  - body: raw 24‑bit BMP bytes
  - expected dimensions:
    - rotation `0` or `180`: **800×480**
    - rotation `90` or `270`: **480×800**
- `GET /api/rotation`, `POST /api/rotation`
- `GET /api/slideshow`, `POST /api/slideshow`
- `GET /api/photos`
- `POST /api/photos/select`
- `POST /api/photos/next`
- `POST /api/photos/delete`
- `POST /api/photos/reorder`
- `POST /api/photos/upload*`

For details, see:
- `sd-content/web-app/API.md`

## Notes / credits

This repository is based on the upstream **xiaozhi-esp32** project:
- https://github.com/78/xiaozhi-esp32
