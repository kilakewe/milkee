<script setup lang="ts">
import { ToastProvider, ToastRoot, ToastTitle, ToastDescription, ToastClose, ToastViewport } from 'radix-vue'
import { useToast } from '@/composables/useToast'
import { computed } from 'vue'

const { toasts, dismiss } = useToast()

const getToastStyles = (type: string) => {
  const baseClasses = 'flex items-start gap-3 p-4 rounded-lg shadow-lg border-l-4 bg-white'
  
  switch (type) {
    case 'success':
      return `${baseClasses} border-pf-success`
    case 'error':
      return `${baseClasses} border-pf-danger`
    case 'warning':
      return `${baseClasses} border-pf-warning`
    case 'info':
      return `${baseClasses} border-pf-info`
    default:
      return `${baseClasses} border-pf-primary`
  }
}

const getIconColor = (type: string) => {
  switch (type) {
    case 'success':
      return 'text-pf-success'
    case 'error':
      return 'text-pf-danger'
    case 'warning':
      return 'text-pf-warning'
    case 'info':
      return 'text-pf-info'
    default:
      return 'text-pf-primary'
  }
}

const getIcon = (type: string) => {
  switch (type) {
    case 'success':
      return '✓'
    case 'error':
      return '✕'
    case 'warning':
      return '⚠'
    case 'info':
      return 'ℹ'
    default:
      return '○'
  }
}
</script>

<template>
  <ToastProvider>
    <ToastRoot
      v-for="toast in toasts"
      :key="toast.id"
      :class="getToastStyles(toast.type)"
      @update:open="(open) => !open && dismiss(toast.id)"
    >
      <div :class="['text-2xl font-bold flex-shrink-0', getIconColor(toast.type)]">
        {{ getIcon(toast.type) }}
      </div>
      <div class="flex-1 min-w-0">
        <ToastTitle class="text-pf-dark font-semibold text-sm mb-1">
          {{ toast.title }}
        </ToastTitle>
        <ToastDescription v-if="toast.description" class="text-pf-secondary text-xs">
          {{ toast.description }}
        </ToastDescription>
      </div>
      <ToastClose
        class="text-pf-secondary hover:text-pf-dark text-xl leading-none flex-shrink-0 w-6 h-6 flex items-center justify-center rounded hover:bg-gray-100 transition-colors touch-manipulation"
        aria-label="Close"
      >
        ×
      </ToastClose>
    </ToastRoot>

    <ToastViewport class="fixed top-0 right-0 p-4 flex flex-col gap-2 w-full max-w-md z-50 pointer-events-none [&>*]:pointer-events-auto" />
  </ToastProvider>
</template>
