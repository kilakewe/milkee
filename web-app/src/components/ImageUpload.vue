<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { api, type UploadOrientation } from '@/services/api'
import {
  loadImage,
  calculateCenterCrop,
  cropAndResize,
  fitAndResize,
  imageToBMP,
  rotateImageElement,
  getOrientationFromDimensions,
  getTargetDimensionsForOrientation,
  type CropParams,
  type ImageOrientation,
} from '@/utils/imageProcessing'
import ImageCropper from './ImageCropper.vue'

const props = defineProps<{
  rotation: number
}>()

const emit = defineEmits<{
  close: []
  complete: []
}>()

type Mode = 'crop' | 'scale'

const file = ref<File | null>(null)
const baseImage = ref<HTMLImageElement | null>(null)
const workingImage = ref<HTMLImageElement | null>(null)

const cropParams = ref<CropParams | null>(null)

const previewUrl = ref<string | null>(null)
const processedPreviewUrl = ref<string | null>(null)
const processedBmp = ref<Blob | null>(null)

const showCropper = ref(false)
const processing = ref(false)
const uploading = ref(false)
const error = ref<string | null>(null)

const rotateDeg = ref<0 | 90 | 180 | 270>(0)
const mode = ref<Mode>('crop')

const defaultTargetOrientation = computed<UploadOrientation>(() =>
  props.rotation === 0 || props.rotation === 180 ? 'landscape' : 'portrait',
)

const targetOrientation = ref<'auto' | UploadOrientation>(defaultTargetOrientation.value)

const detectedOrientation = computed<ImageOrientation | null>(() => {
  if (!workingImage.value) return null
  return getOrientationFromDimensions(workingImage.value.width, workingImage.value.height)
})

const effectiveOrientation = computed<UploadOrientation>(() => {
  if (targetOrientation.value !== 'auto') return targetOrientation.value

  const detected = detectedOrientation.value
  if (detected) return detected

  return defaultTargetOrientation.value
})

const targetDims = computed(() => getTargetDimensionsForOrientation(effectiveOrientation.value))

function resetProcessed() {
  processedPreviewUrl.value = null
  processedBmp.value = null
}

function revokePreviewUrl() {
  if (previewUrl.value) {
    URL.revokeObjectURL(previewUrl.value)
    previewUrl.value = null
  }
}

watch(
  () => [workingImage.value, effectiveOrientation.value] as const,
  () => {
    if (!workingImage.value) return

    cropParams.value = calculateCenterCrop(
      workingImage.value.width,
      workingImage.value.height,
      targetDims.value.width,
      targetDims.value.height,
    )

    resetProcessed()
  },
)

watch(mode, () => {
  resetProcessed()
})

