#include "user_app.h"
#include "axp_prot.h"
#include "button_bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/semphr.h"
#include "driver/rtc_io.h"
#include "i2c_bsp.h"
#include "led_bsp.h"
#include "sdcard_bsp.h"
#include "server_bsp.h"
#include <stdio.h>
#include <string.h>

#include "GUI_BMPfile.h"
#include "GUI_Paint.h"
#include "epaper_port.h"

#include "client_bsp.h"
#include "json_data.h"

#include "i2c_equipment.h"

#define AXP2101_iqr_PIN GPIO_NUM_21

// #if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
/**
 * @brief Renders a 4x4 color-index test pattern on e-paper display during boot.
 *
 * This function generates and displays a simple test pattern on the e-paper display
 * to verify display functionality without requiring SD card access. The pattern
 * consists of a 4x4 grid of cells, each filled with a unique 4-bit color index
 * (0x0 through 0xF), allowing visual verification of how the e-paper controller
 * maps color indices to actual display output.
 *
 * @details
 * - Allocates a temporary frame buffer from SPIRAM
 * - Creates a 4x4 grid with 6-pixel gaps between cells
 * - Each cell displays a unique color index (0-15)
 * - Uses 180° rotation for proper display orientation
 * - Automatically cleans up allocated memory after display
 *
 * @note
 * - This test pattern is displayed early in boot sequence, before SD card
 *   initialization, to verify display hardware is functional
 * - If SPIRAM allocation fails, logs error and returns without rendering
 * - Uses the Paint GUI library for rendering and epaper_port for display output
 *
 * @see Paint_NewImage(), Paint_ClearWindows(), epaper_port_display()
 */
