# Mock API for Development

This project includes a mock API server using [Mock Service Worker (MSW)](https://mswjs.io/) for development and testing without needing the actual ESP32 hardware.

## Features

The mock API simulates all photoframe endpoints with realistic behavior:

- **Rotation management** - Get and set display rotation (0°, 90°, 180°, 270°)
- **Photo management** - List, select, delete, and navigate photos
- **Image upload** - Upload new images with BMP validation
- **Persistent state** - Mock data persists during the development session
- **Realistic delays** - Network delays simulate real API behavior

## Initial Mock Data

The mock API starts with:
- **Current rotation:** 180°
- **Photos:** 3 pre-loaded images
  - `img_000001_r180.bmp` (current)
  - `img_000002_r180.bmp`
  - `img_000003_r0.bmp`

## Setup

### 1. Install Dependencies

```bash
pnpm install
```

### 2. Initialize MSW

MSW requires a service worker file in the public directory:

```bash
npx msw init public/ --save
```

This creates `public/mockServiceWorker.js` which enables request interception in the browser.

### 3. Start Development Server

```bash
pnpm dev
```

The mock API will automatically start when running in development mode.

## Configuration

### Enable/Disable Mock API

In `.env`:

```bash
# Enable mock API (default)
VITE_ENABLE_MOCK_API=true

# Disable mock API (use real backend)
VITE_ENABLE_MOCK_API=false
```

### Use Real Backend

To test against the actual ESP32 device:

1. Set `VITE_ENABLE_MOCK_API=false` in `.env`
2. Set `VITE_API_BASE` to your device's IP:

```bash
VITE_API_BASE=http://192.168.1.100
VITE_ENABLE_MOCK_API=false
```

## Mock API Behavior

### Photo Upload (`POST /dataUP`)
- Validates BMP format (checks for "BM" magic bytes)
- Auto-generates filename like `img_000004_r180.bmp`
- Includes rotation in filename
- Simulates 1 second upload time
- Returns Chinese success/failure message

### Rotation Setting (`POST /api/rotation`)
- Validates rotation value (must be 0, 90, 180, or 270)
- Updates display rotation
- Affects dimensions for new uploads

### Photo Selection (`POST /api/photos/select`)
- Validates photo exists
- Extracts rotation from filename
- Sets as current photo

### Photo Deletion (`POST /api/photos/delete`)
- Removes photo from list
- If current photo deleted, selects another
- Returns success response

### Next Photo (`POST /api/photos/next`)
- Cycles through photos in order
- Wraps to first photo after last

## Development Tools

### Browser DevTools

With MSW running, you can:
1. Open browser DevTools
2. Go to Network tab
3. See intercepted requests marked with MSW
4. Inspect request/response details

### Mock State Utilities

In `src/mocks/handlers.ts`:

```typescript
import { resetMockState, getMockState } from '@/mocks/handlers'

// Reset to initial state
resetMockState()

// Get current mock state
const state = getMockState()
console.log(state)
```

## Testing

The mock API is automatically used in:
- Development server (`pnpm dev`)
- Unit tests (can import handlers directly)
- E2E tests (service worker runs in test browser)

## File Structure

```
src/mocks/
├── handlers.ts     # API request handlers and mock state
└── browser.ts      # MSW browser setup
```

## Troubleshooting

### Mock API Not Working

1. Ensure `public/mockServiceWorker.js` exists:
   ```bash
   npx msw init public/ --save
   ```

2. Check browser console for MSW messages:
   - Should see: `[MSW] Mocking enabled`

3. Verify `.env` configuration:
   ```bash
   VITE_ENABLE_MOCK_API=true
   ```

### Service Worker Issues

If you see CORS or network errors:
1. Clear browser cache
2. Unregister service workers (DevTools → Application → Service Workers)
3. Hard refresh (Cmd/Ctrl + Shift + R)
4. Restart dev server

### TypeScript Errors

If you see MSW type errors:
```bash
pnpm install --save-dev msw@latest
```

## Production

The mock API is **automatically disabled** in production builds (`pnpm build`). The code is tree-shaken out, so it doesn't add to your bundle size.

To ensure mock code is excluded:
- Mock imports use dynamic `import()`
- Condition checks `import.meta.env.DEV`
- Production builds strip unused code

## Learn More

- [MSW Documentation](https://mswjs.io/)
- [MSW Browser Integration](https://mswjs.io/docs/integrations/browser)
- [Testing with MSW](https://mswjs.io/docs/recipes/test-overview)
