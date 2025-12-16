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
  <div class="photo-grid">
    <div
      v-for="(photo, idx) in photos"
      :key="photo.id"
      class="photo-card"
      :class="{ current: photo.id === currentPhotoId }"
    >
      <div class="photo-header">
        <div class="photo-name" :title="photo.id">{{ photo.id }}</div>
        <button @click="emit('delete', photo.id)" class="btn-delete" title="Delete photo">×</button>
      </div>

      <div class="photo-placeholder">
        <div class="variant-text">
          <div class="variant-line" :title="photo.landscape"><strong>L:</strong> {{ photo.landscape || '—' }}</div>
          <div class="variant-line" :title="photo.portrait"><strong>P:</strong> {{ photo.portrait || '—' }}</div>
        </div>
      </div>

      <div class="photo-actions">
        <template v-if="reorderMode">
          <div class="reorder-row">
            <button class="btn-secondary" @click="emit('move', photo.id, 'up')" :disabled="idx === 0">
              ↑
            </button>
            <button
              class="btn-secondary"
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
            class="btn-select"
          >
            Set as Current
          </button>
          <div v-else class="current-badge">Current Photo</div>
        </template>
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
  align-items: flex-start;
  justify-content: flex-start;
  padding: 0.75rem;
}

.variant-text {
  width: 100%;
  font-size: 0.75rem;
  color: #374151;
}

.variant-line {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.variant-line + .variant-line {
  margin-top: 0.25rem;
}

.reorder-row {
  display: flex;
  gap: 0.5rem;
  justify-content: flex-end;
}

.btn-secondary {
  padding: 0.5rem 0.75rem;
  border: 1px solid #d1d5db;
  border-radius: 0.375rem;
  background: #f3f4f6;
  color: #374151;
  cursor: pointer;
}

.btn-secondary:disabled {
  opacity: 0.5;
  cursor: not-allowed;
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
