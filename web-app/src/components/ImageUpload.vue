<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { api, type UploadVariant } from '@/services/api'
import {
  loadImage,
  calculateCenterCrop,
  cropAndResize,
  fitAndResize,
  ditherImage,
  imageToBMP,
  type CropParams,
} from '@/utils/imageProcessing'
import ImageCropper from './ImageCropper.vue'

const props = defineProps<{
  rotation: number
}>()

const emit = defineEmits<{
  close: []
  complete: []
}>()

const file = ref<File | null>(null)
const loadedImage = ref<HTMLImageElement | null>(null)
const cropParams = ref<CropParams | null>(null)
const previewUrl = ref<string | null>(null)

const primaryVariant = ref<UploadVariant>(
  props.rotation === 0 || props.rotation === 180 ? 'landscape' : 'portrait',
)
const alternateVariant = computed<UploadVariant>(() =>
  primaryVariant.value === 'landscape' ? 'portrait' : 'landscape',
)

const primaryDims = computed(() =>
  primaryVariant.value === 'landscape' ? { width: 800, height: 480 } : { width: 480, height: 800 },
)
const alternateDims = computed(() =>
  alternateVariant.value === 'landscape' ? { width: 800, height: 480 } : { width: 480, height: 800 },
)

const primaryPreviewUrl = ref<string | null>(null)
const alternatePreviewUrl = ref<string | null>(null)
const primaryBmp = ref<Blob | null>(null)
const alternateBmp = ref<Blob | null>(null)

const showCropper = ref(false)
const processing = ref(false)
const uploading = ref(false)
const error = ref<string | null>(null)

function resetProcessed() {
  primaryPreviewUrl.value = null
  alternatePreviewUrl.value = null
  primaryBmp.value = null
  alternateBmp.value = null
}

watch(primaryVariant, () => {
  if (!loadedImage.value) return

  cropParams.value = calculateCenterCrop(
    loadedImage.value.width,
    loadedImage.value.height,
    primaryDims.value.width,
    primaryDims.value.height,
  )

  resetProcessed()
})

async function handleFileSelect(event: Event) {
  const input = event.target as HTMLInputElement
  if (!input.files || !input.files[0]) return

  const selectedFile = input.files[0]

  if (!selectedFile.type.match(/image\/(jpeg|jpg|png|bmp|gif|webp)/)) {
    error.value = 'Please select a valid image file'
    return
  }

  try {
    processing.value = true
    error.value = null
    resetProcessed()

    file.value = selectedFile
    loadedImage.value = await loadImage(selectedFile)
    previewUrl.value = URL.createObjectURL(selectedFile)

    cropParams.value = calculateCenterCrop(
      loadedImage.value.width,
      loadedImage.value.height,
      primaryDims.value.width,
      primaryDims.value.height,
    )

    processing.value = false
  } catch (err) {
    error.value = err instanceof Error ? err.message : 'Failed to load image'
    processing.value = false
  }
}

function imageDataToDataURL(imageData: ImageData): string {
  const canvas = document.createElement('canvas')
  canvas.width = imageData.width
  canvas.height = imageData.height
  const ctx = canvas.getContext('2d')
  if (!ctx) {
    throw new Error('Failed to get canvas context')
  }
  ctx.putImageData(imageData, 0, 0)
  return canvas.toDataURL()
}

function processImages() {
  if (!loadedImage.value || !cropParams.value) return

  try {
    processing.value = true
    error.value = null
    resetProcessed()

    // Primary variant: crop-to-fill.
    const primaryImage = cropAndResize(
      loadedImage.value,
      primaryDims.value.width,
      primaryDims.value.height,
      cropParams.value,
    )
    const primaryDithered = ditherImage(primaryImage)
    primaryPreviewUrl.value = imageDataToDataURL(primaryDithered)
    primaryBmp.value = imageToBMP(primaryDithered)

    // Alternate variant: fit-to-contain (letterbox with white background).
    const altImage = fitAndResize(loadedImage.value, alternateDims.value.width, alternateDims.value.height)
    const altDithered = ditherImage(altImage)
    alternatePreviewUrl.value = imageDataToDataURL(altDithered)
    alternateBmp.value = imageToBMP(altDithered)

    processing.value = false
  } catch (err) {
    error.value = err instanceof Error ? err.message : 'Failed to process image'
    processing.value = false
  }
}

