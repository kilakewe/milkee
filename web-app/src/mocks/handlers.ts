import { http, HttpResponse, delay } from 'msw'

interface Photo {
  id: string
  landscape: string
  portrait: string
}

const ALLOWED_INTERVALS = new Set([300, 600, 900, 1800, 3600, 10800, 21600, 86400, 259200, 604800])

// Mock state
let currentRotation = 180
let slideshowEnabled = false
let slideshowIntervalS = 3600
let statusIconsEnabled = false

let photos: Photo[] = [
  { id: 'img_000001', landscape: 'img_000001_L_r0.bmp', portrait: 'img_000001_P_r90.bmp' },
  { id: 'img_000002', landscape: 'img_000002_L_r0.bmp', portrait: 'img_000002_P_r90.bmp' },
  { id: 'img_000003', landscape: 'img_000003_L_r0.bmp', portrait: '' },
]

let currentPhotoId = photos[0]?.id ?? ''
let imageCounter = 4

function chooseDisplaying(): string {
  const p = photos.find((x) => x.id === currentPhotoId)
  if (!p) return ''

  const wantPortrait = currentRotation === 90 || currentRotation === 270
  const preferred = wantPortrait ? p.portrait : p.landscape
  const secondary = wantPortrait ? p.landscape : p.portrait
  return preferred || secondary || ''
}

function allocateId(): string {
  const padded = String(imageCounter).padStart(6, '0')
  imageCounter++
  return `img_${padded}`
}

