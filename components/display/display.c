#include <string.h>
#include "sdkconfig.h"
#include "display.h"
#include "display_panel.h"
#include "board.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "display";

#define LCD_HOST        SPI2_HOST
#define CHUNK_LINES     20                              /* lines sent per SPI transfer */
#define CHUNK_PIXELS    (BOARD_LCD_H_RES * CHUNK_LINES)
#define BL_RESOLUTION   LEDC_TIMER_10_BIT
#define BL_FULL         1024
#define BL_FREQ_HZ      5000

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_done;        /* given when a colour transfer has finished */
static SemaphoreHandle_t s_lock;        /* one drawing call at a time */
static uint16_t *s_buf;                 /* DMA-capable transfer buffer */
static bool s_ready;
static bool s_bl_ready;
static int s_bl_percent;
static void (*s_takeover_hook)(void);

void display_set_takeover_hook(void (*hook)(void))
{
    s_takeover_hook = hook;
}

void display_takeover(void)
{
    if (s_takeover_hook) {
        s_takeover_hook();
    }
}

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

/* ---------------- backlight ---------------- */

static esp_err_t backlight_init(void)
{
    if (s_bl_ready) {
        return ESP_OK;
    }
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BOARD_LEDC_TIMER_BL,
        .duty_resolution = BL_RESOLUTION,
        .freq_hz = BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    const ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BOARD_LEDC_CH_BL,
        .timer_sel = BOARD_LEDC_TIMER_BL,
        .gpio_num = BOARD_LCD_BL,
        .duty = 0,
        .hpoint = 0,
#if !CONFIG_KERO_LCD_BL_ACTIVE_HIGH
        .flags.output_invert = 1,
#endif
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "backlight channel");
    s_bl_ready = true;
    return ESP_OK;
}

esp_err_t display_set_backlight(int percent)
{
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, BOARD_LEDC_CH_BL, (uint32_t)percent * BL_FULL / 100), TAG, "duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, BOARD_LEDC_CH_BL), TAG, "duty");
    s_bl_percent = percent;
    return ESP_OK;
}

int display_get_backlight(void)
{
    return s_bl_percent;
}

/* ---------------- panel ---------------- */

bool display_is_ready(void)
{
    return s_ready;
}

int display_width(void)
{
    return BOARD_LCD_H_RES;
}

int display_height(void)
{
    return BOARD_LCD_V_RES;
}

uint16_t display_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    const uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c << 8) | (c >> 8));     /* the panel wants the high byte first */
}

esp_err_t display_init(void)
{
    /* Backlight first: it is useful on its own and shows that the board is alive.
     * A repeated init keeps the brightness that is set now. */
    ESP_RETURN_ON_ERROR(display_set_backlight(s_bl_ready ? s_bl_percent : CONFIG_KERO_LCD_BL_DEFAULT), TAG, "backlight");

    if (!s_done) {
        s_done = xSemaphoreCreateBinary();
        s_lock = xSemaphoreCreateMutex();
        s_buf = heap_caps_malloc(CHUNK_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        ESP_RETURN_ON_FALSE(s_done && s_lock && s_buf, ESP_ERR_NO_MEM, TAG, "out of memory");
    }

    if (!s_io) {
        const spi_bus_config_t bus = {
            .sclk_io_num = BOARD_LCD_SCK,
            .data0_io_num = BOARD_LCD_D0,
            .data1_io_num = BOARD_LCD_D1,
            .data2_io_num = BOARD_LCD_D2,
            .data3_io_num = BOARD_LCD_D3,
            .max_transfer_sz = CHUNK_PIXELS * sizeof(uint16_t),
        };
        ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

        display_panel_io_params_t p;
        display_panel_get_io_params(&p);
        const esp_lcd_panel_io_spi_config_t io_cfg = {
            .cs_gpio_num = BOARD_LCD_CS,
            .dc_gpio_num = -1,
            .spi_mode = p.spi_mode,
            .pclk_hz = CONFIG_KERO_LCD_PCLK_MHZ * 1000 * 1000,
            .trans_queue_depth = 4,
            .on_color_trans_done = on_color_trans_done,
            .lcd_cmd_bits = p.lcd_cmd_bits,
            .lcd_param_bits = p.lcd_param_bits,
            .flags.quad_mode = true,
        };
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io), TAG, "panel io");
    }
    if (!s_panel) {
        ESP_RETURN_ON_ERROR(display_panel_create(s_io, &s_panel), TAG, "panel create");
    }
    /* Not while another task is drawing (this may be a repeated init). */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = esp_lcd_panel_reset(s_panel);
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(s_panel);
    }
#if CONFIG_KERO_LCD_INVERT_COLOR
    if (err == ESP_OK) {
        err = esp_lcd_panel_invert_color(s_panel, true);
    }
