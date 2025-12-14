#ifndef SERVER_BSP_H
#define SERVER_BSP_H

#include "freertos/FreeRTOS.h"
#include <stdint.h>
#include "esp_err.h"

extern EventGroupHandle_t server_groups;

#ifdef __cplusplus
extern "C" {
#endif

void http_server_init(void);
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


#ifdef __cplusplus
}
#endif

#endif // !CLIENT_BSP_H
