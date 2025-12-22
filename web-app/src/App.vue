<script setup lang="ts">
import { RouterView, RouterLink, useRoute } from 'vue-router'
import { computed } from 'vue'
import { useToast } from '@/composables/useToast'
import { api } from '@/services/api'
import Toast from '@/components/Toast.vue'

const route = useRoute()
const toast = useToast()
const isGallery = computed(() => route.name === 'gallery')
const isSettings = computed(() => route.name === 'settings')
const isWifi = computed(() => route.name === 'wifi')

async function skipPhoto() {
  try {
    await api.nextPhoto()
    toast.success('Next photo', 'Skipped to next photo in rotation')
  } catch (err) {
    toast.error('Failed to skip photo', err instanceof Error ? err.message : undefined)
  }
}
</script>

<template>
  <div class="min-h-screen flex flex-col bg-pf-dark">
    <nav class="relative bg-pf-dark shadow-sm after:pointer-events-none after:absolute after:inset-x-0 after:bottom-0 after:h-px after:bg-pf-primary">
      <div class="mx-auto max-w-7xl px-4 sm:px-6 lg:px-8">
        <div class="flex h-16 justify-between">
          <div class="flex">
            <div class="flex shrink-0 items-center">
              <span class="text-lg font-semibold text-pf-light">PhotoFrame</span>
            </div>
            <div class="hidden md:ml-6 md:flex md:space-x-8">
              <RouterLink 
                to="/" 
                class="inline-flex items-center border-b-2 px-1 pt-1 text-sm font-medium transition-colors"
                :class="isGallery ? 'border-pf-primary text-pf-light' : 'border-transparent text-gray-400 hover:border-pf-primary/20 hover:text-pf-light'"
              >
                Gallery
              </RouterLink>
              <RouterLink 
                to="/settings" 
                class="inline-flex items-center border-b-2 px-1 pt-1 text-sm font-medium transition-colors"
                :class="isSettings ? 'border-pf-primary text-pf-light' : 'border-transparent text-gray-400 hover:border-pf-primary/20 hover:text-pf-light'"
              >
                Settings
              </RouterLink>
              <RouterLink 
                to="/wifi" 
                class="inline-flex items-center border-b-2 px-1 pt-1 text-sm font-medium transition-colors"
                :class="isWifi ? 'border-pf-primary text-pf-light' : 'border-transparent text-gray-400 hover:border-pf-primary/20 hover:text-pf-light'"
              >
                Wi‑Fi
              </RouterLink>
            </div>
          </div>
          <div class="flex items-center">
            <div class="shrink-0">
              <button 
                @click="skipPhoto" 
                type="button" 
                class="relative inline-flex items-center gap-x-1.5 rounded-md bg-pf-primary px-3 py-2 text-sm font-semibold text-white shadow-sm hover:bg-pf-primary-hover focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-pf-primary transition-colors touch-manipulation"
              >
                <svg class="-ml-0.5 size-5" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M3 8.689c0-.864.933-1.406 1.683-.977l7.108 4.061a1.125 1.125 0 0 1 0 1.954l-7.108 4.061A1.125 1.125 0 0 1 3 16.811V8.69ZM12.75 8.689c0-.864.933-1.406 1.683-.977l7.108 4.061a1.125 1.125 0 0 1 0 1.954l-7.108 4.061a1.125 1.125 0 0 1-1.683-.977V8.69Z" />
                </svg>
                Skip Photo
              </button>
            </div>
          </div>
        </div>
      </div>
      
      <!-- Mobile menu -->
      <div class="md:hidden border-t border-pf-primary">
        <div class="space-y-1 pt-2 pb-3">
          <RouterLink 
            to="/" 
            class="block border-l-4 py-2 pr-4 pl-3 text-base font-medium sm:pr-6 sm:pl-5 transition-colors"
            :class="isGallery ? 'border-pf-primary bg-pf-primary/10 text-pf-light' : 'border-transparent text-gray-400 hover:border-pf-primary/20 hover:bg-pf-primary/5 hover:text-pf-light'"
          >
            Gallery
          </RouterLink>
          <RouterLink 
            to="/settings" 
            class="block border-l-4 py-2 pr-4 pl-3 text-base font-medium sm:pr-6 sm:pl-5 transition-colors"
            :class="isSettings ? 'border-pf-primary bg-pf-primary/10 text-pf-light' : 'border-transparent text-gray-400 hover:border-pf-primary/20 hover:bg-pf-primary/5 hover:text-pf-light'"
          >
            Settings
          </RouterLink>
          <RouterLink 
            to="/wifi" 
            class="block border-l-4 py-2 pr-4 pl-3 text-base font-medium sm:pr-6 sm:pl-5 transition-colors"
            :class="isWifi ? 'border-pf-primary bg-pf-primary/10 text-pf-light' : 'border-transparent text-gray-400 hover:border-pf-primary/20 hover:bg-pf-primary/5 hover:text-pf-light'"
          >
            Wi‑Fi
          </RouterLink>
        </div>
      </div>
    </nav>
    <main class="flex-1">
      <RouterView />
    </main>
    <Toast />
  </div>
</template>
