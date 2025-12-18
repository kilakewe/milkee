<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import type { CropParams } from '@/utils/imageProcessing'

const props = defineProps<{
  image: HTMLImageElement
  targetWidth: number
  targetHeight: number
  initialCrop: CropParams
}>()

const emit = defineEmits<{
  update: [crop: CropParams]
  close: []
}>()

const canvasRef = ref<HTMLCanvasElement | null>(null)
const containerRef = ref<HTMLDivElement | null>(null)

const crop = ref<CropParams>({ ...props.initialCrop })
const isDragging = ref(false)
const dragStart = ref({ x: 0, y: 0 })

const scale = ref(1)
const canvasWidth = ref(800)
const canvasHeight = ref(480)

// Calculate display dimensions
function calculateDisplaySize() {
  if (!containerRef.value) return

  const maxWidth = Math.min(containerRef.value.clientWidth - 32, 800)
  const maxHeight = 600

  const imgAspect = props.image.width / props.image.height

  let width = maxWidth
  let height = maxWidth / imgAspect

  if (height > maxHeight) {
    height = maxHeight
    width = height * imgAspect
  }

  canvasWidth.value = width
  canvasHeight.value = height
  scale.value = width / props.image.width
}

// Draw the image with crop overlay
function draw() {
  const canvas = canvasRef.value
  if (!canvas) return

  const ctx = canvas.getContext('2d')
  if (!ctx) return

  // Clear canvas
  ctx.clearRect(0, 0, canvas.width, canvas.height)

  // Draw full image
  ctx.drawImage(props.image, 0, 0, canvas.width, canvas.height)

  // Draw dark overlay
  ctx.fillStyle = 'rgba(0, 0, 0, 0.5)'
  ctx.fillRect(0, 0, canvas.width, canvas.height)

  // Clear crop area
  const sx = crop.value.sx * scale.value
  const sy = crop.value.sy * scale.value
  const sw = crop.value.sw * scale.value
  const sh = crop.value.sh * scale.value

  ctx.clearRect(sx, sy, sw, sh)
  ctx.drawImage(props.image, sx / scale.value, sy / scale.value, sw / scale.value, sh / scale.value, sx, sy, sw, sh)

  // Draw crop border
  ctx.strokeStyle = '#3b82f6'
  ctx.lineWidth = 2
  ctx.strokeRect(sx, sy, sw, sh)

  // Draw corner handles
  const handleSize = 12
  ctx.fillStyle = '#3b82f6'
  const corners = [
    { x: sx, y: sy },
    { x: sx + sw, y: sy },
    { x: sx, y: sy + sh },
    { x: sx + sw, y: sy + sh },
  ]
  for (const corner of corners) {
    ctx.fillRect(corner.x - handleSize / 2, corner.y - handleSize / 2, handleSize, handleSize)
  }

  // Draw center crosshair
  ctx.strokeStyle = '#3b82f6'
  ctx.lineWidth = 1
  const cx = sx + sw / 2
  const cy = sy + sh / 2
  const crossSize = 20
  ctx.beginPath()
  ctx.moveTo(cx - crossSize, cy)
  ctx.lineTo(cx + crossSize, cy)
  ctx.moveTo(cx, cy - crossSize)
  ctx.lineTo(cx, cy + crossSize)
  ctx.stroke()
}

function handleMouseDown(e: MouseEvent) {
  const canvas = canvasRef.value
  if (!canvas) return

  const rect = canvas.getBoundingClientRect()
  const x = ((e.clientX - rect.left) / scale.value)
  const y = ((e.clientY - rect.top) / scale.value)

  // Check if click is inside crop area
  if (
    x >= crop.value.sx &&
    x <= crop.value.sx + crop.value.sw &&
    y >= crop.value.sy &&
    y <= crop.value.sy + crop.value.sh
  ) {
    isDragging.value = true
    dragStart.value = { x: x - crop.value.sx, y: y - crop.value.sy }
    canvas.style.cursor = 'move'
  }
}

function handleMouseMove(e: MouseEvent) {
  if (!isDragging.value) return

  const canvas = canvasRef.value
  if (!canvas) return

  const rect = canvas.getBoundingClientRect()
  const x = (e.clientX - rect.left) / scale.value
  const y = (e.clientY - rect.top) / scale.value

  // Calculate new position
  let newSx = x - dragStart.value.x
  let newSy = y - dragStart.value.y

  // Constrain to image bounds
  newSx = Math.max(0, Math.min(newSx, props.image.width - crop.value.sw))
  newSy = Math.max(0, Math.min(newSy, props.image.height - crop.value.sh))

  crop.value.sx = Math.round(newSx)
  crop.value.sy = Math.round(newSy)

  draw()
}