#endif
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(s_panel, true);
    }
    xSemaphoreGive(s_lock);
    ESP_RETURN_ON_ERROR(err, TAG, "panel reset / init");

    s_ready = true;
    ESP_LOGI(TAG, "%s %dx%d on QSPI at %d MHz (the panel cannot be read back: check the picture)",
             display_panel_name(), BOARD_LCD_H_RES, BOARD_LCD_V_RES, CONFIG_KERO_LCD_PCLK_MHZ);
    return display_fill(0);
}

/* Send s_buf (w x h pixels) and wait until the transfer is complete. */
static esp_err_t flush_chunk(int x, int y, int w, int h)
{
    xSemaphoreTake(s_done, 0);      /* clear a stale signal */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, s_buf), TAG, "draw_bitmap");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_done, pdMS_TO_TICKS(1000)) == pdTRUE, ESP_ERR_TIMEOUT, TAG, "transfer timeout");
    return ESP_OK;
}

static bool in_bounds(int x, int y, int w, int h)
{
    return w > 0 && h > 0 && x >= 0 && y >= 0 && x + w <= BOARD_LCD_H_RES && y + h <= BOARD_LCD_V_RES;
}

esp_err_t display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(in_bounds(x, y, w, h), ESP_ERR_INVALID_ARG, TAG, "rectangle outside the screen");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int lines_per_chunk = CHUNK_PIXELS / w;
    const int fill = (h < lines_per_chunk ? h : lines_per_chunk) * w;
    for (int i = 0; i < fill; i++) {
        s_buf[i] = color;
    }
    esp_err_t err = ESP_OK;
    for (int row = 0; row < h && err == ESP_OK; row += lines_per_chunk) {
        const int n = (h - row) < lines_per_chunk ? (h - row) : lines_per_chunk;
        err = flush_chunk(x, y + row, w, n);
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t display_fill(uint16_t color)
{
    return display_fill_rect(0, 0, BOARD_LCD_H_RES, BOARD_LCD_V_RES, color);
}

esp_err_t display_draw(int x, int y, int w, int h, const uint16_t *pixels)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(pixels && in_bounds(x, y, w, h), ESP_ERR_INVALID_ARG, TAG, "block outside the screen");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int lines_per_chunk = CHUNK_PIXELS / w;
    esp_err_t err = ESP_OK;
    for (int row = 0; row < h && err == ESP_OK; row += lines_per_chunk) {
        const int n = (h - row) < lines_per_chunk ? (h - row) : lines_per_chunk;
        memcpy(s_buf, pixels + (size_t)row * w, (size_t)n * w * sizeof(uint16_t));
        err = flush_chunk(x, y + row, w, n);
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t display_test_pattern(void)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    const int W = BOARD_LCD_H_RES, H = BOARD_LCD_V_RES;
    const uint16_t bars[] = {
        display_rgb(255, 255, 255), display_rgb(255, 255, 0), display_rgb(0, 255, 255), display_rgb(0, 255, 0),
        display_rgb(255, 0, 255), display_rgb(255, 0, 0), display_rgb(0, 0, 255), display_rgb(0, 0, 0),
    };
    const int nbars = sizeof(bars) / sizeof(bars[0]);
    /* upper 2/3: colour bars white-yellow-cyan-green-magenta-red-blue-black from left to right */
    for (int i = 0; i < nbars; i++) {
        const int x0 = i * W / nbars, x1 = (i + 1) * W / nbars;
        ESP_RETURN_ON_ERROR(display_fill_rect(x0, 0, x1 - x0, H * 2 / 3, bars[i]), TAG, "bars");
    }
    /* lower 1/3: grey ramp, black on the left */
    for (int i = 0; i < 36; i++) {
        const uint8_t v = (uint8_t)(i * 255 / 35);
        ESP_RETURN_ON_ERROR(display_fill_rect(i * W / 36, H * 2 / 3, W / 36, H - H * 2 / 3, display_rgb(v, v, v)), TAG, "ramp");
    }
    /* red mark in the middle of the top edge, green in the middle of the left edge: shows orientation */
    ESP_RETURN_ON_ERROR(display_fill_rect(W / 2 - 15, 30, 30, 30, display_rgb(255, 0, 0)), TAG, "mark");
    ESP_RETURN_ON_ERROR(display_fill_rect(30, H / 2 - 15, 30, 30, display_rgb(0, 255, 0)), TAG, "mark");
    return ESP_OK;
}

esp_err_t display_set_invert(bool invert)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    return esp_lcd_panel_invert_color(s_panel, invert);
}

esp_err_t display_set_mirror(bool mirror_x, bool mirror_y)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    return esp_lcd_panel_mirror(s_panel, mirror_x, mirror_y);
}

esp_err_t display_set_on(bool on)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    return esp_lcd_panel_disp_on_off(s_panel, on);
}
