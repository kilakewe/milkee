export interface CropParams {
  sx: number
  sy: number
  sw: number
  sh: number
}

/**
 * Load image and calculate default crop parameters
 */
export async function loadImage(file: File): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const img = new Image()
    img.onload = () => resolve(img)
    img.onerror = () => reject(new Error('Failed to load image'))
    img.src = URL.createObjectURL(file)
  })
}

/**
 * Calculate center crop parameters
 */
export function calculateCenterCrop(
  imgWidth: number,
  imgHeight: number,
  targetWidth: number,
  targetHeight: number,
): CropParams {
  const targetAspect = targetWidth / targetHeight
  const srcAspect = imgWidth / imgHeight

  let sx = 0,
    sy = 0,
    sw = imgWidth,
    sh = imgHeight

  if (srcAspect > targetAspect) {
    // Source is wider: crop width
    sw = Math.round(imgHeight * targetAspect)
    sx = Math.round((imgWidth - sw) / 2)
  } else {
    // Source is taller: crop height
    sh = Math.round(imgWidth / targetAspect)
    sy = Math.round((imgHeight - sh) / 2)
  }

  return { sx, sy, sw, sh }
}

/**
 * Crop and resize image to target dimensions
 */
export function cropAndResize(
  img: HTMLImageElement,
  targetWidth: number,
  targetHeight: number,
  cropParams: CropParams,
): ImageData {
  const canvas = document.createElement('canvas')
  const ctx = canvas.getContext('2d')
  if (!ctx) {
    throw new Error('Failed to get canvas context')
  }

  canvas.width = targetWidth
  canvas.height = targetHeight

  // Fill with white background
  ctx.fillStyle = '#FFFFFF'
  ctx.fillRect(0, 0, targetWidth, targetHeight)

  // Draw cropped image
  ctx.drawImage(img, cropParams.sx, cropParams.sy, cropParams.sw, cropParams.sh, 0, 0, targetWidth, targetHeight)

  return ctx.getImageData(0, 0, targetWidth, targetHeight)
}

/**
 * Resize to fit (contain) within target dimensions while preserving aspect ratio.
 * The unused area is filled with white.
 */
export function fitAndResize(img: HTMLImageElement, targetWidth: number, targetHeight: number): ImageData {
  const canvas = document.createElement('canvas')
  const ctx = canvas.getContext('2d')
  if (!ctx) {
    throw new Error('Failed to get canvas context')
  }

  canvas.width = targetWidth
  canvas.height = targetHeight

  // White background for letterboxing.
  ctx.fillStyle = '#FFFFFF'
  ctx.fillRect(0, 0, targetWidth, targetHeight)

  const scale = Math.min(targetWidth / img.width, targetHeight / img.height)
  const dw = Math.round(img.width * scale)
  const dh = Math.round(img.height * scale)
  const dx = Math.round((targetWidth - dw) / 2)
  const dy = Math.round((targetHeight - dh) / 2)

  ctx.drawImage(img, 0, 0, img.width, img.height, dx, dy, dw, dh)

  return ctx.getImageData(0, 0, targetWidth, targetHeight)
}

// 6-color palette for e-paper display
const PALETTE = [
  { name: 'black', r: 0, g: 0, b: 0 },
  { name: 'white', r: 255, g: 255, b: 255 },
  { name: 'yellow', r: 255, g: 255, b: 0 },
  { name: 'red', r: 255, g: 0, b: 0 },
  { name: 'blue', r: 0, g: 0, b: 255 },
  { name: 'green', r: 0, g: 255, b: 0 },
]

function clamp255(v: number): number {
  return v < 0 ? 0 : v > 255 ? 255 : v
}

function closestPaletteColor(r: number, g: number, b: number) {
  let best = PALETTE[0]!
  let bestD = Infinity
  for (const p of PALETTE) {
    const dr = r - p.r
    const dg = g - p.g
    const db = b - p.b
    const d = dr * dr + dg * dg + db * db
    if (d < bestD) {
      bestD = d
      best = p
    }
  }
  return best
}

/**
 * Apply Floyd-Steinberg dithering to 6-color palette
 */
export function ditherImage(imageData: ImageData): ImageData {
  const { data, width, height } = imageData
  const output = new ImageData(width, height)

  // Copy original data
  for (let i = 0; i < data.length; i++) {
    output.data[i] = data[i]!
  }

  // Error buffers for current and next row
  let errR = new Float32Array(width + 2)
  let errG = new Float32Array(width + 2)
  let errB = new Float32Array(width + 2)

  let nextErrR = new Float32Array(width + 2)
  let nextErrG = new Float32Array(width + 2)
  let nextErrB = new Float32Array(width + 2)

  // Floyd-Steinberg dithering
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const idx = (y * width + x) * 4

      const r0 = output.data[idx + 0]!
      const g0 = output.data[idx + 1]!
      const b0 = output.data[idx + 2]!

      const r = clamp255(r0 + errR[x]!)
      const g = clamp255(g0 + errG[x]!)
      const b = clamp255(b0 + errB[x]!)

      const p = closestPaletteColor(r, g, b)
      output.data[idx + 0] = p.r
      output.data[idx + 1] = p.g
      output.data[idx + 2] = p.b
      output.data[idx + 3] = 255

      const er = r - p.r
      const eg = g - p.g
      const eb = b - p.b

      // Distribute error to neighboring pixels
      // Right pixel (7/16)
      if (x + 1 < width) {
        errR[x + 1] = errR[x + 1]! + er * (7 / 16)
        errG[x + 1] = errG[x + 1]! + eg * (7 / 16)
        errB[x + 1] = errB[x + 1]! + eb * (7 / 16)
      }

      // Next row
      if (y + 1 < height) {
        // Bottom-left (3/16)
        if (x > 0) {
          nextErrR[x - 1] = nextErrR[x - 1]! + er * (3 / 16)
          nextErrG[x - 1] = nextErrG[x - 1]! + eg * (3 / 16)
          nextErrB[x - 1] = nextErrB[x - 1]! + eb * (3 / 16)
        }
        // Bottom (5/16)
        nextErrR[x] = nextErrR[x]! + er * (5 / 16)
        nextErrG[x] = nextErrG[x]! + eg * (5 / 16)
        nextErrB[x] = nextErrB[x]! + eb * (5 / 16)
        // Bottom-right (1/16)
        if (x + 1 < width) {
          nextErrR[x + 1] = nextErrR[x + 1]! + er * (1 / 16)
          nextErrG[x + 1] = nextErrG[x + 1]! + eg * (1 / 16)
          nextErrB[x + 1] = nextErrB[x + 1]! + eb * (1 / 16)
        }
      }
    }

    // Move down a row (swap buffers)
    let tmp
    tmp = errR
    errR = nextErrR
    nextErrR = tmp
    nextErrR.fill(0)
    tmp = errG
    errG = nextErrG
    nextErrG = tmp
    nextErrG.fill(0)
    tmp = errB
    errB = nextErrB
    nextErrB = tmp
    nextErrB.fill(0)
  }

  return output
}

