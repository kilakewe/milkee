# SD card content
This folder is meant to be copied onto the SD card root.

## Layout on SD card
Copy `sd-content/system/` to the SD card as:
- `/system/index.html`
- `/system/app.js`

The firmware serves all HTTP GET requests from `/sdcard/system`.
Examples:
- `GET /` -> `/sdcard/system/index.html`
- `GET /app.js` -> `/sdcard/system/app.js`
- `GET /assets/foo.css` -> `/sdcard/system/assets/foo.css`

## Notes
- Anything considered “system” content should live under `/system` on the SD card.
- The latest uploaded image is written to:
  - `/user/current-img/user_send.bmp`
- After updating files on the SD card, refresh the browser page (responses are sent with `Cache-Control: no-store`).
