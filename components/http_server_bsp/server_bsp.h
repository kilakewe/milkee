#ifndef SERVER_BSP_H
#define SERVER_BSP_H

#include "freertos/FreeRTOS.h"
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

extern EventGroupHandle_t server_groups;

#ifdef __cplusplus
extern "C" {
#endif

void http_server_init(void);

// Network initialization for the PhotoPainter browser upload app.
// Behavior:
// - If Wi-Fi credentials are configured and STA connects, run in STA mode.
// - Otherwise, start a SoftAP so the web app is still reachable.
void Network_wifi_init(void);

// Backward-compatible name (legacy callers).
void Network_wifi_ap_init(void);

void set_espWifi_sleep(void);

// Rotation setting for the browser upload app.
// Value is in degrees. Supported: 0, 90, 180, 270.
uint16_t server_bsp_get_rotation(void);
// Returns ESP_OK on success.
esp_err_t server_bsp_set_rotation(uint16_t rotation_deg);

// Rotation that was active when the last image was uploaded.
// Used to rotate the *current* picture when the rotation setting changes.
uint16_t server_bsp_get_image_rotation(void);

// Full path to the current image on the SD card.
const char *server_bsp_get_current_image_path(void);

// Select the next stored photo (lexicographic order) as the current image.
// Returns ESP_OK if a photo was selected, otherwise an error.
esp_err_t server_bsp_select_next_photo(void);

// Last HTTP activity timestamp (microseconds from esp_timer_get_time).
uint64_t server_bsp_get_last_activity_us(void);

// Manual activity marker (e.g. physical button press)
void server_bsp_mark_activity(void);

// Slideshow settings (NVS-backed)
bool server_bsp_get_slideshow_enabled(void);
uint32_t server_bsp_get_slideshow_interval_s(void);
// Returns ESP_OK on success.
esp_err_t server_bsp_set_slideshow(bool enabled, uint32_t interval_s);

// Initialize SD/NVS-backed state without starting an HTTP server.
// Safe to call multiple times.
void server_bsp_init_state(void);

// Network snapshot (for displaying connection info on the e-paper).
// NOTE: Strings are null-terminated.
typedef enum
{
    SERVER_BSP_WIFI_MODE_NONE = 0,
    SERVER_BSP_WIFI_MODE_STA = 1,
    SERVER_BSP_WIFI_MODE_AP = 2,
} server_bsp_wifi_mode_t;

typedef struct
{
    server_bsp_wifi_mode_t mode;
    bool sta_connected;
    char sta_ssid[33];
    char sta_ip[16];
    char ap_ssid[33];
    char ap_password[65];
    char hostname[32]; // e.g. frame-1a2b3c
} server_bsp_network_info_t;

// Fills a snapshot of the current Wi-Fi state.
void server_bsp_get_network_info(server_bsp_network_info_t *out);

// SoftAP default gateway IP.
const char *server_bsp_get_ap_ip(void);

// Status icon overlay (NVS-backed UI setting).
// When enabled, the firmware can draw battery/Wi-Fi icons on top of the rendered image.
bool server_bsp_get_status_icons_enabled(void);
esp_err_t server_bsp_set_status_icons_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif // !SERVER_BSP_H
