<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { usePhotoFrameStore } from '@/stores/photoframe'
import { api } from '@/services/api'

const store = usePhotoFrameStore()
const saving = ref(false)
const savedMessage = ref(false)

const rotationOptions = [
  { value: 0, label: '0° (Landscape)' },
  { value: 90, label: '90° (Portrait)' },
  { value: 180, label: '180° (Landscape Inverted)' },
  { value: 270, label: '270° (Portrait Inverted)' },
]

async function loadSettings() {
  try {
    store.setLoading(true)
    const rotation = await api.getRotation()
    store.setRotation(rotation)
  } catch (err) {
    store.setError(err instanceof Error ? err.message : 'Failed to load settings')
  } finally {
    store.setLoading(false)
  }
}

async function saveRotation() {
  try {
    saving.value = true
    await api.setRotation(store.rotation)
    savedMessage.value = true
    setTimeout(() => {
      savedMessage.value = false
    }, 2000)
  } catch (err) {
    store.setError(err instanceof Error ? err.message : 'Failed to save rotation')
  } finally {
    saving.value = false
  }
}

function updateRotation(event: Event) {
  const value = parseInt((event.target as HTMLSelectElement).value)
  store.setRotation(value)
  saveRotation()
}

onMounted(() => {
  loadSettings()
})
</script>

<template>
  <div class="settings-view">
    <header class="header">
      <h1>Settings</h1>
    </header>

    <div v-if="store.error" class="error">
      {{ store.error }}
      <button @click="store.clearError" class="btn-close">×</button>
    </div>

    <div v-if="savedMessage" class="success">Settings saved successfully!</div>

    <div v-if="store.isLoading" class="loading">Loading...</div>

    <div v-if="!store.isLoading" class="settings-content">
      <section class="setting-section">
        <h2>Display Orientation</h2>
        <p class="description">
          Choose the rotation of your photoframe. This will affect both the display and how new
          images should be cropped.
        </p>

        <div class="setting-control">
          <label for="rotation">Rotation</label>
          <select id="rotation" :value="store.rotation" @change="updateRotation" :disabled="saving">
            <option v-for="option in rotationOptions" :key="option.value" :value="option.value">
              {{ option.label }}
            </option>
          </select>
        </div>

        <div class="info-box">
          <strong>Current rotation:</strong> {{ store.rotation }}°
          <br />
          <strong>Expected dimensions:</strong>
          {{ store.rotation === 0 || store.rotation === 180 ? '800×480' : '480×800' }}
        </div>
      </section>

      <section class="setting-section">
        <h2>Future Features</h2>
        <p class="description disabled">Image rotation frequency (coming soon)</p>
      </section>
    </div>
  </div>
</template>

<style scoped>
.settings-view {
  max-width: 800px;
  margin: 0 auto;
  padding: 2rem;
}

.header {
  margin-bottom: 2rem;
}

h1 {
  font-size: 2rem;
  font-weight: 600;
  color: #1f2937;
  margin: 0;
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

.success {
  background: #d1fae5;
  border: 1px solid #a7f3d0;
  color: #065f46;
  padding: 1rem;
  border-radius: 0.375rem;
  margin-bottom: 1rem;
}

.btn-close {
  background: none;
  border: none;
  font-size: 1.5rem;
  cursor: pointer;
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

.settings-content {
  display: flex;
  flex-direction: column;
  gap: 2rem;
}

.setting-section {
  background: white;
  border: 1px solid #e5e7eb;
  border-radius: 0.5rem;
  padding: 1.5rem;
}

h2 {
  font-size: 1.25rem;
  font-weight: 600;
  color: #1f2937;
  margin: 0 0 0.5rem 0;
}

.description {
  color: #6b7280;
  margin: 0 0 1.5rem 0;
  line-height: 1.5;
}

.description.disabled {
  opacity: 0.5;
  font-style: italic;
}

.setting-control {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  margin-bottom: 1rem;
}

label {
  font-weight: 500;
  color: #374151;
}

select {
  padding: 0.5rem;
  border: 1px solid #d1d5db;
  border-radius: 0.375rem;
  font-size: 1rem;
  background: white;
  cursor: pointer;
}

select:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.info-box {
  background: #f3f4f6;
  border: 1px solid #e5e7eb;
  border-radius: 0.375rem;
  padding: 1rem;
  font-size: 0.875rem;
  color: #4b5563;
  line-height: 1.6;
}
</style>