export const handlers = [
  // Get rotation
  http.get('*/api/rotation', async () => {
    await delay(200)
    return HttpResponse.json({ rotation: currentRotation })
  }),

  // Set rotation
  http.post('*/api/rotation', async ({ request }) => {
    await delay(300)
    const text = await request.text()
    const rotation = parseInt(text)

    if ([0, 90, 180, 270].includes(rotation)) {
      currentRotation = rotation
      return HttpResponse.json({ rotation: currentRotation })
    }

    return new HttpResponse('Invalid rotation', { status: 400 })
  }),

  // Slideshow settings
  http.get('*/api/slideshow', async () => {
    await delay(150)
    return HttpResponse.json({ enabled: slideshowEnabled, interval_s: slideshowIntervalS })
  }),

  http.post('*/api/slideshow', async ({ request }) => {
    await delay(200)

    const body = (await request.json()) as { enabled?: unknown; interval_s?: unknown }
    const enabled = Boolean(body.enabled)
    const interval_s = typeof body.interval_s === 'number' ? Math.trunc(body.interval_s) : NaN

    if (!ALLOWED_INTERVALS.has(interval_s)) {
      return new HttpResponse('Invalid slideshow settings', { status: 400 })
    }

    slideshowEnabled = enabled
    slideshowIntervalS = interval_s

    return HttpResponse.json({ enabled: slideshowEnabled, interval_s: slideshowIntervalS })
  }),

  // Status icon overlay setting
  http.get('*/api/status_icons', async () => {
    await delay(120)
    return HttpResponse.json({ enabled: statusIconsEnabled })
  }),

  http.post('*/api/status_icons', async ({ request }) => {
    await delay(160)

    const body = (await request.json()) as { enabled?: unknown }
    if (typeof body.enabled !== 'boolean') {
      return new HttpResponse('Invalid status icon settings', { status: 400 })
    }

    statusIconsEnabled = body.enabled
    return HttpResponse.json({ enabled: statusIconsEnabled })
  }),

  // Get photos
  http.get('*/api/photos', async () => {
    await delay(200)
    return HttpResponse.json({
      rotation: currentRotation,
      current: currentPhotoId,
      displaying: chooseDisplaying(),
      photos,
      count: photos.length,
    })
  }),

  // Get photo file (returns a placeholder BMP image)
  http.get('*/api/photos/file/:filename', async ({ params }) => {
    await delay(100)
    const filename = params.filename as string

    // Validate filename format
    if (!filename || !filename.endsWith('.bmp')) {
      return new HttpResponse('Invalid filename', { status: 400 })
    }

    // Check if this file is referenced by any photo
    const exists = photos.some((p) => p.landscape === filename || p.portrait === filename)
    if (!exists) {
      return new HttpResponse('Photo file not found', { status: 404 })
    }

    // Generate a minimal valid BMP (1x1 pixel, 24-bit color)
    // BMP Header (14 bytes) + DIB Header (40 bytes) + pixel data (3 bytes + 1 padding)
    const bmpData = new Uint8Array([
      // BMP Header
      0x42, 0x4d, // Signature 'BM'
      0x3a, 0x00, 0x00, 0x00, // File size (58 bytes)
      0x00, 0x00, 0x00, 0x00, // Reserved
      0x36, 0x00, 0x00, 0x00, // Pixel data offset (54 bytes)
      // DIB Header (BITMAPINFOHEADER)
      0x28, 0x00, 0x00, 0x00, // Header size (40 bytes)
      0x01, 0x00, 0x00, 0x00, // Width (1 pixel)
      0x01, 0x00, 0x00, 0x00, // Height (1 pixel)
      0x01, 0x00, // Color planes (1)
      0x18, 0x00, // Bits per pixel (24)
      0x00, 0x00, 0x00, 0x00, // Compression (none)
      0x04, 0x00, 0x00, 0x00, // Image size (4 bytes)
      0x13, 0x0b, 0x00, 0x00, // Horizontal resolution
      0x13, 0x0b, 0x00, 0x00, // Vertical resolution
      0x00, 0x00, 0x00, 0x00, // Colors in palette
      0x00, 0x00, 0x00, 0x00, // Important colors
      // Pixel data (BGR format) + padding
      0x80, 0x80, 0x80, 0x00, // Gray pixel (B, G, R, padding)
    ])

    return new HttpResponse(bmpData, {
      headers: {
        'Content-Type': 'image/bmp',
        'Cache-Control': 'public, max-age=3600',
      },
    })
  }),

  // Select photo by id
  http.post('*/api/photos/select', async ({ request }) => {
    await delay(200)
    const id = (await request.text()).trim()

    const photo = photos.find((p) => p.id === id)
    if (!photo) {
      return new HttpResponse('Photo not found', { status: 404 })
    }

    currentPhotoId = id
    return HttpResponse.json({ ok: true, current: currentPhotoId })
  }),

  // Next photo
  http.post('*/api/photos/next', async () => {
    await delay(200)

    if (photos.length === 0) {
      currentPhotoId = ''
      return HttpResponse.json({ ok: true, current: '' })
    }

    const idx = photos.findIndex((p) => p.id === currentPhotoId)
    const nextIdx = (idx + 1) % photos.length
    const next = photos[nextIdx]
    currentPhotoId = next?.id ?? ''

    return HttpResponse.json({ ok: true, current: currentPhotoId })
  }),

  // Delete photo
  http.post('*/api/photos/delete', async ({ request }) => {
    await delay(200)
    const id = (await request.text()).trim()

    const index = photos.findIndex((p) => p.id === id)
    if (index === -1) {
      return new HttpResponse('Photo not found', { status: 404 })
    }

    photos.splice(index, 1)

    if (id === currentPhotoId) {
      currentPhotoId = photos[0]?.id ?? ''
    }

    return HttpResponse.json({ ok: true })
  }),

  // Reorder photos
  http.post('*/api/photos/reorder', async ({ request }) => {
    await delay(200)

    const body = (await request.json()) as { order?: unknown }
    if (!Array.isArray(body.order)) {
      return new HttpResponse("Missing 'order' array", { status: 400 })
    }

    const incoming = body.order.filter((x): x is string => typeof x === 'string')

    const byId = new Map(photos.map((p) => [p.id, p] as const))
    const reordered: Photo[] = []

    for (const id of incoming) {
      const p = byId.get(id)
      if (p && !reordered.some((x) => x.id === id)) {
        reordered.push(p)
      }
    }

    for (const p of photos) {
      if (!reordered.some((x) => x.id === p.id)) {
        reordered.push(p)
      }
    }

    photos = reordered

    return HttpResponse.json({ ok: true })
  }),

  // Upload variant (raw BMP)
  http.post('*/api/photos/upload', async ({ request }) => {
    await delay(800)

    const url = new URL(request.url)
    const variantParam = url.searchParams.get('variant')
    const orientationParam = url.searchParams.get('orientation')
    const idParam = url.searchParams.get('id')?.trim()

    const variant = (orientationParam || variantParam) as string | null

    if (variant !== 'landscape' && variant !== 'portrait' && variant !== 'square') {
      return new HttpResponse('Missing/invalid orientation', { status: 400 })
    }

    const blob = await request.blob()
    const arrayBuffer = await blob.arrayBuffer()
    const view = new DataView(arrayBuffer)

    // Validate it's a BMP (check for "BM" magic bytes)
    if (view.getUint16(0, true) !== 0x4d42) {
      return new HttpResponse('Upload failed', { status: 400 })
    }

    const isNew = !idParam
    const id = idParam || allocateId()

    let p = photos.find((x) => x.id === id)
    if (!p) {
      if (!isNew) {
        return new HttpResponse('Unknown photo id', { status: 404 })
      }
      p = { id, landscape: '', portrait: '' }
      photos.push(p)
    }

    const filename =
      variant === 'landscape' ? `${id}_L_r0.bmp` : variant === 'portrait' ? `${id}_P_r90.bmp` : `${id}_S_r0.bmp`

    if (variant === 'portrait') p.portrait = filename
    else p.landscape = filename

    currentPhotoId = id

    return HttpResponse.json({ ok: true, id, variant, filename })
  }),
]

// Export functions to reset mock state (useful for testing)
export function resetMockState() {
  currentRotation = 180
  slideshowEnabled = false
  slideshowIntervalS = 3600
  statusIconsEnabled = false

  photos = [
    { id: 'img_000001', landscape: 'img_000001_L_r0.bmp', portrait: 'img_000001_P_r90.bmp' },
    { id: 'img_000002', landscape: 'img_000002_L_r0.bmp', portrait: 'img_000002_P_r90.bmp' },
    { id: 'img_000003', landscape: 'img_000003_L_r0.bmp', portrait: '' },
  ]

  currentPhotoId = photos[0]?.id ?? ''
  imageCounter = 4
}

export function getMockState() {
  return {
    currentRotation,
    slideshowEnabled,
    slideshowIntervalS,
    statusIconsEnabled,
    currentPhotoId,
    displaying: chooseDisplaying(),
    photos: [...photos],
  }
}
