import { http, HttpResponse, delay } from 'msw'

interface Photo {
  name: string
}

// Mock state
let currentRotation = 180
let currentImageRotation = 180
let currentPhoto = 'img_000001_r180.bmp'
let photos: Photo[] = [
  { name: 'img_000001_r180.bmp' },
  { name: 'img_000002_r180.bmp' },
  { name: 'img_000003_r0.bmp' },
]

let imageCounter = 4

export const handlers = [
  // Get rotation
  http.get('/api/rotation', async () => {
    await delay(200)
    return HttpResponse.json({ rotation: currentRotation })
  }),

  // Set rotation
  http.post('/api/rotation', async ({ request }) => {
    await delay(300)
    const text = await request.text()
    const rotation = parseInt(text)

    if ([0, 90, 180, 270].includes(rotation)) {
      currentRotation = rotation
      return HttpResponse.json({ rotation: currentRotation })
    }

    return new HttpResponse('Invalid rotation', { status: 400 })
  }),

  // Get photos
  http.get('/api/photos', async () => {
    await delay(200)
    return HttpResponse.json({
      rotation: currentRotation,
      image_rotation: currentImageRotation,
      current: currentPhoto,
      photos: photos,
      count: photos.length,
    })
  }),

  // Select photo
  http.post('/api/photos/select', async ({ request }) => {
    await delay(300)
    const filename = await request.text()

    const photo = photos.find((p) => p.name === filename)
    if (!photo) {
      return new HttpResponse('Photo not found', { status: 404 })
    }

    currentPhoto = filename

    // Extract rotation from filename (e.g., img_000001_r180.bmp)
    const match = filename.match(/_r(\d+)\.bmp$/)
    const rotMatch = match?.[1]
    if (rotMatch) {
      currentImageRotation = parseInt(rotMatch, 10)
    }

    return HttpResponse.json({
      ok: true,
      current: currentPhoto,
      image_rotation: currentImageRotation,
    })
  }),

  // Next photo
  http.post('/api/photos/next', async () => {
    await delay(300)

    if (photos.length === 0) {
      return HttpResponse.json({ ok: true, current: '' })
    }

    const currentIndex = photos.findIndex((p) => p.name === currentPhoto)
    const nextIndex = (currentIndex + 1) % photos.length
    const nextPhoto = photos[nextIndex]
    if (nextPhoto) {
      currentPhoto = nextPhoto.name

      // Extract rotation from filename
      const match = currentPhoto.match(/_r(\d+)\.bmp$/)
      const rotMatch = match?.[1]
      if (rotMatch) {
        currentImageRotation = parseInt(rotMatch, 10)
      }
    }

    return HttpResponse.json({ ok: true, current: currentPhoto })
  }),

  // Delete photo
  http.post('/api/photos/delete', async ({ request }) => {
    await delay(300)
    const filename = await request.text()

    const index = photos.findIndex((p) => p.name === filename)
    if (index === -1) {
      return new HttpResponse('Photo not found', { status: 404 })
    }

    photos.splice(index, 1)

    // If we deleted the current photo, select another
    if (filename === currentPhoto) {
      if (photos.length > 0) {
        const firstPhoto = photos[0]
        if (firstPhoto) {
          currentPhoto = firstPhoto.name
          const match = currentPhoto.match(/_r(\d+)\.bmp$/)
          if (match && match[1]) {
            currentImageRotation = parseInt(match[1], 10)
          }
        }
      } else {
        currentPhoto = ''
        currentImageRotation = currentRotation
      }
    }

    return HttpResponse.json({ ok: true })
  }),

  // Upload image
  http.post('/dataUP', async ({ request }) => {
    await delay(1000) // Simulate longer upload time

    const blob = await request.blob()

    // Validate it's a BMP (check for "BM" magic bytes)
    const arrayBuffer = await blob.arrayBuffer()
    const view = new DataView(arrayBuffer)

    if (view.getUint16(0, true) !== 0x4d42) {
      return new HttpResponse('上传失败', { status: 400 })
    }

    // Generate new filename
    const paddedCounter = String(imageCounter).padStart(6, '0')
    const newFilename = `img_${paddedCounter}_r${currentRotation}.bmp`
    imageCounter++

    // Add to photos
    photos.push({ name: newFilename })
    currentPhoto = newFilename
    currentImageRotation = currentRotation

    return new HttpResponse('上传成功', { status: 200 })
  }),
]

// Export functions to reset mock state (useful for testing)
export function resetMockState() {
  currentRotation = 180
  currentImageRotation = 180
  currentPhoto = 'img_000001_r180.bmp'
  photos = [
    { name: 'img_000001_r180.bmp' },
    { name: 'img_000002_r180.bmp' },
    { name: 'img_000003_r0.bmp' },
  ]
  imageCounter = 4
}

export function getMockState() {
  return {
    currentRotation,
    currentImageRotation,
    currentPhoto,
    photos: [...photos],
  }
}