static void RenderBootTestPattern()
{
    // Render a simple color-index test pattern on boot (no SD file dependency).
    // The e-paper controller accepts 4-bit pixel values; this draws a 4x4 grid
    // using indices 0x0..0xF so you can see how the panel maps them.

    // Step 1: Calculate frame buffer size for 4-bit grayscale image
    // Since each pixel uses 4 bits, we need half a byte per pixel
    uint32_t imagesize = ((EXAMPLE_LCD_WIDTH % 2 == 0) ? (EXAMPLE_LCD_WIDTH / 2)
                                                       : (EXAMPLE_LCD_WIDTH / 2 + 1)) *
                         EXAMPLE_LCD_HEIGHT;

    // Step 2: Allocate frame buffer from SPIRAM (external RAM)
    uint8_t *epd_blackImage = (uint8_t *)heap_caps_malloc(imagesize * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
    if (!epd_blackImage)
    {
        ESP_LOGE("boot_test", "Failed to allocate e-paper buffer (%lu bytes)", (unsigned long)imagesize);
        return;
    }

    // Step 3: Initialize the Paint context with display dimensions and white background
    Paint_NewImage(epd_blackImage, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 0, EPD_7IN3E_WHITE);
    // Step 4: Configure rendering scale and rotation (180° for correct orientation)
    Paint_SetScale(16);
    Paint_SetRotate(ROTATE_180);
    Paint_SelectImage(epd_blackImage);

    // Step 5: Fill background with white color
    Paint_Clear(EPD_7IN3E_WHITE);

    // Step 6: Define grid layout for color test pattern
    // Array of available colors defined in epaper_port.h
    const uint8_t colors[] = {
        EPD_7IN3E_BLACK,  // 0x0 - Black
        EPD_7IN3E_WHITE,  // 0x1 - White
        EPD_7IN3E_YELLOW, // 0x2 - Yellow
        EPD_7IN3E_RED,    // 0x3 - Red
        EPD_7IN3E_BLUE,   // 0x5 - Blue
        EPD_7IN3E_GREEN   // 0x6 - Green
    };
    const int num_colors = sizeof(colors) / sizeof(colors[0]);

    constexpr int kCols = 4;
    constexpr int kRows = 2;  // 2 rows to accommodate 7 colors in 4x2 grid
    constexpr int kGapPx = 6; // 6-pixel gap between cells for visual separation

    const int cell_w = EXAMPLE_LCD_WIDTH / kCols;
    const int cell_h = EXAMPLE_LCD_HEIGHT / kRows;

    // Step 7: Iterate through grid and draw each cell with defined color constants
    int color_idx = 0;
    for (int row = 0; row < kRows; row++)
    {
        for (int col = 0; col < kCols; col++)
        {
            // Skip cells beyond available colors
            if (color_idx >= num_colors)
                break;

            // Use defined color constants from epaper_port.h
            const uint8_t color = colors[color_idx++];

            // Calculate cell boundaries with gaps for visual separation
            const UWORD x0 = (UWORD)(col * cell_w + kGapPx);
            const UWORD y0 = (UWORD)(row * cell_h + kGapPx);
            const UWORD x1 = (UWORD)((col + 1) * cell_w - kGapPx);
            const UWORD y1 = (UWORD)((row + 1) * cell_h - kGapPx);

            // Only render if cell has valid dimensions
            if (x1 > x0 && y1 > y0)
            {
                // Paint_ClearWindows uses [start, end) semantics and fills region with color
                Paint_ClearWindows(x0, y0, x1, y1, color);
            }
        }
    }

    // Step 8: Send rendered frame buffer to e-paper display
    epaper_port_display(epd_blackImage);

    // Step 9: Free allocated SPIRAM memory
    heap_caps_free(epd_blackImage);
}
// #endif

// i2c_equipment_shtc3 *dev_shtc3 = NULL;

SemaphoreHandle_t epaper_gui_semapHandle = NULL; // Mutual exclusion lock to prevent repeated refreshing
EventGroupHandle_t epaper_groups;                // Event group for map refreshing
EventGroupHandle_t Green_led_Mode_queue = 0;     // Queue for LED blinking, mainly for storing mode parameters
EventGroupHandle_t Red_led_Mode_queue = 0;       // Queue for LED blinking, mainly for storing mode parameters
uint8_t Green_led_arg = 0;                       // Parameters for LED task
uint8_t Red_led_arg = 0;                         // Parameters for LED task

static void Green_led_user_Task(void *arg)
{
    uint8_t *led_arg = (uint8_t *)arg;
    for (;;)
    {
        EventBits_t even =
            xEventGroupWaitBits(Green_led_Mode_queue, set_bit_all, pdFALSE, pdFALSE, portMAX_DELAY);
        if (get_bit_data(even, 1))
        {
            led_set(LED_PIN_Green, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_OFF);
            xEventGroupClearBits(Green_led_Mode_queue, rset_bit_data(1));
        }
        if (get_bit_data(even, 2))
        {
            led_set(LED_PIN_Green, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_OFF);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_OFF);
            xEventGroupClearBits(Green_led_Mode_queue, rset_bit_data(2));
        }
        if (get_bit_data(even, 3))
        {
            led_set(LED_PIN_Green, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_OFF);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_OFF);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_OFF);
            xEventGroupClearBits(Green_led_Mode_queue, rset_bit_data(3));
        }
        if (get_bit_data(even, 4))
        {
            led_set(LED_PIN_Green, LED_ON);
            xEventGroupClearBits(Green_led_Mode_queue, rset_bit_data(4));
        }
        if (get_bit_data(even, 5))
        {
            led_set(LED_PIN_Green, LED_OFF);
            xEventGroupClearBits(Green_led_Mode_queue, rset_bit_data(5));
        }
        if (get_bit_data(even, 6))
        {
            while (*led_arg)
            {
                led_set(LED_PIN_Green, LED_ON);
                vTaskDelay(pdMS_TO_TICKS(100));
                led_set(LED_PIN_Green, LED_OFF);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            xEventGroupClearBits(Green_led_Mode_queue, rset_bit_data(6));
        }
        if (get_bit_data(even, 7))
        {
            led_set(LED_PIN_Green, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_OFF);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_OFF);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_OFF);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(LED_PIN_Green, LED_OFF);
            xEventGroupClearBits(Green_led_Mode_queue, rset_bit_data(7));
        }
    }
}

// Red LED blink half-period (ms). While charging we slow this down.
static volatile uint32_t s_red_led_blink_ms = 100;

// Cached charging state used for power management decisions.
// Rule: never enter deep sleep while charging.
static volatile bool s_is_charging = false;

// Serialize PMU reads across tasks to avoid I2C contention.
static SemaphoreHandle_t s_pmu_mutex = NULL;

static inline bool IsChargingCached()
{
    return s_is_charging;
}

