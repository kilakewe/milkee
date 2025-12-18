## Upload API
### `POST /dataUP`
Upload a 24-bit BMP.

Request
- Body: raw BMP bytes.
- The UI sends `Content-Type: image/bmp`.

Expected dimensions
- If rotation is `0` or `180`: `800x480`
- If rotation is `90` or `270`: `480x800`

Behavior
- Saves to SD under:
  - `/sdcard/user/current-img/` (SD card path: `/user/current-img/`)
- Each upload creates a new file like:
  - `img_000123_r180.bmp`
- Sets the newly uploaded file as the **current** photo.
- Remembers the rotation used at upload time (used to correctly rotate the current photo later).
- Triggers a redraw on the e-paper.

Response
- On success: HTTP 200 with text `上传成功`
- On failure: HTTP 200 with text `上传失败` (or an HTTP error if the request fails early)

## Rotation API
### `GET /api/rotation`
Returns the current device rotation setting.

Response
- Content-Type: `application/json`
- Body:
  - `{ "rotation": 180 }`

### `POST /api/rotation`
Sets the rotation.

Request
- Content-Type: `text/plain`
- Body: `0`, `90`, `180`, or `270`

Behavior
- Saves the rotation.
- Triggers a redraw of the current image.

Response
- Content-Type: `application/json`
- Body:
  - `{ "rotation": 180 }`

## Stored photo management API
All photos live in:
- `/sdcard/user/current-img` (SD card path: `/user/current-img`)

Filename safety rules (enforced by the firmware)
- Basename only (no `/` or `\`)
- Must end with `.bmp`
- Must be `< 128` characters
- Allowed characters: `A-Z a-z 0-9 _ - .`

### `GET /api/photos`
Lists stored photos and the currently selected one.

Response
- Content-Type: `application/json`
- Example:
  - `{ "rotation":180, "image_rotation":180, "current":"img_000123_r180.bmp", "photos":[{"name":"img_000123_r180.bmp"}], "count":1 }`

Fields
- `rotation`: the current device rotation setting.
- `image_rotation`: the rotation that was active when the current image was created/uploaded.
- `current`: basename of the current photo (empty string if none).
- `photos`: array of `{ "name": "..." }`.
- `count`: number of photos.

### `GET /api/photos/file/:filename`
Serves a stored photo file.

Request
- URL parameter: filename (e.g. `img_000123_r180.bmp`)

Behavior
- Serves the BMP file from `/sdcard/user/current-img/` directory.
- Validates filename for safety (no path traversal, must end with `.bmp`).

Response
- Content-Type: `image/bmp`
- Body: raw BMP file data
- On error: HTTP 404 if file not found, HTTP 400 if filename is invalid

### `POST /api/photos/select`
Selects a stored photo as current.

Request
- Content-Type: `text/plain`
- Body: filename (e.g. `img_000123_r180.bmp`)

Behavior
- Updates the current photo.
- Extracts the upload-time rotation from the filename `_r<deg>` suffix when present.
- Triggers a redraw.

Response
- Content-Type: `application/json`
- Example:
  - `{ "ok": true, "current": "img_000123_r180.bmp", "image_rotation": 180 }`

### `POST /api/photos/next`
Selects the next stored photo.

Request
- No body.

Behavior
- Chooses the next `.bmp` in lexicographic order (wraps to the first).
- Triggers a redraw.

Response
- Content-Type: `application/json`
- Example:
  - `{ "ok": true, "current": "img_000124_r180.bmp" }`

### `POST /api/photos/delete`
Deletes a stored photo.

Request
- Content-Type: `text/plain`
- Body: filename (e.g. `img_000123_r180.bmp`)

Behavior
- Deletes the file.
- If the deleted file was the current photo, the firmware selects another photo if available.

Response
- Content-Type: `application/json`
- Example:
  - `{ "ok": true }`
