<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { usePhotoFrameStore } from '@/stores/photoframe'
import { api } from '@/services/api'
import ImageUpload from '@/components/ImageUpload.vue'
import PhotoGrid from '@/components/PhotoGrid.vue'

const store = usePhotoFrameStore()
const showUpload = ref(false)

async function loadPhotos() {
  try {
    store.setLoading(true)
    store.clearError()
    const data = await api.getPhotos()
    store.setPhotoData(data)
  } catch (err) {
    store.setError(err instanceof Error ? err.message : 'Failed to load photos')
  } finally {
    store.setLoading(false)
  }
}

function handleUploadComplete() {
  showUpload.value = false
  loadPhotos()
}

async function handleSelect(filename: string) {
  try {
    store.setLoading(true)
    await api.selectPhoto(filename)
    await loadPhotos()
  } catch (err) {
    store.setError(err instanceof Error ? err.message : 'Failed to select photo')
    store.setLoading(false)
  }
}

async function handleDelete(filename: string) {
  if (!confirm(`Delete ${filename}?`)) return

  try {
    store.setLoading(true)
    await api.deletePhoto(filename)
    await loadPhotos()
  } catch (err) {
    store.setError(err instanceof Error ? err.message : 'Failed to delete photo')
    store.setLoading(false)
  }
}

async function handleNext() {
  try {
    store.setLoading(true)
    await api.nextPhoto()
    await loadPhotos()
  } catch (err) {
    store.setError(err instanceof Error ? err.message : 'Failed to go to next photo')
    store.setLoading(false)
  }
}

onMounted(() => {
  loadPhotos()
})
</script>

<template>
  <div class="gallery-view">
    <header class="header">
      <h1>PhotoFrame Gallery</h1>
      <div class="actions">
        <button @click="handleNext" :disabled="store.isLoading || !store.hasPhotos" class="btn-next">
          Next Photo
        </button>
        <button @click="showUpload = true" :disabled="store.isLoading" class="btn-upload">
          Upload New
        </button>
      </div>
    </header>

    <div v-if="store.error" class="error">
      {{ store.error }}
      <button @click="store.clearError" class="btn-close">×</button>
    </div>

    <div v-if="store.isLoading" class="loading">Loading...</div>

    <div v-if="!store.isLoading && !store.hasPhotos" class="empty-state">
      <p>No photos yet. Upload your first image!</p>
      <button @click="showUpload = true" class="btn-upload-large">Upload Image</button>
    </div>

    <PhotoGrid
      v-if="store.hasPhotos"
      :photos="store.photos"
      :current-photo="store.currentPhoto"
      @select="handleSelect"
      @delete="handleDelete"
    />

    <ImageUpload
      v-if="showUpload"
      :rotation="store.rotation"
      @close="showUpload = false"
      @complete="handleUploadComplete"
    />
  </div>
</template>

<style scoped>
.gallery-view {
  max-width: 1200px;
  margin: 0 auto;
  padding: 2rem;
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 2rem;
}

h1 {
  font-size: 2rem;
  font-weight: 600;
  color: #1f2937;
  margin: 0;
}

.actions {
  display: flex;
  gap: 1rem;
}

.btn-next,
.btn-upload {
  padding: 0.5rem 1rem;
  border: none;
  border-radius: 0.375rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
}

.btn-next {
  background: #6b7280;
  color: white;
}

.btn-next:hover:not(:disabled) {
  background: #4b5563;
}

.btn-upload {
  background: #3b82f6;
  color: white;
}

.btn-upload:hover:not(:disabled) {
  background: #2563eb;
}

.btn-next:disabled,
.btn-upload:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.error {
  background: #fee;
  border: 1px solid #fcc;
  color: #c00;
  padding: 1rem;
  border-radius: 0.375rem;
  margin-bottom: 1rem;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.btn-close {
  background: none;
  border: none;
  font-size: 1.5rem;
  cursor: pointer;
  color: #c00;
  padding: 0;
  width: 2rem;
  height: 2rem;
}

.loading {
  text-align: center;
  padding: 3rem;
  color: #6b7280;
  font-size: 1.125rem;
}

.empty-state {
  text-align: center;
  padding: 4rem 2rem;
}

.empty-state p {
  color: #6b7280;
  font-size: 1.125rem;
  margin-bottom: 1.5rem;
}

.btn-upload-large {
  padding: 0.75rem 1.5rem;
  background: #3b82f6;
  color: white;
  border: none;
  border-radius: 0.375rem;
  font-size: 1rem;
  font-weight: 500;
  cursor: pointer;
  transition: background 0.2s;
}

.btn-upload-large:hover {
  background: #2563eb;
}
</style>
