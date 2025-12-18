<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { usePhotoFrameStore } from '@/stores/photoframe'
import { useToast } from '@/composables/useToast'
import { api } from '@/services/api'
import LoadingOverlay from '@/components/LoadingOverlay.vue'

const store = usePhotoFrameStore()
const toast = useToast()
const saving = ref(false)
const savingSlideshow = ref(false)

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

async function loadSettings() {
  try {
    store.setLoading(true)

    const [rotation, slideshow] = await Promise.all([api.getRotation(), api.getSlideshow()])
    store.setRotation(rotation)

    slideshowEnabled.value = slideshow.enabled
    slideshowIntervalS.value = slideshow.interval_s
  } catch (err) {
    toast.error('Failed to load settings', err instanceof Error ? err.message : undefined)
  } finally {
    store.setLoading(false)
  }
}

async function saveRotation() {
  try {
    saving.value = true
    await api.setRotation(store.rotation)
    toast.success('Rotation saved', `Display orientation set to ${store.rotation}°`)
  } catch (err) {
    toast.error('Failed to save rotation', err instanceof Error ? err.message : undefined)
  } finally {
    saving.value = false
  }
}

async function saveSlideshow() {
  try {
    savingSlideshow.value = true
    await api.setSlideshow({ enabled: slideshowEnabled.value, interval_s: slideshowIntervalS.value })
    toast.success('Slideshow settings saved')
  } catch (err) {
    toast.error('Failed to save slideshow settings', err instanceof Error ? err.message : undefined)
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
  <div class="max-w-4xl mx-auto p-4 md:p-8">
    <div class="overflow-hidden rounded-lg bg-white shadow-sm relative">
      <LoadingOverlay :loading="store.isLoading" />
      <div class="px-4 py-5 sm:p-6">
        <header class="mb-6 md:mb-8">
          <h1 class="text-2xl md:text-3xl font-semibold text-pf-dark">Settings</h1>
        </header>

        <div class="flex flex-col gap-6">
          <section class="bg-gray-50 border border-gray-200 rounded-lg p-4 md:p-6">
            <h2 class="text-xl font-semibold text-pf-dark mb-2">Display Orientation</h2>
            <p class="text-pf-secondary text-sm mb-4 leading-relaxed">
              Choose the rotation of your photoframe. This will affect both the display and how new
              images should be cropped.
            </p>

            <div class="flex flex-col gap-2 mb-4">
              <label for="rotation" class="font-medium text-pf-secondary text-sm">Rotation</label>
              <select 
                id="rotation" 
                :value="store.rotation" 
                @change="updateRotation" 
                :disabled="saving"
                class="px-3 py-2 border border-gray-300 rounded-md bg-white text-pf-dark disabled:opacity-50 disabled:cursor-not-allowed touch-manipulation"
              >
                <option v-for="option in rotationOptions" :key="option.value" :value="option.value">
                  {{ option.label }}
                </option>
              </select>
            </div>

            <div class="bg-blue-50 border border-blue-200 rounded-md p-3 text-sm text-pf-secondary">
              <strong class="text-pf-dark">Current rotation:</strong> {{ store.rotation }}°
              <br />
              <strong class="text-pf-dark">Expected dimensions:</strong>
              {{ store.rotation === 0 || store.rotation === 180 ? '800×480' : '480×800' }}
            </div>
          </section>

          <section class="bg-gray-50 border border-gray-200 rounded-lg p-4 md:p-6">
            <h2 class="text-xl font-semibold text-pf-dark mb-2">Slideshow</h2>
            <p class="text-pf-secondary text-sm mb-4 leading-relaxed">Automatically rotate through photos while the frame is asleep.</p>

            <div class="flex items-center gap-3 mb-4">
              <input
                id="slideshow-enabled"
                type="checkbox"
                :checked="slideshowEnabled"
                :disabled="savingSlideshow"
                @change="updateSlideshowEnabled"
                class="w-5 h-5 rounded border-gray-300 text-pf-primary focus:ring-pf-primary disabled:opacity-50 disabled:cursor-not-allowed touch-manipulation"
              />
              <label for="slideshow-enabled" class="font-medium text-pf-secondary text-sm">Enable slideshow</label>
            </div>

            <div class="flex flex-col gap-2 mb-4">
              <label for="slideshow-interval" class="font-medium text-pf-secondary text-sm">Rotation interval</label>
              <select
                id="slideshow-interval"
                :value="slideshowIntervalS"
                @change="updateSlideshowInterval"
                :disabled="savingSlideshow || !slideshowEnabled"
                class="px-3 py-2 border border-gray-300 rounded-md bg-white text-pf-dark disabled:opacity-50 disabled:cursor-not-allowed touch-manipulation"
              >
                <option v-for="option in slideshowOptions" :key="option.value" :value="option.value">
                  {{ option.label }}
                </option>
              </select>
            </div>

            <div class="bg-blue-50 border border-blue-200 rounded-md p-3 text-sm text-pf-secondary">
              <strong class="text-pf-dark">Status:</strong> {{ slideshowEnabled ? 'Enabled' : 'Disabled' }}
              <br />
              <strong class="text-pf-dark">Interval (seconds):</strong> {{ slideshowIntervalS }}
            </div>
          </section>
        </div>
      </div>
    </div>
  </div>
</template>
