<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { usePhotoFrameStore } from '@/stores/photoframe'
import { api } from '@/services/api'
import ImageUpload from '@/components/ImageUpload.vue'
import PhotoGrid from '@/components/PhotoGrid.vue'

const store = usePhotoFrameStore()
const showUpload = ref(false)

const reorderMode = ref(false)
const orderDirty = ref(false)

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

async function handleSelect(id: string) {
  try {
    store.setLoading(true)
    await api.selectPhoto(id)
    await loadPhotos()
  } catch (err) {
    store.setError(err instanceof Error ? err.message : 'Failed to select photo')
    store.setLoading(false)
  }
}

async function handleDelete(id: string) {
  if (!confirm(`Delete ${id}?`)) return

  try {
    store.setLoading(true)
    await api.deletePhoto(id)
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

function handleMove(id: string, direction: 'up' | 'down') {
  const idx = store.photos.findIndex((p) => p.id === id)
  if (idx === -1) return

  const next = direction === 'up' ? idx - 1 : idx + 1
  if (next < 0 || next >= store.photos.length) return

  const item = store.photos[idx]
  if (!item) return

  store.photos.splice(idx, 1)
  store.photos.splice(next, 0, item)
  orderDirty.value = true
}

function startReorder() {
  reorderMode.value = true
  orderDirty.value = false
}

async function cancelReorder() {
  reorderMode.value = false
  orderDirty.value = false
  await loadPhotos()
}

async function saveReorder() {
  try {
    store.setLoading(true)
    store.clearError()

    const order = store.photos.map((p) => p.id)
    await api.reorderPhotos(order)

    orderDirty.value = false
    reorderMode.value = false
    await loadPhotos()
  } catch (err) {
    store.setError(err instanceof Error ? err.message : 'Failed to save order')
  } finally {
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
      <div class="header-left">
        <h1>PhotoFrame Gallery</h1>
        <div v-if="store.currentPhotoId" class="subtext">
          Current: <strong>{{ store.currentPhotoId }}</strong>
          <span v-if="store.displayingFilename">(displaying: {{ store.displayingFilename }})</span>
        </div>
      </div>

      <div class="actions">
        <template v-if="reorderMode">
          <button @click="saveReorder" :disabled="store.isLoading || !orderDirty" class="btn-save">
            Save Order
          </button>
          <button @click="cancelReorder" :disabled="store.isLoading" class="btn-cancel">Cancel</button>
        </template>
        <template v-else>
          <button @click="handleNext" :disabled="store.isLoading || !store.hasPhotos" class="btn-next">
            Next Photo
          </button>
          <button @click="startReorder" :disabled="store.isLoading || !store.hasPhotos" class="btn-reorder">
            Reorder
          </button>
          <button @click="showUpload = true" :disabled="store.isLoading" class="btn-upload">
            Upload New
          </button>
        </template>
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
      :current-photo-id="store.currentPhotoId"
      :reorder-mode="reorderMode"
      @select="handleSelect"
      @delete="handleDelete"
      @move="handleMove"
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
  align-items: flex-start;
  margin-bottom: 2rem;
}

.header-left {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.subtext {
  font-size: 0.875rem;
  color: #6b7280;
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
.btn-upload,
.btn-reorder,
.btn-save,
.btn-cancel {
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

.btn-reorder {
  background: #f59e0b;
  color: #111827;
}

.btn-reorder:hover:not(:disabled) {
  background: #d97706;
}

.btn-upload {
  background: #3b82f6;
  color: white;
}

.btn-upload:hover:not(:disabled) {
  background: #2563eb;
}

.btn-save {
  background: #10b981;
  color: white;
}

.btn-save:hover:not(:disabled) {
  background: #059669;
}

.btn-cancel {
  background: #f3f4f6;
  color: #374151;
}

.btn-cancel:hover:not(:disabled) {
  background: #e5e7eb;
}

.btn-next:disabled,
.btn-upload:disabled,
.btn-reorder:disabled,
.btn-save:disabled,
.btn-cancel:disabled {
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
