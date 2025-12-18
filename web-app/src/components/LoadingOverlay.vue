<script setup lang="ts">
defineProps<{
  loading: boolean
}>()
</script>

<template>
  <Transition
    enter-active-class="transition-opacity duration-200"
    leave-active-class="transition-opacity duration-200"
    enter-from-class="opacity-0"
    leave-to-class="opacity-0"
  >
    <div 
      v-if="loading"
      class="absolute inset-0 bg-white/80 backdrop-blur-sm flex items-center justify-center z-10 rounded-lg"
    >
      <div class="flex flex-col items-center gap-3">
        <!-- Spinner -->
        <div class="relative w-12 h-12">
          <div class="absolute inset-0 border-4 border-gray-200 rounded-full"></div>
          <div class="absolute inset-0 border-4 border-pf-primary border-t-transparent rounded-full animate-spin"></div>
        </div>
        <p class="text-sm font-medium text-pf-secondary">Loading...</p>
      </div>
    </div>
  </Transition>
</template>