static bool PmuIsCharging()
{
    // If we can't safely read the PMU, be conservative and assume charging.
    if (s_pmu_mutex)
    {
        if (xSemaphoreTake(s_pmu_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
        {
            return true;
        }
    }

    const bool charging = axp2101_is_charging();

    if (s_pmu_mutex)
    {
        xSemaphoreGive(s_pmu_mutex);
    }

    return charging;
}

static void Red_led_user_Task(void *arg)
{
    uint8_t *led_arg = (uint8_t *)arg;
    for (;;)
    {
        EventBits_t even =
            xEventGroupWaitBits(Red_led_Mode_queue, set_bit_all, pdFALSE, pdFALSE, portMAX_DELAY);
        if (get_bit_data(even, 0))
        {
            led_set(LED_PIN_Red, LED_ON);
            xEventGroupClearBits(Red_led_Mode_queue, rset_bit_data(0));
        }
        if (get_bit_data(even, 6))
        {
            while (*led_arg)
            {
                const uint32_t ms = (s_red_led_blink_ms < 50) ? 50 : s_red_led_blink_ms;
                led_set(LED_PIN_Red, LED_ON);
                vTaskDelay(pdMS_TO_TICKS(ms));
                led_set(LED_PIN_Red, LED_OFF);
                vTaskDelay(pdMS_TO_TICKS(ms));
            }
            led_set(LED_PIN_Red, LED_OFF);
            xEventGroupClearBits(Red_led_Mode_queue, rset_bit_data(6));
        }
    }
}

static void ChargingStatusLedTask(void *arg)
{
    (void)arg;

    // Default: fast blink when awake.
    constexpr uint32_t kBlinkFastMs = 100;
    constexpr uint32_t kBlinkSlowMs = 1000;

    // Hysteresis: enter charging state quickly, exit slowly.
    bool cached = IsChargingCached();
    int not_charging_streak = 0;

    for (;;)
    {
        const bool charging_raw = PmuIsCharging();

        // LED blink rate follows the raw reading.
        s_red_led_blink_ms = charging_raw ? kBlinkSlowMs : kBlinkFastMs;

        bool new_cached = cached;
        if (charging_raw)
        {
            new_cached = true;
            not_charging_streak = 0;
        }
        else
        {
            not_charging_streak++;
            if (not_charging_streak >= 3)
            {
                new_cached = false;
            }
        }

        if (new_cached != cached)
        {
            cached = new_cached;
            s_is_charging = cached;
            ESP_LOGI("charge", "charging=%d", (int)cached);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Minimal "app" that:
// - runs a Wi-Fi AP + HTTP server (see components/http_server_bsp)
// - accepts a raw 24-bit BMP via POST /dataUP
//   (800x480 for 0/180; 480x800 for 90/270)
// - displays it on the e-paper panel when upload completes
static void BrowserImageUploadDisplayTask(void *arg)
{
    (void)arg;

    const uint32_t imagesize = ((EXAMPLE_LCD_WIDTH % 2 == 0) ? (EXAMPLE_LCD_WIDTH / 2)
                                                             : (EXAMPLE_LCD_WIDTH / 2 + 1)) *
                               EXAMPLE_LCD_HEIGHT;

    uint8_t *epd_blackImage = (uint8_t *)heap_caps_malloc(imagesize * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
    if (!epd_blackImage)
    {
        ESP_LOGE("browser_upload", "Failed to allocate e-paper buffer (%lu bytes)", (unsigned long)imagesize);
        vTaskDelete(NULL);
        return;
    }

    Paint_NewImage(epd_blackImage, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, server_bsp_get_rotation(), EPD_7IN3E_WHITE);
    Paint_SetScale(6);
    Paint_SelectImage(epd_blackImage);
    Paint_Clear(EPD_7IN3E_WHITE);

    for (;;)
    {
        // server_groups bits:
        // 0: upload started
        // 2: upload success (new image ready)
        // 3: upload failed
        const EventBits_t wait_mask = set_bit_button(0) | set_bit_button(2) | set_bit_button(3);
        EventBits_t bits = xEventGroupWaitBits(server_groups, wait_mask, pdTRUE, pdFALSE, portMAX_DELAY);

        if (get_bit_button(bits, 0))
        {
            // Start green blinking as soon as an image upload begins.
            if (!Green_led_arg)
            {
                Green_led_arg = 1;
                xEventGroupSetBits(Green_led_Mode_queue, set_bit_button(6));
            }
        }

        if (get_bit_button(bits, 3))
        {
            // Upload failed; stop green blinking.
            Green_led_arg = 0;
        }

        if (!get_bit_button(bits, 2))
        {
            continue;
        }

        if (pdTRUE == xSemaphoreTake(epaper_gui_semapHandle, pdMS_TO_TICKS(2000)))
        {
            // Keep green blinking while drawing.
            if (!Green_led_arg)
            {
                Green_led_arg = 1;
                xEventGroupSetBits(Green_led_Mode_queue, set_bit_button(6));
            }

            // Re-init the paint buffer for the current rotation.
            // IMPORTANT: Paint_SetRotate() does not update Paint.Width/Paint.Height, so for 90/270
            // we must call Paint_NewImage() to swap the logical dimensions safely.
            const uint16_t rotation = server_bsp_get_rotation();
            const uint16_t img_rotation = server_bsp_get_image_rotation();
            Paint_NewImage(epd_blackImage, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, rotation, EPD_7IN3E_WHITE);
            Paint_SetScale(6);
            Paint_SelectImage(epd_blackImage);

            // Render the current picture.
            // Note: Paint_NewImage(..., rotation, ...) controls how the framebuffer is rotated onto the panel.
            // To avoid "canceling out" the frame rotation (e.g. 90 -> 270), only use the rotate helper to
            // fix orientation *mismatches* (portrait vs landscape). Exact frame rotation differences are handled
            // by Paint.Rotate.
            const uint16_t dst_canvas = ((rotation == ROTATE_90) || (rotation == ROTATE_270)) ? ROTATE_90 : ROTATE_0;
            const uint16_t src_canvas = ((img_rotation == ROTATE_90) || (img_rotation == ROTATE_270)) ? ROTATE_90 : ROTATE_0;

            const char *img_path = server_bsp_get_current_image_path();
            if (img_path && img_path[0] != '\0')
            {
                GUI_ReadBmp_RGB_6Color_Rotate(img_path, 0, 0, src_canvas, dst_canvas);
            }

            epaper_port_display(epd_blackImage);

            xSemaphoreGive(epaper_gui_semapHandle);
            Green_led_arg = 0;
        }
    }
}

static void BrowserUploadRenderCurrentOnce(void)
{
    const uint32_t imagesize = ((EXAMPLE_LCD_WIDTH % 2 == 0) ? (EXAMPLE_LCD_WIDTH / 2)
                                                             : (EXAMPLE_LCD_WIDTH / 2 + 1)) *
                               EXAMPLE_LCD_HEIGHT;

    uint8_t *epd_blackImage = (uint8_t *)heap_caps_malloc(imagesize * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
    if (!epd_blackImage)
    {
        ESP_LOGE("browser_upload", "Failed to allocate e-paper buffer (%lu bytes)", (unsigned long)imagesize);
        return;
    }

    if (pdTRUE == xSemaphoreTake(epaper_gui_semapHandle, pdMS_TO_TICKS(5000)))
    {
        const uint16_t rotation = server_bsp_get_rotation();
        const uint16_t img_rotation = server_bsp_get_image_rotation();

        Paint_NewImage(epd_blackImage, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, rotation, EPD_7IN3E_WHITE);
        Paint_SetScale(6);
        Paint_SelectImage(epd_blackImage);
        Paint_Clear(EPD_7IN3E_WHITE);

        const uint16_t dst_canvas = ((rotation == ROTATE_90) || (rotation == ROTATE_270)) ? ROTATE_90 : ROTATE_0;
        const uint16_t src_canvas = ((img_rotation == ROTATE_90) || (img_rotation == ROTATE_270)) ? ROTATE_90 : ROTATE_0;

        const char *img_path = server_bsp_get_current_image_path();
        if (img_path && img_path[0] != '\0')
        {
            GUI_ReadBmp_RGB_6Color_Rotate(img_path, 0, 0, src_canvas, dst_canvas);
        }

        epaper_port_display(epd_blackImage);
        xSemaphoreGive(epaper_gui_semapHandle);
    }

    heap_caps_free(epd_blackImage);
}

static void BrowserUploadIdleSleepTask(void *arg)
{
    (void)arg;

    constexpr uint64_t kIdleTimeoutUs = 5ULL * 60ULL * 1000000ULL;
    constexpr gpio_num_t kWakeKeyPin = GPIO_NUM_4; // Key button (active-low)

    for (;;)
    {
        const uint64_t now = esp_timer_get_time();
        const uint64_t last = server_bsp_get_last_activity_us();

        // Rule: never enter deep sleep while charging.
        if (IsChargingCached())
        {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (last != 0 && now > last && (now - last) > kIdleTimeoutUs)
        {
            // Final safety check in case charging started since the last poll.
            if (PmuIsCharging())
            {
                s_is_charging = true;
                ESP_LOGI("browser_upload", "Charging detected, skip sleep");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }

            ESP_LOGI("browser_upload", "Idle for 5 minutes; entering deep sleep (wake on key button + optional timer)");

            // Stop status LEDs before sleeping.
            Red_led_arg = 0;
            Green_led_arg = 0;
            led_set(LED_PIN_Red, LED_OFF);
            led_set(LED_PIN_Green, LED_OFF);

            // Stop Wi-Fi before sleeping.
            set_espWifi_sleep();

            esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_AUTO);
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

            const uint64_t mask = 1ULL << kWakeKeyPin;
            ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ALL_LOW));
            ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(kWakeKeyPin));
            ESP_ERROR_CHECK(rtc_gpio_pullup_en(kWakeKeyPin));

            // If slideshow is enabled, also wake periodically by timer to advance photos.
            if (server_bsp_get_slideshow_enabled())
            {
                const uint64_t interval_us = (uint64_t)server_bsp_get_slideshow_interval_s() * 1000000ULL;
                ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(interval_us));
            }

            vTaskDelay(pdMS_TO_TICKS(200));
            esp_deep_sleep_start();
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void key1_button_user_Task(void *arg)
{
    (void)arg;

    for (;;)
    {
        // key_groups bit 1 is set by button_bsp on BTN_LONG_PRESS_START for USER_KEY_2.
        const EventBits_t even = xEventGroupWaitBits(key_groups, (0x02), pdFALSE, pdFALSE, pdMS_TO_TICKS(2000));
        if (get_bit_button(even, 1))
        {
            xEventGroupClearBits(key_groups, set_bit_button(1));
            server_bsp_mark_activity();
            ESP_LOGI("key", "Long press detected, rebooting");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }
    }
}

// While awake, a single click on the key button should advance to the next photo.
static void BrowserUploadKeyNextTask(void *arg)
{
    (void)arg;

    for (;;)
    {
        // key_groups bit 0 is set by button_bsp on BTN_SINGLE_CLICK for USER_KEY_2 (GPIO 4).
        const EventBits_t bits = xEventGroupWaitBits(key_groups, set_bit_button(0), pdTRUE, pdFALSE, portMAX_DELAY);
        if (get_bit_button(bits, 0))
        {
            server_bsp_mark_activity();
            if (server_bsp_select_next_photo() == ESP_OK)
            {
                xEventGroupSetBits(server_groups, set_bit_button(2));
            }
        }
    }
}

void axp2101_irq_init(void)
{
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask = ((uint64_t)0x01 << AXP2101_iqr_PIN);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;

    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
    gpio_set_level(AXP2101_iqr_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(AXP2101_iqr_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

uint8_t User_Mode_init(void)
{
    epaper_gui_semapHandle = xSemaphoreCreateMutex(); /* Acquire the mutual exclusion lock to prevent re-flashing */
    i2c_master_Init();                                /* Must be initialized */
    // axp2101_irq_init();                             /* AXP2101 Wakeup Settings */
    axp_i2c_prot_init(); /* AXP2101 Initialization */
    axp_cmd_init();      /* Enable the corresponding channel */

    if (!s_pmu_mutex)
    {
        s_pmu_mutex = xSemaphoreCreateMutex();
    }

    // Prime charging cache early so sleep policy is correct immediately after boot.
    s_is_charging = PmuIsCharging();

    led_init();          /* LED Blink Initialization */
    epaper_port_init();  /* Ink Display Initialization */

    ESP_LOGI("reset", "esp_reset_reason=%d", (int)esp_reset_reason());

    // #if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
    // Always render a 4x4 color-index test pattern (0x0..0xF) on boot for the PhotoPainter board.
    // This is intentionally before SD init so the panel shows something even if SD init fails.
    // RenderBootTestPattern();
    // #endif

    uint8_t sdcard_win = _sdcard_init(); /* SD Card Initialization */
    if (sdcard_win == 0)
        return 0;

    // Load rotation/slideshow/library state from NVS/SD.
    server_bsp_init_state();

    const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();

    // Only refresh the e-paper at boot when the selected photo actually changed.
    // E-paper retains its image without power, so a "cold" start usually does not require a refresh.
    bool need_initial_render = false;

    // Timer wake: if slideshow is enabled, advance one photo, refresh the panel, then sleep again.
    if (wake == ESP_SLEEP_WAKEUP_TIMER && server_bsp_get_slideshow_enabled())
    {
        ESP_LOGI("browser_upload", "Woke from timer for slideshow; advancing photo and returning to sleep");
        (void)server_bsp_select_next_photo();
        BrowserUploadRenderCurrentOnce();

        constexpr gpio_num_t kWakeKeyPin = GPIO_NUM_4; // Key button (active-low)

        esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_AUTO);
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

        const uint64_t mask = 1ULL << kWakeKeyPin;
        ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ALL_LOW));
        ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(kWakeKeyPin));
        ESP_ERROR_CHECK(rtc_gpio_pullup_en(kWakeKeyPin));

        const uint64_t interval_us = (uint64_t)server_bsp_get_slideshow_interval_s() * 1000000ULL;
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(interval_us));

        vTaskDelay(pdMS_TO_TICKS(200));
        esp_deep_sleep_start();
    }

    // If we woke from deep sleep via a button, do NOT change the selected photo.
    // The user can advance photos with a click while awake.
    if (wake == ESP_SLEEP_WAKEUP_EXT1)
    {
        // No-op.
    }

    Green_led_Mode_queue = xEventGroupCreate();
    Red_led_Mode_queue = xEventGroupCreate();
    epaper_groups = xEventGroupCreate();
    /*GPIO */
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.pin_bit_mask = 0x1ULL << GPIO_NUM_4;
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
    do
    {
        vTaskDelay(pdMS_TO_TICKS(50));
    } while (!gpio_get_level(GPIO_NUM_4));
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_reset_pin(GPIO_NUM_4));
    button_Init();
    xTaskCreate(key1_button_user_Task, "key1_button_user_Task", 4 * 1024, NULL, 3, NULL);
    xTaskCreate(BrowserUploadKeyNextTask, "BrowserUploadKeyNextTask", 4 * 1024, NULL, 3, NULL);
    xTaskCreate(Green_led_user_Task, "Green_led_user_Task", 3 * 1024, &Green_led_arg, 2, NULL);
    xTaskCreate(Red_led_user_Task, "Red_led_user_Task", 3 * 1024, &Red_led_arg, 2, NULL);

    // NOTE: Avoid running multiple PMU polling tasks concurrently; it can cause I2C errors/resets.
    // If you want periodic PMU logging, re-enable this task (but keep it mutually exclusive with other PMU reads).
    // xTaskCreate(axp2101_isCharging_task, "axp2101_isCharging_task", 3 * 1024, NULL, 2, NULL);

    // Browser upload app
    Network_wifi_ap_init();
    http_server_init();

    // Charging state poller (controls LED blink + sleep gating).
    xTaskCreate(ChargingStatusLedTask, "ChargingStatusLedTask", 3 * 1024, NULL, 2, NULL);

    // Heartbeat: blink red LED while the device is awake.
    Red_led_arg = 1;
    xEventGroupSetBits(Red_led_Mode_queue, set_bit_button(6));

    xTaskCreate(BrowserImageUploadDisplayTask, "BrowserImageUploadDisplayTask", 6 * 1024, NULL, 2, NULL);
    xTaskCreate(BrowserUploadIdleSleepTask, "BrowserUploadIdleSleepTask", 4 * 1024, NULL, 2, NULL);

    // Only render on startup if something changed during boot.
    if (need_initial_render)
    {
        xEventGroupSetBits(server_groups, set_bit_button(2));
    }

    return 1;
}
