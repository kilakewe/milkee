import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export interface Photo {
  id: string
  landscape: string
  portrait: string
}

// Response shape from GET /api/photos
export interface PhotoFrameState {
  rotation: number
  current: string
  displaying: string
  photos: Photo[]
  count: number
}

export const usePhotoFrameStore = defineStore('photoframe', () => {
  const rotation = ref<number>(0)
  const currentPhotoId = ref<string>('')
  const displayingFilename = ref<string>('')
  const photos = ref<Photo[]>([])
  const isLoading = ref<boolean>(false)
  const error = ref<string | null>(null)

  const hasPhotos = computed(() => photos.value.length > 0)
  const photoCount = computed(() => photos.value.length)

  function setRotation(value: number) {
    rotation.value = value
  }

  function setPhotoData(data: PhotoFrameState) {
    rotation.value = data.rotation
    currentPhotoId.value = data.current
    displayingFilename.value = data.displaying
    photos.value = data.photos
  }

  function setLoading(value: boolean) {
    isLoading.value = value
  }

  function setError(message: string | null) {
    error.value = message
  }

  function clearError() {
    error.value = null
  }

  return {
    rotation,
    currentPhotoId,
    displayingFilename,
    photos,
    isLoading,
    error,
    hasPhotos,
    photoCount,
    setRotation,
    setPhotoData,
    setLoading,
    setError,
    clearError,
  }
})