function handleCropUpdate(newCrop: CropParams) {
  cropParams.value = newCrop
  showCropper.value = false
  processImages()
}

async function uploadImages() {
  if (!primaryBmp.value || !alternateBmp.value) return

  try {
    uploading.value = true
    error.value = null

    // Upload primary first (allocates an id), then upload alternate variant using that id.
    const first = await api.uploadPhotoVariant(primaryVariant.value, primaryBmp.value)
    await api.uploadPhotoVariant(alternateVariant.value, alternateBmp.value, first.id)

    emit('complete')
  } catch (err) {
    error.value = err instanceof Error ? err.message : 'Failed to upload image'
  } finally {
    uploading.value = false
  }
}

function reset() {
  file.value = null
  loadedImage.value = null
  cropParams.value = null
  previewUrl.value = null
  showCropper.value = false
  error.value = null
  resetProcessed()
}
</script>

<template>
  <div class="upload-overlay" @click.self="emit('close')">
    <div class="upload-dialog">
      <header class="dialog-header">
        <h2>Upload New Image</h2>
        <button @click="emit('close')" class="btn-close">×</button>
      </header>

      <div class="dialog-content">
        <div class="info-box">
          <div class="info-row">
            <strong>Primary:</strong> {{ primaryVariant }} ({{ primaryDims.width }}×{{ primaryDims.height }})
          </div>
          <div class="info-row">
            <strong>Alternate:</strong> {{ alternateVariant }} ({{ alternateDims.width }}×{{ alternateDims.height }})
          </div>
          <div class="info-hint">Primary uses crop-to-fill. Alternate uses fit-to-frame with a white background.</div>
        </div>

        <div class="setting-row">
          <label for="primary-variant">Primary orientation</label>
          <select
            id="primary-variant"
            :value="primaryVariant"
            :disabled="processing || uploading"
            @change="primaryVariant = (($event.target as HTMLSelectElement).value as UploadVariant)"
          >
            <option value="landscape">landscape (800×480)</option>
            <option value="portrait">portrait (480×800)</option>
          </select>
        </div>

        <div v-if="error" class="error">{{ error }}</div>

        <div v-if="!file" class="upload-area">
          <input type="file" id="file-input" accept="image/*" @change="handleFileSelect" class="file-input" />
          <label for="file-input" class="file-label">
            <svg
              xmlns="http://www.w3.org/2000/svg"
              fill="none"
              viewBox="0 0 24 24"
              stroke="currentColor"
              class="upload-icon"
            >
              <path
                stroke-linecap="round"
                stroke-linejoin="round"
                stroke-width="2"
                d="M7 16a4 4 0 01-.88-7.903A5 5 0 1115.9 6L16 6a5 5 0 011 9.9M15 13l-3-3m0 0l-3 3m3-3v12"
              />
            </svg>
            <span class="upload-text">Click to select an image</span>
            <span class="upload-hint">JPEG, PNG, BMP, GIF, or WebP</span>
          </label>
        </div>

        <div v-if="file && !primaryPreviewUrl" class="preview-section">
          <div class="preview-container">
            <img :src="previewUrl || undefined" alt="Preview" class="preview-image" />
          </div>
          <div class="button-group">
            <button @click="reset" class="btn-secondary" :disabled="processing">Cancel</button>
            <button @click="showCropper = true" class="btn-secondary" :disabled="processing">Adjust Crop</button>
            <button @click="processImages" class="btn-primary" :disabled="processing">
              {{ processing ? 'Processing...' : 'Process & Dither' }}
            </button>
          </div>
        </div>

        <div v-if="primaryPreviewUrl && alternatePreviewUrl" class="preview-section">
          <div class="preview-grid">
            <div class="preview-panel">
              <div class="preview-title">Primary ({{ primaryVariant }})</div>
              <div class="preview-container">
                <img :src="primaryPreviewUrl || undefined" alt="Primary dithered preview" class="preview-image" />
              </div>
            </div>

            <div class="preview-panel">
              <div class="preview-title">Alternate ({{ alternateVariant }})</div>
              <div class="preview-container">
                <img
                  :src="alternatePreviewUrl || undefined"
                  alt="Alternate dithered preview"
                  class="preview-image"
                />
              </div>
            </div>
          </div>

          <div class="button-group">
            <button @click="reset" class="btn-secondary" :disabled="uploading">Start Over</button>
            <button @click="uploadImages" class="btn-primary" :disabled="uploading">
              {{ uploading ? 'Uploading...' : 'Upload to Frame' }}
            </button>
          </div>
        </div>
      </div>
    </div>

    <ImageCropper
      v-if="showCropper && loadedImage && cropParams"
      :image="loadedImage"
      :target-width="primaryDims.width"
      :target-height="primaryDims.height"
      :initial-crop="cropParams"
      @update="handleCropUpdate"
      @close="showCropper = false"
    />
  </div>
