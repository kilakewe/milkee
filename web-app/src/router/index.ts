import { createRouter, createWebHashHistory } from 'vue-router'
import GalleryView from '@/views/GalleryView.vue'
import SettingsView from '@/views/SettingsView.vue'
import WifiView from '@/views/WifiView.vue'
import ColorsDebugView from '@/views/ColorsDebugView.vue'
import { api } from '@/services/api'

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
    {
      path: '/wifi',
      name: 'wifi',
      component: WifiView,
    },
    {
      path: '/_debug/colours',
      name: 'colors-debug',
      component: ColorsDebugView,
    },
  ],
})

// If the device has no Wi-Fi credentials configured yet, force the user into
// the Wi-Fi setup page first.
let wifiChecked = false
let wifiConfigured = true

router.beforeEach(async (to) => {
  // Always allow the Wi-Fi setup page.
  if (to.name === 'wifi') return true
  // Allow debug routes without gating.
  if (to.path.startsWith('/_debug')) return true

  if (!wifiChecked) {
    try {
      const status = await api.getWifiStatus()
      wifiConfigured = status.configured
    } catch {
      // If the endpoint isn't available (e.g. running the app without firmware),
      // don't block navigation.
      wifiConfigured = true
    } finally {
      wifiChecked = true
    }
  }

  // Only force the default landing page (Gallery) to Wi‑Fi setup.
  if (!wifiConfigured && to.name === 'gallery') {
    return { name: 'wifi', query: { redirect: to.fullPath } }
  }

  return true
})

export default router
