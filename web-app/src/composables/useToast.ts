import { ref } from 'vue'

export type ToastType = 'success' | 'error' | 'warning' | 'info'

export interface Toast {
  id: string
  title: string
  description?: string
  type: ToastType
  duration?: number
}

const toasts = ref<Toast[]>([])
let idCounter = 0

export function useToast() {
  const show = (toast: Omit<Toast, 'id'>) => {
    const id = `toast-${++idCounter}`
    const newToast: Toast = {
      id,
      ...toast,
      duration: toast.duration ?? 5000,
    }
    
    toasts.value.push(newToast)

    if (newToast.duration && newToast.duration > 0) {
      setTimeout(() => {
        dismiss(id)
      }, newToast.duration)
    }

    return id
  }

  const dismiss = (id: string) => {
    const index = toasts.value.findIndex((t) => t.id === id)
    if (index > -1) {
      toasts.value.splice(index, 1)
    }
  }

  const success = (title: string, description?: string, duration?: number) => {
    return show({ title, description, type: 'success', duration })
  }

  const error = (title: string, description?: string, duration?: number) => {
    return show({ title, description, type: 'error', duration })
  }

  const warning = (title: string, description?: string, duration?: number) => {
    return show({ title, description, type: 'warning', duration })
  }

  const info = (title: string, description?: string, duration?: number) => {
    return show({ title, description, type: 'info', duration })
  }

  return {
    toasts,
    show,
    dismiss,
    success,
    error,
    warning,
    info,
  }
}
