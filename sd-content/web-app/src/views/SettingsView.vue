<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { usePhotoFrameStore } from '@/stores/photoframe'
import { api } from '@/services/api'

const store = usePhotoFrameStore()
const saving = ref(false)
const savingSlideshow = ref(false)
const savedMessage = ref(false)

const rotationOptions = [
  { value: 0, label: '0° (Landscape)' },
  { value: 90, label: '90° (Portrait)' },
  { value: 180, label: '180° (Landscape Inverted)' },
  { value: 270, label: '270° (Portrait Inverted)' },
]

const slideshowEnabled = ref(false)
const slideshowIntervalS = ref<number>(3600)

const slideshowOptions = [
  { value: 300, label: '5 minutes' },
  { value: 600, label: '10 minutes' },
  { value: 900, label: '15 minutes' },
  { value: 1800, label: '30 minutes' },
  { value: 3600, label: '1 hour' },
  { value: 10800, label: '3 hours' },
  { value: 21600, label: '6 hours' },
  { value: 86400, label: '1 day' },
  { value: 259200, label: '3 days' },
  { value: 604800, label: '7 days' },
]

function flashSaved() {
  savedMessage.value = true
  setTimeout(() => {
    savedMessage.value = false
  }, 2000)
}

async function loadSettings() {
  try {
    store.setLoading(true)
    store.clearError()

    const [rotation, slideshow] = await Promise.all([api.getRotation(), api.getSlideshow()])
    store.setRotation(rotation)

    slideshowEnabled.value = slideshow.enabled
    slideshowIntervalS.value = slideshow.interval_s
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
    flashSaved()
  } catch (err) {
    store.setError(err instanceof Error ? err.message : 'Failed to save rotation')
  } finally {
    saving.value = false
  }
}

async function saveSlideshow() {
  try {
    savingSlideshow.value = true
    await api.setSlideshow({ enabled: slideshowEnabled.value, interval_s: slideshowIntervalS.value })
    flashSaved()
  } catch (err) {
    store.setError(err instanceof Error ? err.message : 'Failed to save slideshow settings')
  } finally {
    savingSlideshow.value = false
  }
}

function updateRotation(event: Event) {
  const value = parseInt((event.target as HTMLSelectElement).value)
  store.setRotation(value)
  saveRotation()
}

function updateSlideshowEnabled(event: Event) {
  slideshowEnabled.value = (event.target as HTMLInputElement).checked
  saveSlideshow()
}

function updateSlideshowInterval(event: Event) {
  slideshowIntervalS.value = parseInt((event.target as HTMLSelectElement).value)
  saveSlideshow()
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
        <h2>Slideshow</h2>
        <p class="description">Automatically rotate through photos while the frame is asleep.</p>

        <div class="setting-control">
          <label for="slideshow-enabled">Enable slideshow</label>
          <input
            id="slideshow-enabled"
            type="checkbox"
            :checked="slideshowEnabled"
            :disabled="savingSlideshow"
            @change="updateSlideshowEnabled"
          />
        </div>

        <div class="setting-control">
          <label for="slideshow-interval">Rotation interval</label>
          <select
            id="slideshow-interval"
            :value="slideshowIntervalS"
            @change="updateSlideshowInterval"
            :disabled="savingSlideshow || !slideshowEnabled"
          >
            <option v-for="option in slideshowOptions" :key="option.value" :value="option.value">
              {{ option.label }}
            </option>
          </select>
        </div>

        <div class="info-box">
          <strong>Status:</strong> {{ slideshowEnabled ? 'Enabled' : 'Disabled' }}
          <br />
          <strong>Interval (seconds):</strong> {{ slideshowIntervalS }}
        </div>
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
