import { createRouter, createWebHashHistory } from 'vue-router'
import GalleryView from '@/views/GalleryView.vue'
import SettingsView from '@/views/SettingsView.vue'

const router = createRouter({
  // Hash routing works with the ESP32 static file server (no server-side history fallback needed).
  history: createWebHashHistory(import.meta.env.BASE_URL),
  routes: [
    {
      path: '/',
      name: 'gallery',
      component: GalleryView,
    },
    {
      path: '/settings',
      name: 'settings',
      component: SettingsView,
    },
  ],
})

export default router
