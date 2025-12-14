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

// Last HTTP activity timestamp (microseconds from esp_timer_get_time).
uint64_t server_bsp_get_last_activity_us(void);


#ifdef __cplusplus
}
#endif

#endif // !CLIENT_BSP_H
