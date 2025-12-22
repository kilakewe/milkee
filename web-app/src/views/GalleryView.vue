<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { usePhotoFrameStore } from '@/stores/photoframe'
import { useToast } from '@/composables/useToast'
import { api } from '@/services/api'
import ImageUpload from '@/components/ImageUpload.vue'
import PhotoGrid from '@/components/PhotoGrid.vue'
import LoadingOverlay from '@/components/LoadingOverlay.vue'

const store = usePhotoFrameStore()
const toast = useToast()
const showUpload = ref(false)

const reorderMode = ref(false)
const orderDirty = ref(false)

async function loadPhotos() {
  try {
    store.setLoading(true)
    const data = await api.getPhotos()
    store.setPhotoData(data)
  } catch (err) {
    toast.error('Failed to load photos', err instanceof Error ? err.message : undefined)
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
    toast.success('Photo selected', `Now displaying ${id}`)
  } catch (err) {
    toast.error('Failed to select photo', err instanceof Error ? err.message : undefined)
    store.setLoading(false)
  }
}

async function handleDelete(id: string) {
  if (!confirm(`Delete ${id}?`)) return

  try {
    store.setLoading(true)
    await api.deletePhoto(id)
    await loadPhotos()
    toast.success('Photo deleted', `${id} has been removed`)
  } catch (err) {
    toast.error('Failed to delete photo', err instanceof Error ? err.message : undefined)
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

    const order = store.photos.map((p) => p.id)
    await api.reorderPhotos(order)

    orderDirty.value = false
    reorderMode.value = false
    await loadPhotos()
    toast.success('Order saved', 'Photos have been reordered')
  } catch (err) {
    toast.error('Failed to save order', err instanceof Error ? err.message : undefined)
  } finally {
    store.setLoading(false)
  }
}

onMounted(() => {
  loadPhotos()
})
</script>

<template>
  <div class="max-w-7xl mx-auto p-4 md:p-8">
    <div class="overflow-hidden rounded-lg bg-white shadow-sm relative">
      <LoadingOverlay :loading="store.isLoading" />
      <div class="px-4 py-5 sm:p-6">
        <header class="flex flex-col md:flex-row md:justify-between md:items-start gap-4 mb-6 md:mb-8">
      <div class="flex flex-col gap-1">
        <h1 class="text-2xl md:text-3xl font-semibold text-pf-dark">PhotoFrame Gallery</h1>
        <div v-if="store.currentPhotoId" class="text-sm text-pf-secondary">
          Current: <strong class="text-pf-dark">{{ store.currentPhotoId }}</strong>
          <span v-if="store.displayingFilename" class="hidden sm:inline">(displaying: {{ store.displayingFilename }})</span>
        </div>
      </div>

      <div class="flex flex-wrap gap-2 md:gap-3">
        <template v-if="reorderMode">
          <button
            @click="saveReorder"
            :disabled="store.isLoading || !orderDirty"
            class="flex-1 sm:flex-none px-4 py-2 bg-pf-success text-white rounded-md font-medium hover:bg-pf-success-hover disabled:opacity-50 disabled:cursor-not-allowed transition-colors touch-manipulation"
          >
            Save Order
          </button>
          <button
            @click="cancelReorder"
            :disabled="store.isLoading"
            class="flex-1 sm:flex-none px-4 py-2 bg-gray-400 text-pf-dark rounded-md font-medium hover:bg-gray-500 disabled:opacity-50 disabled:cursor-not-allowed transition-colors touch-manipulation"
          >
            Cancel
          </button>
        </template>
        <template v-else>

          <button
            @click="startReorder"
            :disabled="store.isLoading || !store.hasPhotos"
            class="flex-1 sm:flex-none px-4 py-2 bg-pf-secondary text-white rounded-md font-medium hover:bg-pf-secondary-hover disabled:opacity-50 disabled:cursor-not-allowed transition-colors touch-manipulation"
          >
            Reorder
          </button>
          <button
            @click="showUpload = true"
            :disabled="store.isLoading"
            class="flex-1 sm:flex-none px-4 py-2 bg-pf-primary text-white rounded-md font-medium hover:bg-pf-primary-hover disabled:opacity-50 disabled:cursor-not-allowed transition-colors touch-manipulation"
          >
            Upload New
          </button>
        </template>
      </div>
    </header>

    <div v-if="!store.hasPhotos" class="text-center py-16 px-4">
      <p class="text-pf-secondary text-lg mb-6">No photos yet. Upload your first image!</p>
      <button
        @click="showUpload = true"
        class="px-6 py-3 bg-pf-primary text-white rounded-md font-medium hover:bg-pf-primary-hover transition-colors touch-manipulation"
      >
        Upload Image
      </button>
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
    </div>
  </div>
</template>

