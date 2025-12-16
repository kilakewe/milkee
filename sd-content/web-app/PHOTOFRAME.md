# PhotoFrame Management Interface

A Vue 3 web application for managing images on an ESP32-based e-paper photoframe.

## Features

### Image Upload
- Upload images that are automatically cropped to fit the photoframe dimensions
- Automatic dithering using Floyd-Steinberg algorithm for optimal e-paper display
- Live preview of processed images before upload
- Supports JPEG, PNG, BMP, GIF, and WebP formats

### Gallery Management
- View all uploaded photos in a grid layout
- See which photo is currently displayed on the frame
- Select any photo to display
- Navigate to the next photo
- Delete unwanted photos
- Photos are displayed with clear visual indicators for the current selection

### Settings
- Configure display orientation (0°, 90°, 180°, 270°)
- Rotation setting determines expected image dimensions:
  - 0° or 180°: 800×480 (Landscape)
  - 90° or 270°: 480×800 (Portrait)
- Changes are applied immediately to the photoframe

### Future Features
- Image rotation frequency scheduling (coming soon)

## Technical Details

### Image Processing
Images undergo the following processing steps:
1. **Crop & Resize**: Images are cropped to cover the target dimensions (similar to CSS `object-fit: cover`)
2. **Dithering**: Floyd-Steinberg dithering converts images to black and white for optimal e-paper display
3. **BMP Conversion**: Final image is converted to 24-bit BMP format for upload

### API Integration
The interface communicates with the ESP32 photoframe via the following endpoints (see `API.md` for full details):
- `GET /api/rotation` - Get current rotation setting
- `POST /api/rotation` - Update rotation setting
- `GET /api/photos` - List all stored photos
- `POST /api/photos/select` - Select a specific photo
- `POST /api/photos/next` - Navigate to next photo
- `POST /api/photos/delete` - Delete a photo
- `POST /dataUP` - Upload a new image

### State Management
- Pinia store manages application state
- Centralized error handling
- Loading states for all async operations

## Development

```sh
# Install dependencies
pnpm install

# Start development server
pnpm dev

# Build for production
pnpm build

# Run linter
pnpm lint
```

## Configuration

Create a `.env` file for environment-specific configuration:

```
VITE_API_BASE=http://your-photoframe-ip
```

Leave `VITE_API_BASE` empty if serving from the same origin as the API.

## Project Structure

```
src/
├── components/
│   ├── ImageUpload.vue      # Image upload modal with preview
│   └── PhotoGrid.vue         # Photo gallery grid display
├── services/
│   └── api.ts                # API client for photoframe endpoints
├── stores/
│   └── photoframe.ts         # Pinia store for state management
├── utils/
│   └── imageProcessing.ts    # Image cropping, dithering, BMP conversion
├── views/
│   ├── GalleryView.vue       # Main gallery page
│   └── SettingsView.vue      # Settings page
├── App.vue                   # Root component with navigation
└── main.ts                   # Application entry point
```

## Browser Compatibility

Tested on modern browsers:
- Chrome/Edge (recommended)
- Firefox
- Safari

Canvas API and modern JavaScript features required.
