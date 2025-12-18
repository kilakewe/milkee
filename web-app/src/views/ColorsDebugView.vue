<script setup lang="ts">
import { ref, onMounted } from 'vue'

interface ColorInfo {
  name: string
  cssVar: string
  tailwindClass: string
  hex: string
  description: string
}

const colors = ref<ColorInfo[]>([
  { name: 'Light', cssVar: '--color-pf-light', tailwindClass: 'pf-light', hex: '', description: 'Light background, borders' },
  { name: 'Dark', cssVar: '--color-pf-dark', tailwindClass: 'pf-dark', hex: '', description: 'Main background, primary text' },
  { name: 'Primary', cssVar: '--color-pf-primary', tailwindClass: 'pf-primary', hex: '', description: 'Primary actions, links' },
  { name: 'Secondary', cssVar: '--color-pf-secondary', tailwindClass: 'pf-secondary', hex: '', description: 'Secondary text, muted elements' },
  { name: 'Info', cssVar: '--color-pf-info', tailwindClass: 'pf-info', hex: '', description: 'Info messages, accents' },
  { name: 'Success', cssVar: '--color-pf-success', tailwindClass: 'pf-success', hex: '', description: 'Success states, confirmations' },
  { name: 'Warning', cssVar: '--color-pf-warning', tailwindClass: 'pf-warning', hex: '', description: 'Warnings, cautions' },
  { name: 'Danger', cssVar: '--color-pf-danger', tailwindClass: 'pf-danger', hex: '', description: 'Errors, destructive actions' },
])

onMounted(() => {
  // Read the actual color values from CSS variables
  const style = getComputedStyle(document.documentElement)
  colors.value.forEach(color => {
    color.hex = style.getPropertyValue(color.cssVar).trim()
  })
})

function copyToClipboard(text: string) {
  navigator.clipboard.writeText(text)
}
</script>

<template>
  <div class="max-w-7xl mx-auto p-4 md:p-8">
    <div class="overflow-hidden rounded-lg bg-white shadow-sm">
      <div class="px-4 py-5 sm:p-6">
        <header class="mb-6 md:mb-8">
          <h1 class="text-2xl md:text-3xl font-semibold text-pf-dark mb-2">Color Palette</h1>
          <p class="text-pf-secondary">PhotoFrame theme colors</p>
        </header>

        <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
          <div 
            v-for="color in colors" 
            :key="color.name"
            class="group relative overflow-hidden rounded-lg border border-gray-200 hover:shadow-md transition-shadow cursor-pointer"
            @click="copyToClipboard(color.hex)"
          >
            <div 
              class="h-32 w-full"
              :style="{ backgroundColor: color.hex }"
            ></div>
            <div class="p-4 bg-white">
              <div class="flex justify-between items-start mb-1">
                <h3 class="font-semibold text-pf-dark">{{ color.name }}</h3>
                <button 
                  class="opacity-0 group-hover:opacity-100 text-xs text-pf-secondary hover:text-pf-primary transition-opacity"
                  @click.stop="copyToClipboard(color.hex)"
                >
                  Copy Hex
                </button>
              </div>
              <p class="text-sm font-mono text-pf-secondary mb-1">{{ color.hex }}</p>
              <p class="text-xs font-mono text-gray-500 mb-1">{{ color.tailwindClass }}</p>
              <p class="text-xs text-pf-secondary">{{ color.description }}</p>
            </div>
          </div>
        </div>

        <div class="mt-8 border-t border-gray-200 pt-6">
          <h2 class="text-xl font-semibold text-pf-dark mb-4">Color Usage Examples</h2>
          
          <div class="space-y-6">
            <!-- Buttons -->
            <div>
              <h3 class="text-sm font-medium text-pf-secondary mb-3">Buttons</h3>
              <div class="flex flex-wrap gap-2">
                <button class="px-4 py-2 bg-pf-primary text-white rounded-md hover:bg-pf-primary-hover transition-colors">Primary</button>
                <button class="px-4 py-2 bg-pf-secondary text-white rounded-md hover:bg-pf-secondary-hover transition-colors">Secondary</button>
                <button class="px-4 py-2 bg-pf-success text-white rounded-md hover:bg-pf-success-hover transition-colors">Success</button>
                <button class="px-4 py-2 bg-pf-warning text-pf-dark rounded-md hover:bg-pf-warning-hover transition-colors">Warning</button>
                <button class="px-4 py-2 bg-pf-danger text-white rounded-md hover:bg-pf-danger-hover transition-colors">Danger</button>
                <button class="px-4 py-2 bg-pf-info text-white rounded-md hover:bg-pf-info-hover transition-colors">Info</button>
              </div>
            </div>

            <!-- Alerts -->
            <div>
              <h3 class="text-sm font-medium text-pf-secondary mb-3">Alerts</h3>
              <div class="space-y-2">
                <div class="p-4 bg-green-50 border-l-4 border-pf-success rounded-r">
                  <p class="text-sm text-pf-dark"><strong>Success:</strong> Operation completed successfully</p>
                </div>
                <div class="p-4 bg-yellow-50 border-l-4 border-pf-warning rounded-r">
                  <p class="text-sm text-pf-dark"><strong>Warning:</strong> Please review this action</p>
                </div>
                <div class="p-4 bg-red-50 border-l-4 border-pf-danger rounded-r">
                  <p class="text-sm text-pf-dark"><strong>Error:</strong> Something went wrong</p>
                </div>
                <div class="p-4 bg-pink-50 border-l-4 border-pf-info rounded-r">
                  <p class="text-sm text-pf-dark"><strong>Info:</strong> Here's some information</p>
                </div>
              </div>
            </div>

            <!-- Badges -->
            <div>
              <h3 class="text-sm font-medium text-pf-secondary mb-3">Badges</h3>
              <div class="flex flex-wrap gap-2">
                <span class="px-3 py-1 bg-pf-primary text-white text-xs font-medium rounded-full">Primary</span>
                <span class="px-3 py-1 bg-pf-secondary text-white text-xs font-medium rounded-full">Secondary</span>
                <span class="px-3 py-1 bg-pf-success text-white text-xs font-medium rounded-full">Success</span>
                <span class="px-3 py-1 bg-pf-warning text-pf-dark text-xs font-medium rounded-full">Warning</span>
                <span class="px-3 py-1 bg-pf-danger text-white text-xs font-medium rounded-full">Danger</span>
                <span class="px-3 py-1 bg-pf-info text-white text-xs font-medium rounded-full">Info</span>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>
