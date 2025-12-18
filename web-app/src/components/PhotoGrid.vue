<script setup lang="ts">
import type { Photo } from '@/stores/photoframe'

defineProps<{
  photos: Photo[]
  currentPhotoId: string
  reorderMode?: boolean
}>()

const emit = defineEmits<{
  select: [id: string]
  delete: [id: string]
  move: [id: string, direction: 'up' | 'down']
}>()
</script>

<template>
  <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-4 md:gap-6">
    <div
      v-for="(photo, idx) in photos"
      :key="photo.id"
      class="bg-white border-2 rounded-lg overflow-hidden transition-all hover:border-pf-primary hover:shadow-lg"
      :class="photo.id === currentPhotoId ? 'border-pf-success bg-green-50' : 'border-gray-300'"
    >
      <div 
        class="flex justify-between items-center px-3 py-2 border-b"
        :class="photo.id === currentPhotoId ? 'bg-green-100 border-green-200' : 'bg-gray-100 border-gray-300'"
      >
        <div class="text-sm font-medium text-gray-900 overflow-hidden overflow-ellipsis whitespace-nowrap" :title="photo.id">
          {{ photo.id }}
        </div>
        <button 
          @click="emit('delete', photo.id)" 
          class="text-pf-danger text-2xl leading-none w-8 h-8 flex items-center justify-center rounded hover:bg-red-50 transition-colors touch-manipulation" 
          title="Delete photo"
        >
          ×
        </button>
      </div>

      <div class="aspect-[5/3] bg-gray-100 flex items-start justify-start p-3">
        <div class="w-full text-xs text-gray-700">
          <div class="overflow-hidden overflow-ellipsis whitespace-nowrap" :title="photo.landscape">
            <strong>L:</strong> {{ photo.landscape || '—' }}
          </div>
          <div class="overflow-hidden overflow-ellipsis whitespace-nowrap mt-1" :title="photo.portrait">
            <strong>P:</strong> {{ photo.portrait || '—' }}
          </div>
        </div>
      </div>

      <div class="p-3">
        <template v-if="reorderMode">
          <div class="flex gap-2 justify-end">
            <button 
              class="px-3 py-2 border border-gray-300 rounded-md bg-gray-100 text-gray-700 hover:bg-gray-200 disabled:opacity-50 disabled:cursor-not-allowed touch-manipulation" 
              @click="emit('move', photo.id, 'up')" 
              :disabled="idx === 0"
            >
              ↑
            </button>
            <button
              class="px-3 py-2 border border-gray-300 rounded-md bg-gray-100 text-gray-700 hover:bg-gray-200 disabled:opacity-50 disabled:cursor-not-allowed touch-manipulation"
              @click="emit('move', photo.id, 'down')"
              :disabled="idx === photos.length - 1"
            >
              ↓
            </button>
          </div>
        </template>
        <template v-else>
          <button
            v-if="photo.id !== currentPhotoId"
            @click="emit('select', photo.id)"
            class="w-full px-4 py-2 bg-pf-primary text-white rounded-md font-medium hover:bg-pf-primary-hover transition-colors touch-manipulation"
          >
            Set as Current
          </button>
          <div v-else class="text-center px-4 py-2 bg-pf-success text-white rounded-md font-medium text-sm">
            Current Photo
          </div>
        </template>
      </div>
    </div>
  </div>
</template>

