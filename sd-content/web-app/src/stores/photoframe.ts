import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export interface Photo {
  name: string
}

export interface PhotoFrameState {
  rotation: number
  image_rotation: number
  current: string
  photos: Photo[]
  count: number
}

export const usePhotoFrameStore = defineStore('photoframe', () => {
  const rotation = ref<number>(0)
  const imageRotation = ref<number>(0)
  const currentPhoto = ref<string>('')
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
    imageRotation.value = data.image_rotation
    currentPhoto.value = data.current
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
    imageRotation,
    currentPhoto,
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