</template>

<style scoped>
.upload-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
  padding: 1rem;
}

.upload-dialog {
  background: white;
  border-radius: 0.5rem;
  max-width: 600px;
  width: 100%;
  max-height: 90vh;
  display: flex;
  flex-direction: column;
  box-shadow: 0 20px 25px -5px rgba(0, 0, 0, 0.1);
}

.dialog-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 1.5rem;
  border-bottom: 1px solid #e5e7eb;
}

h2 {
  font-size: 1.5rem;
  font-weight: 600;
  color: #1f2937;
  margin: 0;
}

.btn-close {
  background: none;
  border: none;
  font-size: 2rem;
  cursor: pointer;
  color: #6b7280;
  padding: 0;
  width: 2rem;
  height: 2rem;
}

.dialog-content {
  padding: 1.5rem;
  overflow-y: auto;
}

.info-box {
  background: #eff6ff;
  border: 1px solid #bfdbfe;
  border-radius: 0.375rem;
  padding: 1rem;
  margin-bottom: 1.5rem;
  color: #1e40af;
  font-size: 0.875rem;
}

.info-row + .info-row {
  margin-top: 0.25rem;
}

.info-hint {
  margin-top: 0.5rem;
  color: #1e40af;
  opacity: 0.9;
  font-size: 0.8rem;
}

.setting-row {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  margin-bottom: 1rem;
}

.setting-row label {
  font-weight: 500;
  color: #374151;
}

.setting-row select {
  padding: 0.5rem;
  border: 1px solid #d1d5db;
  border-radius: 0.375rem;
  background: white;
}

.preview-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 1rem;
}

@media (max-width: 640px) {
  .preview-grid {
    grid-template-columns: 1fr;
  }
}

.preview-title {
  font-size: 0.875rem;
  font-weight: 600;
  color: #374151;
  margin-bottom: 0.5rem;
}

.error {
  background: #fee;
  border: 1px solid #fcc;
  color: #c00;
  padding: 1rem;
  border-radius: 0.375rem;
  margin-bottom: 1rem;
}

.upload-area {
  position: relative;
}

.file-input {
  display: none;
}

.file-label {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 3rem;
  border: 2px dashed #d1d5db;
  border-radius: 0.5rem;
  cursor: pointer;
  transition: all 0.2s;
}

.file-label:hover {
  border-color: #3b82f6;
  background: #f9fafb;
}

.upload-icon {
  width: 3rem;
  height: 3rem;
  color: #9ca3af;
  margin-bottom: 1rem;
}

.upload-text {
  font-size: 1rem;
  font-weight: 500;
  color: #374151;
  margin-bottom: 0.5rem;
}

.upload-hint {
  font-size: 0.875rem;
  color: #6b7280;
}

.preview-section {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.preview-container {
  border: 1px solid #e5e7eb;
  border-radius: 0.5rem;
  overflow: hidden;
  background: #f9fafb;
}

.preview-image {
  width: 100%;
  height: auto;
  display: block;
}

.button-group {
  display: flex;
  gap: 1rem;
  justify-content: flex-end;
}

.btn-primary,
.btn-secondary {
  padding: 0.625rem 1.25rem;
  border: none;
  border-radius: 0.375rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
}

.btn-primary {
  background: #3b82f6;
  color: white;
}

.btn-primary:hover:not(:disabled) {
  background: #2563eb;
}

.btn-secondary {
  background: #f3f4f6;
  color: #374151;
}

.btn-secondary:hover:not(:disabled) {
  background: #e5e7eb;
}

.btn-primary:disabled,
.btn-secondary:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
</style>
