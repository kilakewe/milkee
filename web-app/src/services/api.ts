import type { PhotoFrameState } from '@/stores/photoframe'

const API_BASE = import.meta.env.VITE_API_BASE || ''

export interface RotationResponse {
  rotation: number
}

export interface SelectPhotoResponse {
  ok: boolean
  current: string
}

export interface DeletePhotoResponse {
  ok: boolean
}

export interface NextPhotoResponse {
  ok: boolean
  current: string
}

export interface SlideshowSettings {
  enabled: boolean
  interval_s: number
}

export interface ReorderPhotosRequest {
  order: string[]
}

export interface ReorderPhotosResponse {
  ok: boolean
}

export type UploadVariant = 'landscape' | 'portrait'

export interface UploadPhotoResponse {
  ok: boolean
  id: string
  variant: UploadVariant
  filename: string
}

export const api = {
  async getRotation(): Promise<number> {
    const response = await fetch(`${API_BASE}/api/rotation`)
    if (!response.ok) throw new Error('Failed to fetch rotation')
    const data: RotationResponse = await response.json()
    return data.rotation
  },

  async setRotation(rotation: number): Promise<number> {
    const response = await fetch(`${API_BASE}/api/rotation`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: String(rotation),
    })
    if (!response.ok) throw new Error('Failed to set rotation')
    const data: RotationResponse = await response.json()
    return data.rotation
  },

  async getSlideshow(): Promise<SlideshowSettings> {
    const response = await fetch(`${API_BASE}/api/slideshow`)
    if (!response.ok) throw new Error('Failed to fetch slideshow settings')
    return await response.json()
  },

  async setSlideshow(settings: SlideshowSettings): Promise<SlideshowSettings> {
    const response = await fetch(`${API_BASE}/api/slideshow`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(settings),
    })
    if (!response.ok) throw new Error('Failed to save slideshow settings')
    return await response.json()
  },

  async getPhotos(): Promise<PhotoFrameState> {
    const response = await fetch(`${API_BASE}/api/photos`)
    if (!response.ok) throw new Error('Failed to fetch photos')
    return await response.json()
  },

  async reorderPhotos(order: string[]): Promise<ReorderPhotosResponse> {
    const payload: ReorderPhotosRequest = { order }
    const response = await fetch(`${API_BASE}/api/photos/reorder`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    })
    if (!response.ok) throw new Error('Failed to reorder photos')
    return await response.json()
  },

  async selectPhoto(id: string): Promise<SelectPhotoResponse> {
    const response = await fetch(`${API_BASE}/api/photos/select`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: id,
    })
    if (!response.ok) throw new Error('Failed to select photo')
    return await response.json()
  },

  async nextPhoto(): Promise<NextPhotoResponse> {
    const response = await fetch(`${API_BASE}/api/photos/next`, {
      method: 'POST',
    })
    if (!response.ok) throw new Error('Failed to select next photo')
    return await response.json()
  },

  async deletePhoto(id: string): Promise<DeletePhotoResponse> {
    const response = await fetch(`${API_BASE}/api/photos/delete`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: id,
    })
    if (!response.ok) throw new Error('Failed to delete photo')
    return await response.json()
  },

  async uploadPhotoVariant(variant: UploadVariant, bmpData: Blob, id?: string): Promise<UploadPhotoResponse> {
    const params = new URLSearchParams({ variant })
    if (id) params.set('id', id)

    const response = await fetch(`${API_BASE}/api/photos/upload?${params.toString()}`, {
      method: 'POST',
      headers: { 'Content-Type': 'image/bmp' },
      body: bmpData,
    })

    if (!response.ok) throw new Error('Upload failed')
    return await response.json()
  },
}
