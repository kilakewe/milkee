# SD card content
This folder is meant to be copied onto the SD card root.

## Layout on SD card (PhotoFrame web UI)
The PhotoFrame web UI is the Vue app under `sd-content/web-app/`.

Copy the build output to the SD card as:
- `sd-content/web-app/dist/` -> `/web-app/dist/`

The firmware serves all HTTP `GET` requests from:
- `/sdcard/web-app/dist`

Examples:
- `GET /` -> `/sdcard/web-app/dist/index.html`
- `GET /assets/<file>` -> `/sdcard/web-app/dist/assets/<file>`

Note: the Vue app uses **hash routing** (e.g. `/#/settings`) so navigation works without any server-side SPA history fallback.

## Fallback images
If there is no usable image for the current orientation, the firmware will fall back to:
- `/fallback-frame/fallback_landscape.bmp` (800×480)
- `/fallback-frame/fallback_portrait.bmp` (480×800)

This repo includes simple placeholder fallback BMPs under `sd-content/fallback-frame/`.

## Photo library storage
Uploaded photos are stored under:
- `/user/current-img/`

The ordered library is tracked by:
- `/user/current-img/library.json`

Each photo ID can have up to two variants:
- `<id>_L_r0.bmp` (landscape)
- `<id>_P_r90.bmp` (portrait)

## HTTP APIs (high level)
- `GET/POST /api/rotation`
- `GET/POST /api/slideshow` (enable + interval)
- `GET /api/photos`
- `POST /api/photos/select` (body: photo id)
- `POST /api/photos/next`
- `POST /api/photos/delete` (body: photo id)
- `POST /api/photos/reorder` (JSON body: `{ "order": ["id", ...] }`)
- `POST /api/photos/upload?variant=landscape|portrait[&id=...]` (raw BMP body)

## Legacy UI
`sd-content/system/` contains an older static UI and legacy API assumptions; it is no longer required for the current PhotoFrame web UI.
