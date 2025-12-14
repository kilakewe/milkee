# SD card content
This folder is meant to be copied onto the SD card root.

## Layout on SD card
Copy `sd-content/system/` to the SD card as:
- `/system/index.html`
- `/system/app.js`
- `/system/README.md` (documents the web APIs)

The firmware serves all HTTP GET requests from `/sdcard/system`.
Examples:
- `GET /` -> `/sdcard/system/index.html`
- `GET /app.js` -> `/sdcard/system/app.js`
- `GET /assets/foo.css` -> `/sdcard/system/assets/foo.css`

## Notes
- Anything considered “system” content should live under `/system` on the SD card.
- Web API docs live in `/system/README.md`.
- Uploaded images are stored under:
  - `/user/current-img/`
  Each upload creates a new file like `img_000123_r180.bmp`.
- The app remembers a “current” photo and cycles to the next photo on wake from deep sleep.
- After updating files on the SD card, refresh the browser page (responses are sent with `Cache-Control: no-store`).
