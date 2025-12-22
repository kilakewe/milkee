#include "user_app.h"
#include "axp_prot.h"
#include "button_bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "freertos/semphr.h"
#include "driver/rtc_io.h"
#include "freertos/task.h"
#include "i2c_bsp.h"
#include "led_bsp.h"
#include "sdcard_bsp.h"
#include "server_bsp.h"
#include "qrcodegen.h"
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

// Persisted across deep sleep (and often across soft resets) to help distinguish
// "USB link flapping" from actual MCU reboots.
RTC_DATA_ATTR static uint32_t s_boot_counter_rtc = 0;
static uint32_t s_boot_id = 0;

// Serialize PMU reads across tasks to avoid I2C contention.
static SemaphoreHandle_t s_pmu_mutex = NULL;

static const char *ResetReasonToString(esp_reset_reason_t r)
{
    switch (r)
    {
    case ESP_RST_POWERON:
        return "POWERON";
    case ESP_RST_EXT:
        return "EXT";
    case ESP_RST_SW:
        return "SW";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INT_WDT";
    case ESP_RST_TASK_WDT:
        return "TASK_WDT";
    case ESP_RST_WDT:
        return "WDT";
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_SDIO:
        return "SDIO";
    case ESP_RST_USB:
        return "USB";
    default:
        return "UNKNOWN";
    }
}

static const char *WakeupCauseToString(esp_sleep_wakeup_cause_t c)
{
    switch (c)
    {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        return "UNDEFINED";
    case ESP_SLEEP_WAKEUP_ALL:
        return "ALL";
    case ESP_SLEEP_WAKEUP_EXT0:
        return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:
        return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER:
        return "TIMER";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        return "TOUCHPAD";
    case ESP_SLEEP_WAKEUP_ULP:
        return "ULP";
    case ESP_SLEEP_WAKEUP_GPIO:
        return "GPIO";
    case ESP_SLEEP_WAKEUP_UART:
        return "UART";
    case ESP_SLEEP_WAKEUP_WIFI:
        return "WIFI";
    case ESP_SLEEP_WAKEUP_COCPU:
        return "COCPU";
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
        return "COCPU_TRAP";
    case ESP_SLEEP_WAKEUP_BT:
        return "BT";
    case ESP_SLEEP_WAKEUP_VAD:
        return "VAD";
    case ESP_SLEEP_WAKEUP_VBAT_UNDER_VOLT:
        return "VBAT_UNDER_VOLT";
    default:
        return "UNKNOWN";
    }
}

