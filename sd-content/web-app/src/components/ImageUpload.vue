<script setup lang="ts">
import { ref, computed } from 'vue'
import { api } from '@/services/api'
import {
  loadImage,
  calculateCenterCrop,
  cropAndResize,
  ditherImage,
  imageToBMP,
  getTargetDimensions,
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
const ditheredPreviewUrl = ref<string | null>(null)
const showCropper = ref(false)
const processing = ref(false)
const uploading = ref(false)
const error = ref<string | null>(null)

const targetDimensions = computed(() => getTargetDimensions(props.rotation))

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

    file.value = selectedFile
    loadedImage.value = await loadImage(selectedFile)
    previewUrl.value = URL.createObjectURL(selectedFile)
    ditheredPreviewUrl.value = null

    // Calculate default center crop
    cropParams.value = calculateCenterCrop(
      loadedImage.value.width,
      loadedImage.value.height,
      targetDimensions.value.width,
      targetDimensions.value.height,
    )

    processing.value = false
  } catch (err) {
    error.value = err instanceof Error ? err.message : 'Failed to load image'
    processing.value = false
  }
}

function processImage() {
  if (!loadedImage.value || !cropParams.value) return

  try {
    processing.value = true
    error.value = null

    const imageData = cropAndResize(
      loadedImage.value,
      targetDimensions.value.width,
      targetDimensions.value.height,
      cropParams.value,
    )

    const dithered = ditherImage(imageData)

    const canvas = document.createElement('canvas')
    canvas.width = dithered.width
    canvas.height = dithered.height
    const ctx = canvas.getContext('2d')
    if (ctx) {
      ctx.putImageData(dithered, 0, 0)
      ditheredPreviewUrl.value = canvas.toDataURL()
    }

    processing.value = false
  } catch (err) {
    error.value = err instanceof Error ? err.message : 'Failed to process image'
    processing.value = false
  }
}

function handleCropUpdate(newCrop: CropParams) {
  cropParams.value = newCrop
  showCropper.value = false
  processImage()
}

async function uploadImage() {
  if (!loadedImage.value || !cropParams.value || !ditheredPreviewUrl.value) return

  try {
    uploading.value = true
    error.value = null

    const imageData = cropAndResize(
      loadedImage.value,
      targetDimensions.value.width,
      targetDimensions.value.height,
      cropParams.value,
    )
    const dithered = ditherImage(imageData)
    const bmpBlob = imageToBMP(dithered)

    const result = await api.uploadImage(bmpBlob)

    if (result.includes('成功')) {
      emit('complete')
    } else {
      error.value = 'Upload failed: ' + result
    }
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
  ditheredPreviewUrl.value = null
  showCropper.value = false
  error.value = null
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
          <strong>Target dimensions:</strong> {{ targetDimensions.width }}×{{ targetDimensions.height }}
          ({{ rotation === 0 || rotation === 180 ? 'Landscape' : 'Portrait' }})
        </div>

        <div v-if="error" class="error">{{ error }}</div>

        <div v-if="!file" class="upload-area">
          <input type="file" id="file-input" accept="image/*" @change="handleFileSelect" class="file-input" />
          <label for="file-input" class="file-label">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor" class="upload-icon">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M7 16a4 4 0 01-.88-7.903A5 5 0 1115.9 6L16 6a5 5 0 011 9.9M15 13l-3-3m0 0l-3 3m3-3v12" />
            </svg>
            <span class="upload-text">Click to select an image</span>
            <span class="upload-hint">JPEG, PNG, BMP, GIF, or WebP</span>
          </label>
        </div>

        <div v-if="file && !ditheredPreviewUrl" class="preview-section">
          <div class="preview-container">
            <img :src="previewUrl || undefined" alt="Preview" class="preview-image" />
          </div>
          <div class="button-group">
            <button @click="reset" class="btn-secondary" :disabled="processing">Cancel</button>
            <button @click="showCropper = true" class="btn-secondary" :disabled="processing">Adjust Crop</button>
            <button @click="processImage" class="btn-primary" :disabled="processing">
              {{ processing ? 'Processing...' : 'Process & Dither' }}
            </button>
          </div>
        </div>

        <div v-if="ditheredPreviewUrl" class="preview-section">
          <div class="preview-container">
            <img :src="ditheredPreviewUrl || undefined" alt="Dithered preview" class="preview-image" />
          </div>
          <div class="button-group">
            <button @click="reset" class="btn-secondary" :disabled="uploading">Start Over</button>
            <button @click="uploadImage" class="btn-primary" :disabled="uploading">
              {{ uploading ? 'Uploading...' : 'Upload to Frame' }}
            </button>
          </div>
        </div>
      </div>
    </div>

    <ImageCropper
      v-if="showCropper && loadedImage && cropParams"
      :image="loadedImage"
      :target-width="targetDimensions.width"
      :target-height="targetDimensions.height"
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
