<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { api, type WifiStatusResponse } from '@/services/api'
import { useToast } from '@/composables/useToast'
import LoadingOverlay from '@/components/LoadingOverlay.vue'

const toast = useToast()

const loading = ref(false)
const saving = ref(false)
const clearing = ref(false)

const status = ref<WifiStatusResponse | null>(null)

const ssid = ref('')
const password = ref('')

const isConfigured = computed(() => status.value?.configured ?? false)
const mode = computed(() => status.value?.mode ?? 'none')
const connected = computed(() => status.value?.connected ?? false)

async function refresh() {
  try {
    loading.value = true
    const s = await api.getWifiStatus()
    status.value = s

    // Convenience: if we're already connected, prefill the SSID.
    if (s.ssid && !ssid.value) {
      ssid.value = s.ssid
    }
  } catch (err) {
    toast.error('Failed to load Wi‑Fi status', err instanceof Error ? err.message : undefined)
  } finally {
    loading.value = false
  }
}

async function save() {
  if (!ssid.value.trim()) {
    toast.warning('SSID is required')
    return
  }

  try {
    saving.value = true
    await api.setWifiConfig(ssid.value.trim(), password.value)
    toast.success('Wi‑Fi saved', 'Rebooting device to apply changes…')

    // The device will reboot, so this page will likely disconnect.
  } catch (err) {
    toast.error('Failed to save Wi‑Fi', err instanceof Error ? err.message : undefined)
  } finally {
    saving.value = false
  }
}

async function clear() {
  if (!confirm('Clear saved Wi‑Fi credentials and reboot?')) return

  try {
    clearing.value = true
    await api.clearWifiConfig()
    toast.success('Wi‑Fi cleared', 'Rebooting device…')
  } catch (err) {
    toast.error('Failed to clear Wi‑Fi', err instanceof Error ? err.message : undefined)
  } finally {
    clearing.value = false
  }
}

onMounted(() => {
  refresh()
})
</script>

<template>
  <div class="max-w-4xl mx-auto p-4 md:p-8">
    <div class="overflow-hidden rounded-lg bg-white shadow-sm relative">
      <LoadingOverlay :loading="loading" />

      <div class="px-4 py-5 sm:p-6">
        <header class="mb-6 md:mb-8">
          <h1 class="text-2xl md:text-3xl font-semibold text-pf-dark">Wi‑Fi Setup</h1>
          <p class="text-sm text-pf-secondary mt-2">
            Configure the device Wi‑Fi connection. Saving will reboot the device.
          </p>
        </header>

        <section class="bg-gray-50 border border-gray-200 rounded-lg p-4 md:p-6 mb-6">
          <h2 class="text-xl font-semibold text-pf-dark mb-2">Status</h2>

          <div v-if="status" class="text-sm text-pf-secondary space-y-1">
            <div>
              <strong class="text-pf-dark">Configured:</strong>
              {{ isConfigured ? 'Yes' : 'No' }}
            </div>
            <div>
              <strong class="text-pf-dark">Mode:</strong>
              {{ mode.toUpperCase() }}
            </div>
            <div v-if="mode === 'sta'">
              <strong class="text-pf-dark">Connected:</strong>
              {{ connected ? 'Yes' : 'No' }}
            </div>
            <div v-if="mode === 'sta' && connected">
              <strong class="text-pf-dark">SSID:</strong>
              {{ status.ssid }}
            </div>
            <div v-if="mode === 'sta' && connected">
              <strong class="text-pf-dark">IP:</strong>
              {{ status.ip }}
            </div>

            <div v-if="mode === 'ap'" class="pt-2">
              <div class="bg-blue-50 border border-blue-200 rounded-md p-3">
                <div class="font-medium text-pf-dark mb-1">Access Point Mode</div>
                <div>SSID: <strong class="text-pf-dark">{{ status.ap_ssid }}</strong></div>
                <div>Open: <strong class="text-pf-dark">http://{{ status.ap_ip }}/</strong></div>
              </div>
            </div>
          </div>

          <div v-else class="text-sm text-pf-secondary">Loading…</div>
        </section>

        <section class="bg-gray-50 border border-gray-200 rounded-lg p-4 md:p-6">
          <h2 class="text-xl font-semibold text-pf-dark mb-2">Configure</h2>
          <p class="text-sm text-pf-secondary mb-4 leading-relaxed">
            Enter your network details. Password can be empty for open networks.
          </p>

          <div class="flex flex-col gap-3">
            <div class="flex flex-col gap-1">
              <label for="wifi-ssid" class="font-medium text-pf-secondary text-sm">SSID</label>
              <input
                id="wifi-ssid"
                v-model="ssid"
                type="text"
                autocomplete="off"
                class="px-3 py-2 border border-gray-300 rounded-md bg-white text-pf-dark"
                placeholder="Your Wi‑Fi name"
                :disabled="saving || clearing"
              />
            </div>

            <div class="flex flex-col gap-1">
              <label for="wifi-password" class="font-medium text-pf-secondary text-sm">Password</label>
              <input
                id="wifi-password"
                v-model="password"
                type="password"
                autocomplete="off"
                class="px-3 py-2 border border-gray-300 rounded-md bg-white text-pf-dark"
                placeholder="(optional)"
                :disabled="saving || clearing"
              />
            </div>

            <div class="flex flex-col sm:flex-row gap-2 pt-2">
              <button
                @click="save"
                :disabled="saving || clearing"
                class="flex-1 px-4 py-2 bg-pf-primary text-white rounded-md font-medium hover:bg-pf-primary-hover disabled:opacity-50 disabled:cursor-not-allowed transition-colors touch-manipulation"
              >
                {{ saving ? 'Saving…' : 'Save & Reboot' }}
              </button>

              <button
                @click="refresh"
                :disabled="loading || saving || clearing"
                class="flex-1 px-4 py-2 bg-pf-secondary text-white rounded-md font-medium hover:bg-pf-secondary-hover disabled:opacity-50 disabled:cursor-not-allowed transition-colors touch-manipulation"
              >
                Refresh
              </button>

              <button
                @click="clear"
                :disabled="saving || clearing"
                class="flex-1 px-4 py-2 bg-pf-danger text-white rounded-md font-medium hover:bg-red-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors touch-manipulation"
              >
                {{ clearing ? 'Clearing…' : 'Clear Wi‑Fi' }}
              </button>
            </div>
          </div>
        </section>
      </div>
    </div>
  </div>
</template>
