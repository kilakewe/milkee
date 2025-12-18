# Quick Start Guide

Get the PhotoFrame Manager running in under 2 minutes!

## Prerequisites

- Node.js 20.19+ or 22.12+
- pnpm 10.25+

## Setup

### 1. Install Dependencies

```bash
pnpm install
```

### 2. Initialize Mock Service Worker

```bash
npx msw init public/ --save
```

This creates the service worker needed for the mock API.

### 3. Start Development Server

```bash
pnpm dev
```

Open your browser to the URL shown (usually `http://localhost:5173`).

## What You'll See

The app starts with a **mock API** that simulates the ESP32 photoframe:

- **3 pre-loaded photos** in the gallery
- **180° rotation** setting
- Fully functional upload, select, delete, and navigation

## Try It Out

### Upload an Image

1. Click **"Upload New"**
2. Select any image file (JPEG, PNG, etc.)
3. Click **"Process & Dither"** to see the black/white preview
4. Click **"Upload to Frame"** to add it to the gallery

### Manage Photos

- Click **"Set as Current"** to select a photo
- Click **"Next Photo"** to cycle through photos
- Click **"×"** to delete a photo

### Change Settings

1. Click **"Settings"** in the navigation
2. Change the rotation (affects upload dimensions)
3. Changes are applied immediately

## Switch to Real Hardware

When you're ready to test with the actual ESP32:

1. Edit `.env`:
   ```bash
   VITE_API_BASE=http://192.168.1.100  # Your ESP32 IP
   VITE_ENABLE_MOCK_API=false
   ```

2. Restart dev server:
   ```bash
   pnpm dev
   ```

## Build for Production

```bash
pnpm build
```

Output goes to `dist/` directory. The mock API code is automatically excluded from production builds.

## Need Help?

- **Mock API details**: See `MOCK_API.md`
- **Full documentation**: See `PHOTOFRAME.md`
- **API reference**: See `API.md`

## Common Issues

**Service worker not found?**
```bash
npx msw init public/ --save
```

**Mock API not working?**
- Check `.env` has `VITE_ENABLE_MOCK_API=true`
- Hard refresh your browser (Cmd/Ctrl + Shift + R)
- Check browser console for `[MSW] Mocking enabled`

**TypeScript errors?**
```bash
pnpm install
```