/**
 * Convert ImageData to 24-bit BMP blob
 */
export function imageToBMP(imageData: ImageData): Blob {
  const { width, height, data } = imageData

  // BMP header sizes
  const fileHeaderSize = 14
  const infoHeaderSize = 40
  const rowSize = Math.floor((24 * width + 31) / 32) * 4 // Row size must be multiple of 4
  const pixelDataSize = rowSize * height
  const fileSize = fileHeaderSize + infoHeaderSize + pixelDataSize

  const buffer = new ArrayBuffer(fileSize)
  const bytes = new Uint8Array(buffer)
  const view = new DataView(buffer)

  // File header
  bytes[0] = 0x42 // 'B'
  bytes[1] = 0x4d // 'M'
  view.setUint32(2, fileSize, true)
  view.setUint32(6, 0, true) // Reserved
  view.setUint32(10, fileHeaderSize + infoHeaderSize, true) // Pixel data offset

  // Info header (BITMAPINFOHEADER)
  view.setUint32(14, infoHeaderSize, true)
  view.setInt32(18, width, true)
  view.setInt32(22, height, true)
  view.setUint16(26, 1, true) // Planes
  view.setUint16(28, 24, true) // Bits per pixel
  view.setUint32(30, 0, true) // Compression (none)
  view.setUint32(34, pixelDataSize, true)
  view.setInt32(38, 2835, true) // X pixels per meter (72 DPI)
  view.setInt32(42, 2835, true) // Y pixels per meter (72 DPI)
  view.setUint32(46, 0, true) // Colors used
  view.setUint32(50, 0, true) // Important colors

  let out = 54
  // BMP is bottom-up: write rows from bottom to top
  for (let y = height - 1; y >= 0; y--) {
    for (let x = 0; x < width; x++) {
      const srcIdx = (y * width + x) * 4
      // BGR format
      bytes[out++] = data[srcIdx + 2]! // B
      bytes[out++] = data[srcIdx + 1]! // G
      bytes[out++] = data[srcIdx]! // R
    }
    // Add padding bytes
    const padding = rowSize - width * 3
    for (let p = 0; p < padding; p++) bytes[out++] = 0
  }

  return new Blob([buffer], { type: 'image/bmp' })
}

/**
 * Get target dimensions based on device rotation
 */
export function getTargetDimensions(rotation: number): { width: number; height: number } {
  if (rotation === 0 || rotation === 180) {
    return { width: 800, height: 480 }
  } else {
    return { width: 480, height: 800 }
  }
}

export type ImageOrientation = 'landscape' | 'portrait' | 'square'

export function getOrientationFromDimensions(width: number, height: number): ImageOrientation {
  if (width === height) return 'square'
  return width > height ? 'landscape' : 'portrait'
}

export function getTargetDimensionsForOrientation(orientation: ImageOrientation): { width: number; height: number } {
  switch (orientation) {
    case 'landscape':
      return { width: 800, height: 480 }
    case 'portrait':
      return { width: 480, height: 800 }
    case 'square':
      return { width: 480, height: 480 }
  }
}

export async function rotateImageElement(
  img: HTMLImageElement,
  rotation: 0 | 90 | 180 | 270,
): Promise<HTMLImageElement> {
  const rot = ((rotation % 360) + 360) % 360
  if (rot === 0) return img

  const canvas = document.createElement('canvas')
  const ctx = canvas.getContext('2d')
  if (!ctx) throw new Error('Failed to get canvas context')

  if (rot === 90 || rot === 270) {
    canvas.width = img.height
    canvas.height = img.width
  } else {
    canvas.width = img.width
    canvas.height = img.height
  }

  ctx.translate(canvas.width / 2, canvas.height / 2)
  ctx.rotate((rot * Math.PI) / 180)
  ctx.drawImage(img, -img.width / 2, -img.height / 2)

  const blob: Blob = await new Promise((resolve, reject) => {
    canvas.toBlob((b) => {
      if (!b) reject(new Error('Failed to encode rotated image'))
      else resolve(b)
    }, 'image/png')
  })

  const url = URL.createObjectURL(blob)
  try {
    const out = new Image()
    const loaded: HTMLImageElement = await new Promise((resolve, reject) => {
      out.onload = () => resolve(out)
      out.onerror = () => reject(new Error('Failed to load rotated image'))
      out.src = url
    })
    return loaded
  } finally {
    URL.revokeObjectURL(url)
  }
}
