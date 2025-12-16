<script setup lang="ts">
import type { Photo } from '@/stores/photoframe'

defineProps<{
  photos: Photo[]
  currentPhoto: string
}>()

const emit = defineEmits<{
  select: [filename: string]
  delete: [filename: string]
}>()
</script>

<template>
  <div class="photo-grid">
    <div
      v-for="photo in photos"
      :key="photo.name"
      class="photo-card"
      :class="{ current: photo.name === currentPhoto }"
    >
      <div class="photo-header">
        <div class="photo-name" :title="photo.name">{{ photo.name }}</div>
        <button @click="emit('delete', photo.name)" class="btn-delete" title="Delete photo">
          ×
        </button>
      </div>

      <div class="photo-placeholder">
        <svg
          xmlns="http://www.w3.org/2000/svg"
          fill="none"
          viewBox="0 0 24 24"
          stroke="currentColor"
        >
          <path
            stroke-linecap="round"
            stroke-linejoin="round"
            stroke-width="2"
            d="M4 16l4.586-4.586a2 2 0 012.828 0L16 16m-2-2l1.586-1.586a2 2 0 012.828 0L20 14m-6-6h.01M6 20h12a2 2 0 002-2V6a2 2 0 00-2-2H6a2 2 0 00-2 2v12a2 2 0 002 2z"
          />
        </svg>
      </div>

      <div class="photo-actions">
        <button
          v-if="photo.name !== currentPhoto"
          @click="emit('select', photo.name)"
          class="btn-select"
        >
          Set as Current
        </button>
        <div v-else class="current-badge">Current Photo</div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.photo-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
  gap: 1.5rem;
}

.photo-card {
  background: white;
  border: 2px solid #e5e7eb;
  border-radius: 0.5rem;
  overflow: hidden;
  transition: all 0.2s;
}

.photo-card:hover {
  border-color: #3b82f6;
  box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1);
}

.photo-card.current {
  border-color: #10b981;
  background: #f0fdf4;
}

.photo-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0.75rem;
  background: #f9fafb;
  border-bottom: 1px solid #e5e7eb;
}

.photo-card.current .photo-header {
  background: #d1fae5;
}

.photo-name {
  font-size: 0.875rem;
  font-weight: 500;
  color: #374151;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.btn-delete {
  background: none;
  border: none;
  font-size: 1.5rem;
  color: #ef4444;
  cursor: pointer;
  padding: 0;
  width: 2rem;
  height: 2rem;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 0.25rem;
  transition: background 0.2s;
}

.btn-delete:hover {
  background: #fee;
}

.photo-placeholder {
  aspect-ratio: 5/3;
  background: #f3f4f6;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #9ca3af;
}

.photo-placeholder svg {
  width: 4rem;
  height: 4rem;
}

.photo-actions {
  padding: 0.75rem;
}

.btn-select {
  width: 100%;
  padding: 0.5rem;
  background: #3b82f6;
  color: white;
  border: none;
  border-radius: 0.375rem;
  font-weight: 500;
  cursor: pointer;
  transition: background 0.2s;
}

.btn-select:hover {
  background: #2563eb;
}

.current-badge {
  text-align: center;
  padding: 0.5rem;
  background: #10b981;
  color: white;
  border-radius: 0.375rem;
  font-weight: 500;
  font-size: 0.875rem;
}
</style>
