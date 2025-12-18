<script setup lang="ts">
import { RouterView, RouterLink, useRoute } from 'vue-router'
import { computed } from 'vue'

const route = useRoute()
const isGallery = computed(() => route.name === 'gallery')
const isSettings = computed(() => route.name === 'settings')
</script>

<template>
  <div class="app">
    <nav class="nav">
      <RouterLink to="/" class="nav-link" :class="{ active: isGallery }">Gallery</RouterLink>
      <RouterLink to="/settings" class="nav-link" :class="{ active: isSettings }">Settings</RouterLink>
    </nav>
    <main class="main">
      <RouterView />
    </main>
  </div>
</template>

<style>
* {
  box-sizing: border-box;
}

body {
  margin: 0;
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
  background: #f9fafb;
  color: #1f2937;
}

#app {
  min-height: 100vh;
}
</style>

<style scoped>
.app {
  min-height: 100vh;
  display: flex;
  flex-direction: column;
}

.nav {
  background: white;
  border-bottom: 1px solid #e5e7eb;
  padding: 0 2rem;
  display: flex;
  gap: 2rem;
}

.nav-link {
  padding: 1rem 0;
  text-decoration: none;
  color: #6b7280;
  font-weight: 500;
  border-bottom: 2px solid transparent;
  transition: all 0.2s;
}

.nav-link:hover {
  color: #3b82f6;
}

.nav-link.active {
  color: #3b82f6;
  border-bottom-color: #3b82f6;
}

.main {
  flex: 1;
}
</style>
