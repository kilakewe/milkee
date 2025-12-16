import type { PhotoFrameState } from '@/stores/photoframe'

const API_BASE = import.meta.env.VITE_API_BASE || ''

export interface RotationResponse {
  rotation: number
}

export interface SelectPhotoResponse {
  ok: boolean
  current: string
  image_rotation: number
}

export interface DeletePhotoResponse {
  ok: boolean
}

export interface NextPhotoResponse {
  ok: boolean
  current: string
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

  async getPhotos(): Promise<PhotoFrameState> {
    const response = await fetch(`${API_BASE}/api/photos`)
    if (!response.ok) throw new Error('Failed to fetch photos')
    return await response.json()
  },

  async selectPhoto(filename: string): Promise<SelectPhotoResponse> {
    const response = await fetch(`${API_BASE}/api/photos/select`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: filename,
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

  async deletePhoto(filename: string): Promise<DeletePhotoResponse> {
    const response = await fetch(`${API_BASE}/api/photos/delete`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: filename,
    })
    if (!response.ok) throw new Error('Failed to delete photo')
    return await response.json()
  },

  async uploadImage(bmpData: Blob): Promise<string> {
    const response = await fetch(`${API_BASE}/dataUP`, {
      method: 'POST',
      headers: { 'Content-Type': 'image/bmp' },
      body: bmpData,
    })
    if (!response.ok) throw new Error('Upload failed')
    return await response.text()
  },
}