let rotateToken = 0
watch(rotateDeg, async (deg) => {
  if (!baseImage.value) return

  const token = ++rotateToken

  try {
    processing.value = true
    error.value = null
    resetProcessed()

    const rotated = await rotateImageElement(baseImage.value, deg)
    if (token !== rotateToken) return

    workingImage.value = rotated
  } catch (err) {
    if (token !== rotateToken) return
    error.value = err instanceof Error ? err.message : 'Failed to rotate image'
  } finally {
    if (token === rotateToken) processing.value = false
  }
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

    reset()

    file.value = selectedFile
    previewUrl.value = URL.createObjectURL(selectedFile)

    baseImage.value = await loadImage(selectedFile)
    rotateDeg.value = 0
    workingImage.value = baseImage.value

    // Default to matching the current frame orientation, but allow Auto.
    targetOrientation.value = defaultTargetOrientation.value

    cropParams.value = calculateCenterCrop(
      workingImage.value.width,
      workingImage.value.height,
      targetDims.value.width,
      targetDims.value.height,
    )
  } catch (err) {
    error.value = err instanceof Error ? err.message : 'Failed to load image'
  } finally {
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

function processImage() {
  if (!workingImage.value) return
  if (mode.value === 'crop' && !cropParams.value) return

  try {
    processing.value = true
    error.value = null
    resetProcessed()

    const imageData =
      mode.value === 'crop'
        ? cropAndResize(workingImage.value, targetDims.value.width, targetDims.value.height, cropParams.value!)
        : fitAndResize(workingImage.value, targetDims.value.width, targetDims.value.height)

    // NOTE: Dithering happens on-device. We upload the processed 24-bit BMP as-is.
    processedPreviewUrl.value = imageDataToDataURL(imageData)
    processedBmp.value = imageToBMP(imageData)
  } catch (err) {
    error.value = err instanceof Error ? err.message : 'Failed to process image'
  } finally {
    processing.value = false
  }
}

function handleCropUpdate(newCrop: CropParams) {
  cropParams.value = newCrop
  showCropper.value = false
  processImage()
}

async function uploadImage() {
  if (!processedBmp.value) return

  try {
    uploading.value = true
    error.value = null

    await api.uploadPhoto(effectiveOrientation.value, processedBmp.value)
    emit('complete')
  } catch (err) {
    error.value = err instanceof Error ? err.message : 'Failed to upload image'
  } finally {
    uploading.value = false
  }
}

function reset() {
  file.value = null
  baseImage.value = null
  workingImage.value = null
  cropParams.value = null
  showCropper.value = false
  error.value = null
  rotateDeg.value = 0
  mode.value = 'crop'
  targetOrientation.value = defaultTargetOrientation.value

  resetProcessed()
  revokePreviewUrl()
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
            <strong>Detected:</strong> {{ detectedOrientation || '—' }}
          </div>
          <div class="info-row">
            <strong>Target:</strong> {{ effectiveOrientation }} ({{ targetDims.width }}×{{ targetDims.height }})
          </div>
          <div class="info-row"><strong>Mode:</strong> {{ mode }}</div>
          <div class="info-row"><strong>Rotate:</strong> {{ rotateDeg }}°</div>
          <div class="info-hint">Crop = cover (fills target). Scale = contain (letterbox with white background). Dithering happens on-device.</div>
        </div>

        <div class="setting-row">
          <label for="target-orientation">Target orientation</label>
          <select
            id="target-orientation"
            :value="targetOrientation"
            :disabled="processing || uploading"
            @change="targetOrientation = (($event.target as HTMLSelectElement).value as any)"
          >
            <option value="auto">auto (use detected)</option>
            <option value="landscape">landscape (800×480)</option>
            <option value="portrait">portrait (480×800)</option>
            <option value="square">square (480×480)</option>
          </select>
        </div>

        <div class="setting-row">
          <label for="mode">Mode</label>
          <select
            id="mode"
            :value="mode"
            :disabled="processing || uploading"
            @change="mode = (($event.target as HTMLSelectElement).value as any)"
          >
            <option value="crop">crop (fill)</option>
            <option value="scale">scale (fit)</option>
          </select>
        </div>

        <div class="setting-row">
          <label>Rotate</label>
          <div class="rotate-buttons">
            <button class="btn-secondary btn-small" :disabled="processing || uploading" @click="rotateDeg = 0">0°</button>
            <button class="btn-secondary btn-small" :disabled="processing || uploading" @click="rotateDeg = 90">90°</button>
            <button class="btn-secondary btn-small" :disabled="processing || uploading" @click="rotateDeg = 180">180°</button>
            <button class="btn-secondary btn-small" :disabled="processing || uploading" @click="rotateDeg = 270">270°</button>
          </div>
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

        <div v-else-if="file && !processedPreviewUrl" class="preview-section">
          <div class="preview-container">
            <img :src="previewUrl || undefined" alt="Preview" class="preview-image" />
          </div>
          <div class="button-group">
            <button @click="reset" class="btn-secondary" :disabled="processing">Cancel</button>
            <button
              v-if="mode === 'crop'"
              @click="showCropper = true"
              class="btn-secondary"
              :disabled="processing || !workingImage || !cropParams"
            >
              Adjust Crop
            </button>
            <button @click="processImage" class="btn-primary" :disabled="processing">
              {{ processing ? 'Processing...' : 'Process & Dither' }}
            </button>
          </div>
        </div>

        <div v-else class="preview-section">
          <div class="preview-panel">
            <div class="preview-title">Processed ({{ effectiveOrientation }}) — device will dither</div>
            <div class="preview-container">
              <img :src="processedPreviewUrl || undefined" alt="Processed dithered preview" class="preview-image" />
            </div>
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
      v-if="showCropper && workingImage && cropParams && mode === 'crop'"
      :image="workingImage"
      :target-width="targetDims.width"
      :target-height="targetDims.height"
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

.rotate-buttons {
  display: flex;
  gap: 0.5rem;
  flex-wrap: wrap;
}

.btn-small {
  padding: 0.4rem 0.75rem;
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