static void BootDiagnosticsTask(void *arg)
{
    (void)arg;

    // Delay so the serial monitor has time to reattach after USB re-enumeration.
    vTaskDelay(pdMS_TO_TICKS(2000));

    const esp_reset_reason_t rr = esp_reset_reason();
    const esp_sleep_wakeup_cause_t wc = esp_sleep_get_wakeup_cause();
    const uint32_t up_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    // Basic chip + heap snapshot.
    esp_chip_info_t chip = {};
    esp_chip_info(&chip);

    // Keep this log strictly 32-bit types to avoid nano printf varargs pitfalls.
    ESP_LOGW("bootdiag",
             "boot_id=%u uptime_ms=%u reset=%d wake=%d cores=%d rev=%d",
             (unsigned)s_boot_id,
             (unsigned)up_ms,
             (int)rr,
             (int)wc,
             (int)chip.cores,
             (int)chip.revision);

    ESP_LOGW("bootdiag", "reset_str=%s wake_str=%s", ResetReasonToString(rr), WakeupCauseToString(wc));

    ESP_LOGW("bootdiag",
             "heap_free=%u heap_min_free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));

    // Best-effort PMU snapshot.
    bool took = true;
    if (s_pmu_mutex)
    {
        took = (xSemaphoreTake(s_pmu_mutex, pdMS_TO_TICKS(200)) == pdTRUE);
    }

    if (took)
    {
        const bool charging = axp2101_is_charging();
        const bool vbus_in = axp2101_is_vbus_in();
        const bool vbus_good = axp2101_is_vbus_good();
        const int batt_mv = axp2101_get_batt_voltage_mv();
        const int vbus_mv = axp2101_get_vbus_voltage_mv();
        const int sys_mv = axp2101_get_sys_voltage_mv();
        const int chg_status = axp2101_get_charger_status();

        ESP_LOGW("bootdiag",
                 "pmu charging=%d vbus_in=%d vbus_good=%d batt_mv=%d vbus_mv=%d sys_mv=%d chg_status=%d",
                 (int)charging,
                 (int)vbus_in,
                 (int)vbus_good,
                 batt_mv,
                 vbus_mv,
                 sys_mv,
                 chg_status);

        if (s_pmu_mutex)
        {
            xSemaphoreGive(s_pmu_mutex);
        }
    }
    else
    {
        ESP_LOGW("bootdiag", "pmu snapshot skipped (mutex timeout)");
    }

    vTaskDelete(NULL);
}

// Cached charging state used for power management decisions.
// Rule: never enter deep sleep while charging.
static volatile bool s_is_charging = false;

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

static void BrowserUploadWaitForNetworkQuiet(uint32_t quiet_ms, uint32_t max_wait_ms)
{
    const uint64_t quiet_us = (uint64_t)quiet_ms * 1000ULL;
    const uint64_t max_wait_us = (uint64_t)max_wait_ms * 1000ULL;

    const uint64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < max_wait_us)
    {
        const uint64_t now = esp_timer_get_time();
        const uint64_t last = server_bsp_get_last_activity_us();

        // If we've been quiet long enough (or activity is unset), allow refresh.
        if (last == 0 || now < last || (now - last) >= quiet_us)
        {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static inline int BrowserUploadMinInt(int a, int b)
{
    return (a < b) ? a : b;
}

static void BrowserUploadGetBatterySnapshot(int *out_percent, bool *out_charging, bool *out_discharging)
{
    if (out_percent)
    {
        *out_percent = -1;
    }
    if (out_charging)
    {
        *out_charging = false;
    }
    if (out_discharging)
    {
        *out_discharging = false;
    }

    bool took = true;
    if (s_pmu_mutex)
    {
        took = (xSemaphoreTake(s_pmu_mutex, pdMS_TO_TICKS(200)) == pdTRUE);
    }

    if (!took)
    {
        return;
    }

    const bool charging = axp2101_is_charging();
    const bool discharging = axp2101_is_discharging();
    const int percent = axp2101_get_battery_percent();

    if (out_charging)
    {
        *out_charging = charging;
    }
    if (out_discharging)
    {
        *out_discharging = discharging;
    }
    if (out_percent)
    {
        *out_percent = percent;
    }

    if (s_pmu_mutex)
    {
        xSemaphoreGive(s_pmu_mutex);
    }
}

static void BrowserUploadDrawCircleBadge48(int x, int y, UWORD fill_color)
{
    constexpr int kSize = 48;
    constexpr int kRadius = 23; // 2*23+1 = 47px diameter (fits comfortably in a 48px box)

    const int cx = x + kSize / 2;
    const int cy = y + kSize / 2;

    // Fill.
    Paint_DrawCircle((UWORD)cx, (UWORD)cy, (UWORD)kRadius, fill_color, DOT_PIXEL_DFT, DRAW_FILL_FULL);

    // Black ring border (2px).
    Paint_DrawCircle((UWORD)cx, (UWORD)cy, (UWORD)kRadius, EPD_7IN3E_BLACK, DOT_PIXEL_DFT, DRAW_FILL_EMPTY);
    Paint_DrawCircle((UWORD)cx, (UWORD)cy, (UWORD)(kRadius - 1), EPD_7IN3E_BLACK, DOT_PIXEL_DFT, DRAW_FILL_EMPTY);
}

static void BrowserUploadDrawCenteredText48(int x, int y, const char *text, UWORD fg, UWORD bg)
{
    (void)bg;

    if (!text)
    {
        return;
    }

    // We intentionally draw the battery text with a tiny, transparent bitmap font.
    // Using the built-in font drawing routines with a non-white background paints a
    // rectangular background behind the glyphs, which spills outside the circle.

    constexpr int kSize = 48;
    constexpr int kCols = 3;
    constexpr int kRows = 5;

    // 3x5 glyphs, stored row-major, MSB is leftmost pixel.
    // Example: 0b111 means "###".
    static const uint8_t kDigits[10][kRows] = {
        {0b111, 0b101, 0b101, 0b101, 0b111}, // 0
        {0b010, 0b110, 0b010, 0b010, 0b111}, // 1
        {0b111, 0b001, 0b111, 0b100, 0b111}, // 2
        {0b111, 0b001, 0b111, 0b001, 0b111}, // 3
        {0b101, 0b101, 0b111, 0b001, 0b001}, // 4
        {0b111, 0b100, 0b111, 0b001, 0b111}, // 5
        {0b111, 0b100, 0b111, 0b101, 0b111}, // 6
        {0b111, 0b001, 0b010, 0b010, 0b010}, // 7
        {0b111, 0b101, 0b111, 0b101, 0b111}, // 8
        {0b111, 0b101, 0b111, 0b001, 0b111}, // 9
    };

    static const uint8_t kHyphen[kRows] = {0b000, 0b000, 0b111, 0b000, 0b000};

    const int len = (int)strlen(text);
    if (len <= 0)
    {
        return;
    }

    constexpr int kScale = 4; // size of one "pixel" in the tiny font (in screen pixels)
    constexpr int kGapPx = 2;

    const int glyph_w = kCols * kScale;
    const int glyph_h = kRows * kScale;
    const int total_w = len * glyph_w + (len - 1) * kGapPx;

    const int start_x = x + (kSize - total_w) / 2;
    const int start_y = y + (kSize - glyph_h) / 2;

    for (int ci = 0; ci < len; ci++)
    {
        const char c = text[ci];

        const uint8_t *rows = nullptr;
        uint8_t scratch[kRows] = {0};

        if (c >= '0' && c <= '9')
        {
            rows = kDigits[c - '0'];
        }
        else if (c == '-')
        {
            rows = kHyphen;
        }
        else
        {
            // Unknown: render as blank.
            rows = scratch;
        }

        const int gx = start_x + ci * (glyph_w + kGapPx);
        const int gy = start_y;

        for (int r = 0; r < kRows; r++)
        {
            const uint8_t bits = rows[r];
            for (int col = 0; col < kCols; col++)
            {
                const bool on = (bits & (1U << (kCols - 1 - col))) != 0;
                if (!on)
                {
                    continue;
                }

                const int px = gx + col * kScale;
                const int py = gy + r * kScale;

                for (int yy = 0; yy < kScale; yy++)
                {
                    for (int xx = 0; xx < kScale; xx++)
                    {
                        Paint_SetPixel((UWORD)(px + xx), (UWORD)(py + yy), fg);
                    }
                }
            }
        }
    }
}

static void BrowserUploadDrawWifiIcon48(int x, int y, bool connected, UWORD fg)
{
    constexpr int kSize = 48;

    if (!connected)
    {
        // Draw a simple "X" for disconnected.
        const int x1 = x + 16;
        const int y1 = y + 16;
        const int x2 = x + 32;
        const int y2 = y + 32;
        Paint_DrawLine((UWORD)x1, (UWORD)y1, (UWORD)x2, (UWORD)y2, fg, DOT_PIXEL_3X3, LINE_STYLE_SOLID);
        Paint_DrawLine((UWORD)x2, (UWORD)y1, (UWORD)x1, (UWORD)y2, fg, DOT_PIXEL_3X3, LINE_STYLE_SOLID);
        return;
    }

    // Connected: draw simple signal bars.
    constexpr int kBarW = 6;
    constexpr int kGap = 4;
    constexpr int kNumBars = 3;

    const int total_w = kNumBars * kBarW + (kNumBars - 1) * kGap; // 26
    const int x0 = x + (kSize - total_w) / 2;
    const int base_y = y + 38;

    const int heights[kNumBars] = {10, 16, 22};

    for (int i = 0; i < kNumBars; i++)
    {
        const int h = heights[i];
        const int xs = x0 + i * (kBarW + kGap);
        const int ys = base_y - h;
        Paint_DrawRectangle((UWORD)xs, (UWORD)ys, (UWORD)(xs + kBarW - 1), (UWORD)(base_y + 1), fg, DOT_PIXEL_DFT,
                            DRAW_FILL_FULL);
    }
}

static void BrowserUploadDrawStatusIconsOverlayIfEnabled(void)
{
    if (!server_bsp_get_status_icons_enabled())
    {
        return;
    }

    constexpr int kSize = 48;
    constexpr int kGap = 6;
    constexpr int kMargin = 8;

    const int W = (int)Paint.Width;
    const int H = (int)Paint.Height;

    const int y = H - kMargin - kSize;
    const int x_batt = W - kMargin - kSize;
    const int x_wifi = x_batt - kGap - kSize;

    // Battery badge.
    int batt_pct = -1;
    bool charging = false;
    bool discharging = false;
    BrowserUploadGetBatterySnapshot(&batt_pct, &charging, &discharging);

    UWORD batt_fill = EPD_7IN3E_BLUE;
    UWORD batt_text = EPD_7IN3E_WHITE;

    // Color rules (low battery overrides state).
    if (batt_pct >= 0 && batt_pct < 10)
    {
        batt_fill = EPD_7IN3E_RED;
        batt_text = EPD_7IN3E_WHITE;
    }
    else if (batt_pct >= 0 && batt_pct < 20)
    {
        batt_fill = EPD_7IN3E_YELLOW;
        batt_text = EPD_7IN3E_BLACK; // requested black text for yellow
    }
    else if (charging)
    {
        batt_fill = EPD_7IN3E_GREEN;
        batt_text = EPD_7IN3E_WHITE;
    }
    else if (discharging)
    {
        batt_fill = EPD_7IN3E_BLUE;
        batt_text = EPD_7IN3E_WHITE;
    }
    else
    {
        batt_fill = EPD_7IN3E_BLUE;
        batt_text = EPD_7IN3E_WHITE;
    }

    BrowserUploadDrawCircleBadge48(x_batt, y, batt_fill);

    // Note: batt_pct is an int; keep the buffer large enough to avoid -Wformat-truncation under -Werror.
    char batt_str[16] = {0};
    if (batt_pct < 0)
    {
        snprintf(batt_str, sizeof(batt_str), "--");
    }
    else
    {
        snprintf(batt_str, sizeof(batt_str), "%d", batt_pct);
    }
    BrowserUploadDrawCenteredText48(x_batt, y, batt_str, batt_text, batt_fill);

    // Wi-Fi badge.
    if (x_wifi >= 0)
    {
        server_bsp_network_info_t net = {};
        server_bsp_get_network_info(&net);

        UWORD wifi_fill = EPD_7IN3E_WHITE;
        UWORD wifi_fg = EPD_7IN3E_BLACK;
        bool wifi_connected = false;

        if (net.mode == SERVER_BSP_WIFI_MODE_AP)
        {
            wifi_fill = EPD_7IN3E_BLUE;
            wifi_fg = EPD_7IN3E_WHITE;
            wifi_connected = true;
        }
        else if (net.mode == SERVER_BSP_WIFI_MODE_STA)
        {
            if (net.sta_connected)
            {
                wifi_fill = EPD_7IN3E_GREEN;
                wifi_fg = EPD_7IN3E_WHITE;
                wifi_connected = true;
            }
            else
            {
                wifi_fill = EPD_7IN3E_RED;
                wifi_fg = EPD_7IN3E_WHITE;
                wifi_connected = false;
            }
        }

        BrowserUploadDrawCircleBadge48(x_wifi, y, wifi_fill);
        BrowserUploadDrawWifiIcon48(x_wifi, y, wifi_connected, wifi_fg);
    }
}

static void BrowserUploadEscapeWifiQrField(const char *in, char *out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    out[0] = '\0';
    if (!in)
    {
        return;
    }

    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && (j + 1) < out_len; i++)
    {
        const char c = in[i];
        const bool needs_escape = (c == '\\' || c == ';' || c == ',' || c == ':');
        if (needs_escape)
        {
            if ((j + 2) >= out_len)
            {
                break;
            }
            out[j++] = '\\';
            out[j++] = c;
        }
        else
        {
            out[j++] = c;
        }
    }
    out[j] = '\0';
}

static bool BrowserUploadDrawQrCode(const char *text, int x, int y, int w, int h)
{
    if (!text || text[0] == '\0')
    {
        return false;
    }

    uint8_t *qrcode = (uint8_t *)heap_caps_malloc(qrcodegen_BUFFER_LEN_MAX, MALLOC_CAP_8BIT);
    uint8_t *temp = (uint8_t *)heap_caps_malloc(qrcodegen_BUFFER_LEN_MAX, MALLOC_CAP_8BIT);
    if (!qrcode || !temp)
    {
        if (qrcode)
        {
            heap_caps_free(qrcode);
        }
        if (temp)
        {
            heap_caps_free(temp);
        }
        return false;
    }

    const bool ok = qrcodegen_encodeText(text,
                                        temp,
                                        qrcode,
                                        qrcodegen_Ecc_MEDIUM,
                                        qrcodegen_VERSION_MIN,
                                        qrcodegen_VERSION_MAX,
                                        qrcodegen_Mask_AUTO,
                                        true);
    if (!ok)
    {
        heap_caps_free(qrcode);
        heap_caps_free(temp);
        return false;
    }

    const int size = qrcodegen_getSize(qrcode);
    const int border = 4;
    const int total_modules = size + border * 2;

    int scale = 1;
    if (total_modules > 0)
    {
        scale = BrowserUploadMinInt(w / total_modules, h / total_modules);
    }
    if (scale < 1)
    {
        heap_caps_free(qrcode);
        heap_caps_free(temp);
        return false;
    }

    const int qr_px = total_modules * scale;
    const int x0 = x + (w - qr_px) / 2;
    const int y0 = y + (h - qr_px) / 2;

    // White background (including quiet zone).
    Paint_ClearWindows((UWORD)x0, (UWORD)y0, (UWORD)(x0 + qr_px), (UWORD)(y0 + qr_px), EPD_7IN3E_WHITE);

    for (int j = 0; j < size; j++)
    {
        for (int i = 0; i < size; i++)
        {
            if (qrcodegen_getModule(qrcode, i, j))
            {
                const int xx = x0 + (i + border) * scale;
                const int yy = y0 + (j + border) * scale;
                Paint_ClearWindows((UWORD)xx, (UWORD)yy, (UWORD)(xx + scale), (UWORD)(yy + scale), EPD_7IN3E_BLACK);
            }
        }
    }

    heap_caps_free(qrcode);
    heap_caps_free(temp);
    return true;
}

static void BrowserUploadDrawWrappedString(int x, int y, int w, const char *text, int max_lines)
{
    if (!text || text[0] == '\0' || max_lines <= 0)
    {
        return;
    }

    const int char_w = (int)Font24.Width;
    if (char_w <= 0)
    {
        return;
    }

    const int max_chars = w / char_w;
    if (max_chars <= 0)
    {
        return;
    }

    const char *p = text;
    for (int line = 0; line < max_lines && *p; line++)
    {
        int n = 0;
        while (p[n] != '\0' && n < max_chars)
        {
            n++;
        }

        // If we're not at end-of-string, try to break on a nice boundary.
        int cut = n;
        if (p[n] != '\0')
        {
            for (int k = n - 1; k > 0 && k > (n - 12); k--)
            {
                const char c = p[k];
                if (c == '/' || c == '-' || c == '.')
                {
                    cut = k + 1;
                    break;
                }
            }
        }

        char buf[128] = {0};
        const int copy_n = BrowserUploadMinInt(cut, (int)sizeof(buf) - 1);
        snprintf(buf, sizeof(buf), "%.*s", copy_n, p);
        Paint_DrawString_EN((UWORD)x, (UWORD)y, buf, &Font24, EPD_7IN3E_BLACK, EPD_7IN3E_WHITE);

        y += (int)Font24.Height + 4;
        p += cut;
    }
}

static void BrowserUploadRenderConnectionInfoOnce(void)
{
    server_bsp_network_info_t net = {};
    server_bsp_get_network_info(&net);

    const uint32_t imagesize = ((EXAMPLE_LCD_WIDTH % 2 == 0) ? (EXAMPLE_LCD_WIDTH / 2)
                                                             : (EXAMPLE_LCD_WIDTH / 2 + 1)) *
                               EXAMPLE_LCD_HEIGHT;

    uint8_t *epd_blackImage = (uint8_t *)heap_caps_malloc(imagesize * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
    if (!epd_blackImage)
    {
        ESP_LOGE("browser_upload", "Failed to allocate e-paper buffer (%lu bytes)", (unsigned long)imagesize);
        return;
    }

    // Avoid overlapping a refresh with bursts of Wi-Fi traffic.
    BrowserUploadWaitForNetworkQuiet(/*quiet_ms=*/600, /*max_wait_ms=*/5000);

    if (pdTRUE == xSemaphoreTake(epaper_gui_semapHandle, pdMS_TO_TICKS(5000)))
    {
        const uint16_t rotation = server_bsp_get_rotation();
        Paint_NewImage(epd_blackImage, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, rotation, EPD_7IN3E_WHITE);
        Paint_SetScale(6);
        Paint_SelectImage(epd_blackImage);
        Paint_Clear(EPD_7IN3E_WHITE);

        const int W = (int)Paint.Width;
        const int H = (int)Paint.Height;
        const int margin = 20;
        const int line_h = (int)Font24.Height + 6;

        int y = margin;

        Paint_DrawString_EN((UWORD)margin, (UWORD)y, "Connect to Frame", &Font24, EPD_7IN3E_BLACK, EPD_7IN3E_WHITE);
        y += line_h;

        char line[96] = {0};
        if (net.mode == SERVER_BSP_WIFI_MODE_STA)
        {
            snprintf(line, sizeof(line), "Wi-Fi: %s", net.sta_ssid[0] ? net.sta_ssid : "(unknown)");
        }
        else if (net.mode == SERVER_BSP_WIFI_MODE_AP)
        {
            snprintf(line, sizeof(line), "AP: %s", net.ap_ssid[0] ? net.ap_ssid : "(unknown)");
        }
        else
        {
            snprintf(line, sizeof(line), "Network: starting...");
        }
        Paint_DrawString_EN((UWORD)margin, (UWORD)y, line, &Font24, EPD_7IN3E_BLACK, EPD_7IN3E_WHITE);
        y += line_h;

        if (net.hostname[0] != '\0')
        {
            snprintf(line, sizeof(line), "Name: %s", net.hostname);
            Paint_DrawString_EN((UWORD)margin, (UWORD)y, line, &Font24, EPD_7IN3E_BLACK, EPD_7IN3E_WHITE);
            y += line_h;
        }

        const int content_y = y + 10;
        const int content_h = H - content_y - margin;

        struct Panel
        {
            int x;
            int y;
            int w;
            int h;
        };

        Panel left = {0};
        Panel right = {0};

        if (W >= H)
        {
            // Landscape: two columns.
            const int panel_w = (W - margin * 3) / 2;
            const int panel_h = content_h;
            left = Panel{margin, content_y, panel_w, panel_h};
            right = Panel{margin * 2 + panel_w, content_y, panel_w, panel_h};
        }
        else
        {
            // Portrait: two rows.
            const int panel_w = W - margin * 2;
            const int panel_h = (content_h - margin) / 2;
            left = Panel{margin, content_y, panel_w, panel_h};
            right = Panel{margin, content_y + panel_h + margin, panel_w, panel_h};
        }

        const int label_h = line_h * 3;
        const int qr_size_left = BrowserUploadMinInt(left.w, left.h - label_h);
        const int qr_size_right = BrowserUploadMinInt(right.w, right.h - label_h);

        if (net.mode == SERVER_BSP_WIFI_MODE_STA && net.sta_connected && net.sta_ip[0] != '\0')
        {
            char url_ip[64] = {0};
            snprintf(url_ip, sizeof(url_ip), "http://%s/", net.sta_ip);

            char url_host[96] = {0};
            char url_host_local[96] = {0};
            if (net.hostname[0] != '\0')
            {
                snprintf(url_host, sizeof(url_host), "http://%s/", net.hostname);
                snprintf(url_host_local, sizeof(url_host_local), "http://%s.local/", net.hostname);
            }

            // Left: easy URL
            // Note: mDNS typically resolves with the .local suffix, but some LANs also resolve the
            // DHCP hostname directly. We show both, and prefer the non-.local URL for the QR.
            const char *easy_qr = (url_host[0] != '\0') ? url_host : url_host_local;
            const int lx = left.x + (left.w - qr_size_left) / 2;
            const int ly = left.y;
            if (!BrowserUploadDrawQrCode(easy_qr, lx, ly, qr_size_left, qr_size_left))
            {
                Paint_DrawString_EN((UWORD)left.x, (UWORD)left.y, "QR failed", &Font24, EPD_7IN3E_BLACK, EPD_7IN3E_WHITE);
            }
            int ty = left.y + qr_size_left + 6;
            Paint_DrawString_EN((UWORD)left.x, (UWORD)ty, "Easy URL", &Font24, EPD_7IN3E_BLACK, EPD_7IN3E_WHITE);
            ty += line_h;
            BrowserUploadDrawWrappedString(left.x, ty, left.w, url_host, 1);
            ty += line_h;
            BrowserUploadDrawWrappedString(left.x, ty, left.w, url_host_local, 1);

            // Right: IP URL
            const int rx = right.x + (right.w - qr_size_right) / 2;
            const int ry = right.y;
            if (!BrowserUploadDrawQrCode(url_ip, rx, ry, qr_size_right, qr_size_right))
            {
                Paint_DrawString_EN((UWORD)right.x, (UWORD)right.y, "QR failed", &Font24, EPD_7IN3E_BLACK, EPD_7IN3E_WHITE);
            }
            ty = right.y + qr_size_right + 6;
            Paint_DrawString_EN((UWORD)right.x, (UWORD)ty, "IP URL", &Font24, EPD_7IN3E_BLACK, EPD_7IN3E_WHITE);
            ty += line_h;
            BrowserUploadDrawWrappedString(right.x, ty, right.w, url_ip, 2);
        }
        else
        {
            // AP mode (or not connected yet): show Wi-Fi join QR + AP web URL.
            const char *ap_ip = server_bsp_get_ap_ip();

            char url_ap[64] = {0};
            snprintf(url_ap, sizeof(url_ap), "http://%s/", ap_ip ? ap_ip : "192.168.4.1");

            char esc_ssid[96] = {0};
            char esc_pass[96] = {0};
            BrowserUploadEscapeWifiQrField(net.ap_ssid[0] ? net.ap_ssid : "esp_network", esc_ssid, sizeof(esc_ssid));
            BrowserUploadEscapeWifiQrField(net.ap_password, esc_pass, sizeof(esc_pass));

            char wifi_qr[256] = {0};
            if (net.ap_password[0] != '\0')
            {
                snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:WPA;S:%s;P:%s;;", esc_ssid, esc_pass);
            }
            else
            {
                snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:nopass;S:%s;;", esc_ssid);
            }

            // Left: Wi-Fi credentials
            const int lx = left.x + (left.w - qr_size_left) / 2;
            const int ly = left.y;
            (void)BrowserUploadDrawQrCode(wifi_qr, lx, ly, qr_size_left, qr_size_left);
            int ty = left.y + qr_size_left + 6;
            Paint_DrawString_EN((UWORD)left.x, (UWORD)ty, "Join Wi-Fi", &Font24, EPD_7IN3E_BLACK, EPD_7IN3E_WHITE);
            ty += line_h;
            char ss[128] = {0};
            snprintf(ss, sizeof(ss), "SSID: %s", net.ap_ssid[0] ? net.ap_ssid : "esp_network");
            BrowserUploadDrawWrappedString(left.x, ty, left.w, ss, 1);
            ty += line_h;
            char pp[128] = {0};
            snprintf(pp, sizeof(pp), "PASS: %s", net.ap_password[0] ? net.ap_password : "(open)");
            BrowserUploadDrawWrappedString(left.x, ty, left.w, pp, 1);

            // Right: Web UI URL
            const int rx = right.x + (right.w - qr_size_right) / 2;
            const int ry = right.y;
            (void)BrowserUploadDrawQrCode(url_ap, rx, ry, qr_size_right, qr_size_right);
            ty = right.y + qr_size_right + 6;
            Paint_DrawString_EN((UWORD)right.x, (UWORD)ty, "Open Web UI", &Font24, EPD_7IN3E_BLACK, EPD_7IN3E_WHITE);
            ty += line_h;
            BrowserUploadDrawWrappedString(right.x, ty, right.w, url_ap, 2);
        }

        // Optional status overlay (battery + Wi-Fi).
        BrowserUploadDrawStatusIconsOverlayIfEnabled();

        // Blink green once before the panel refresh.
        led_set(LED_PIN_Green, LED_ON);
        vTaskDelay(pdMS_TO_TICKS(120));
        led_set(LED_PIN_Green, LED_OFF);

        epaper_port_display(epd_blackImage);
        xSemaphoreGive(epaper_gui_semapHandle);
    }

    heap_caps_free(epd_blackImage);
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

        // Upload state notifications used to drive status LEDs.
        // (Disabled: user requested no blinking lights.)

        if (!get_bit_button(bits, 2))
        {
            continue;
        }

        // Avoid overlapping bursts of Wi-Fi traffic (follow-up HTTP GETs) with an
        // e-paper refresh, which can cause large peak current draw on some supplies.
        BrowserUploadWaitForNetworkQuiet(/*quiet_ms=*/600, /*max_wait_ms=*/5000);

        if (pdTRUE == xSemaphoreTake(epaper_gui_semapHandle, pdMS_TO_TICKS(2000)))
        {
            // Re-init the paint buffer for the current rotation.
            // IMPORTANT: Paint_SetRotate() does not update Paint.Width/Paint.Height, so for 90/270
            // we must call Paint_NewImage() to swap the logical dimensions safely.
            const uint16_t rotation = server_bsp_get_rotation();
            Paint_NewImage(epd_blackImage, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, rotation, EPD_7IN3E_WHITE);
            Paint_SetScale(6);
            Paint_SelectImage(epd_blackImage);
            Paint_Clear(EPD_7IN3E_WHITE);

            const char *img_path = server_bsp_get_current_image_path();
            if (img_path && img_path[0] != '\0')
            {
                int iw = 0;
                int ih = 0;
                const bool ok = GUI_Bmp_GetDimensions(img_path, &iw, &ih);
                if (ok && iw > 0 && ih > 0)
                {
                    const bool img_square = (iw == ih);
                    const bool img_landscape = (iw > ih);
                    const bool frame_landscape = (Paint.Width >= Paint.Height);
                    const bool mismatch = (!img_square) && (img_landscape != frame_landscape);

                    // If orientations differ, fit-scale to the frame; otherwise just center (no upscale).
                    const bool allow_upscale = mismatch;
                    GUI_DrawBmp_RGB_6Color_Fit(img_path, 0, 0, Paint.Width, Paint.Height, allow_upscale);
                }
                else
                {
                    // Fallback: best-effort draw without scaling.
                    GUI_ReadBmp_RGB_6Color(img_path, 0, 0);
                }
            }

            // Optional status overlay (battery + Wi-Fi).
            BrowserUploadDrawStatusIconsOverlayIfEnabled();

            // Blink green once before the panel refresh.
            led_set(LED_PIN_Green, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(120));
            led_set(LED_PIN_Green, LED_OFF);

            epaper_port_display(epd_blackImage);

            xSemaphoreGive(epaper_gui_semapHandle);
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

        Paint_NewImage(epd_blackImage, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, rotation, EPD_7IN3E_WHITE);
        Paint_SetScale(6);
        Paint_SelectImage(epd_blackImage);
        Paint_Clear(EPD_7IN3E_WHITE);

        const char *img_path = server_bsp_get_current_image_path();
        if (img_path && img_path[0] != '\0')
        {
            int iw = 0;
            int ih = 0;
            const bool ok = GUI_Bmp_GetDimensions(img_path, &iw, &ih);
            if (ok && iw > 0 && ih > 0)
            {
                const bool img_square = (iw == ih);
                const bool img_landscape = (iw > ih);
                const bool frame_landscape = (Paint.Width >= Paint.Height);
                const bool mismatch = (!img_square) && (img_landscape != frame_landscape);

                const bool allow_upscale = mismatch;
                GUI_DrawBmp_RGB_6Color_Fit(img_path, 0, 0, Paint.Width, Paint.Height, allow_upscale);
            }
            else
            {
                GUI_ReadBmp_RGB_6Color(img_path, 0, 0);
            }
        }

        // Optional status overlay (battery + Wi-Fi).
        BrowserUploadDrawStatusIconsOverlayIfEnabled();

        // Blink green once before the panel refresh.
        led_set(LED_PIN_Green, LED_ON);
        vTaskDelay(pdMS_TO_TICKS(120));
        led_set(LED_PIN_Green, LED_OFF);

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
            ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW));
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
    // Increment early so we can detect real reboots even if the monitor attaches late.
    s_boot_id = ++s_boot_counter_rtc;

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

    led_init();         /* LED Blink Initialization */
    epaper_port_init(); /* Ink Display Initialization */

    // Immediate boot marker (may be missed if host attaches late).
    ESP_LOGI("boot", "boot_id=%u esp_reset_reason=%d", (unsigned)s_boot_id, (int)esp_reset_reason());

    // Delayed marker + extended snapshot (helps when USB re-enumeration causes late attach).
    xTaskCreate(BootDiagnosticsTask, "BootDiagnosticsTask", 4 * 1024, NULL, 3, NULL);

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

    // Ensure the server event group exists even if the HTTP server is disabled.
    // Some tasks (e.g. BrowserImageUploadDisplayTask) wait on this handle.
    if (!server_groups)
    {
        server_groups = xEventGroupCreate();
    }

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
        ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW));
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

    // Status LED blinking disabled.
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
    // Status LED blinking disabled.

    // NOTE: Avoid running multiple PMU polling tasks concurrently; it can cause I2C errors/resets.
    // If you want periodic PMU logging, re-enable this task (but keep it mutually exclusive with other PMU reads).
    // xTaskCreate(axp2101_isCharging_task, "axp2101_isCharging_task", 3 * 1024, NULL, 2, NULL);

    // Browser upload app
    Network_wifi_init();
    http_server_init();

    // Show connection instructions (URLs + QR codes) on boot.
    BrowserUploadRenderConnectionInfoOnce();

    // Charging state poller (controls LED blink + sleep gating).
    // xTaskCreate(ChargingStatusLedTask, "ChargingStatusLedTask", 3 * 1024, NULL, 2, NULL);

    // Status LEDs:
    // - Red stays on while awake.
    // - Green blinks once right before an image refresh.
    led_set(LED_PIN_Red, LED_ON);
    led_set(LED_PIN_Green, LED_OFF);

    xTaskCreate(BrowserImageUploadDisplayTask, "BrowserImageUploadDisplayTask", 6 * 1024, NULL, 2, NULL);
    xTaskCreate(BrowserUploadIdleSleepTask, "BrowserUploadIdleSleepTask", 4 * 1024, NULL, 2, NULL);

    // Only render on startup if something changed during boot.
    if (need_initial_render)
    {
        xEventGroupSetBits(server_groups, set_bit_button(2));
    }

    return 1;
}