function handleMouseUp() {
  if (isDragging.value) {
    isDragging.value = false
    if (canvasRef.value) {
      canvasRef.value.style.cursor = 'default'
    }
  }
}

function handleWheel(e: WheelEvent) {
  e.preventDefault()

  const delta = e.deltaY > 0 ? 1.1 : 0.9
  const newSw = crop.value.sw * delta
  const newSh = crop.value.sh * delta

  // Check bounds
  if (newSw > props.image.width || newSh > props.image.height) return
  if (newSw < 50 || newSh < 50) return

  // Adjust position to zoom from center
  const centerX = crop.value.sx + crop.value.sw / 2
  const centerY = crop.value.sy + crop.value.sh / 2

  crop.value.sw = Math.round(newSw)
  crop.value.sh = Math.round(newSh)
  crop.value.sx = Math.round(centerX - crop.value.sw / 2)
  crop.value.sy = Math.round(centerY - crop.value.sh / 2)

  // Constrain to bounds
  crop.value.sx = Math.max(0, Math.min(crop.value.sx, props.image.width - crop.value.sw))
  crop.value.sy = Math.max(0, Math.min(crop.value.sy, props.image.height - crop.value.sh))

  draw()
}

function resetCrop() {
  crop.value = { ...props.initialCrop }
  draw()
}

function applyCrop() {
  emit('update', crop.value)
}

onMounted(() => {
  calculateDisplaySize()
  draw()

  window.addEventListener('resize', () => {
    calculateDisplaySize()
    draw()
  })
  window.addEventListener('mousemove', handleMouseMove)
  window.addEventListener('mouseup', handleMouseUp)
})

onUnmounted(() => {
  window.removeEventListener('mousemove', handleMouseMove)
  window.removeEventListener('mouseup', handleMouseUp)
})
</script>

<template>
  <div class="cropper-overlay" @click.self="emit('close')">
    <div class="cropper-dialog">
      <header class="dialog-header">
        <h2>Adjust Crop</h2>
        <button @click="emit('close')" class="btn-close">×</button>
      </header>

      <div class="dialog-content" ref="containerRef">
        <div class="canvas-container">
          <canvas
            ref="canvasRef"
            :width="canvasWidth"
            :height="canvasHeight"
            @mousedown="handleMouseDown"
            @wheel="handleWheel"
          />
        </div>

        <div class="instructions">
          <p><strong>Instructions:</strong></p>
          <ul>
            <li><strong>Drag</strong> the highlighted area to reposition</li>
            <li><strong>Scroll</strong> to zoom in/out</li>
            <li>The blue rectangle shows what will be uploaded</li>
          </ul>
        </div>
      </div>

      <footer class="dialog-footer">
        <button @click="resetCrop" class="btn-secondary">Reset</button>
        <button @click="applyCrop" class="btn-primary">Apply Crop</button>
      </footer>
    </div>
  </div>
</template>

<style scoped>
.cropper-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.7);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1001;
  padding: 1rem;
}

.cropper-dialog {
  background: white;
  border-radius: 0.5rem;
  max-width: 900px;
  width: 100%;
  max-height: 90vh;
  display: flex;
  flex-direction: column;
  box-shadow: 0 20px 25px -5px rgba(0, 0, 0, 0.3);
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
  flex: 1;
}

.canvas-container {
  display: flex;
  justify-content: center;
  margin-bottom: 1.5rem;
}

canvas {
  border: 1px solid #d1d5db;
  border-radius: 0.375rem;
  cursor: default;
  user-select: none;
}

.instructions {
  background: #f3f4f6;
  border: 1px solid #e5e7eb;
  border-radius: 0.375rem;
  padding: 1rem;
  font-size: 0.875rem;
  color: #4b5563;
}

.instructions p {
  margin: 0 0 0.5rem 0;
}

.instructions ul {
  margin: 0;
  padding-left: 1.5rem;
}

.instructions li {
  margin-bottom: 0.25rem;
}

.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 1rem;
  padding: 1.5rem;
  border-top: 1px solid #e5e7eb;
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

.btn-primary:hover {
  background: #2563eb;
}

.btn-secondary {
  background: #f3f4f6;
  color: #374151;
}

.btn-secondary:hover {
  background: #e5e7eb;
}
</style>
