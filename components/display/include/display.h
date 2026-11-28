/**
 * 360 x 360 QSPI display on the 14-pin header plus its backlight (GPIO43).
 *
 * The panel driver is selected in display_panel.c (ST77916, the 1.8" screen).
 * Neither the panel reset nor the touch reset is wired to a GPIO, so the panel
 * is reset by command. Touch is not handled here yet: find the controller with
 * "i2c scan" first.
 *
 * Pixel format everywhere in this API: RGB565 in bus byte order (high byte
 * first), which is also what the camera delivers. Use display_rgb() to build
 * a colour value.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the backlight and the panel. Can be called again (console: "init display")
 * to send the reset and init sequence once more, e.g. when the screen was
 * plugged in after power-up - the panel cannot be read, so this never fails
 * just because no screen is connected.
 */
esp_err_t display_init(void);

bool display_is_ready(void);

int display_width(void);
int display_height(void);

/** RGB888 to the 16-bit value this API expects. */
uint16_t display_rgb(uint8_t r, uint8_t g, uint8_t b);

/** Backlight brightness, 0..100. Works even when the panel itself failed to start. */
esp_err_t display_set_backlight(int percent);
int display_get_backlight(void);

/** Fill the whole screen. Blocks until the data is on the panel. */
esp_err_t display_fill(uint16_t color);

/** Fill a rectangle. */
esp_err_t display_fill_rect(int x, int y, int w, int h, uint16_t color);

/**
 * Copy a block of pixels to the screen. The source may live anywhere (PSRAM is
 * fine); it is sent through an internal DMA buffer. Blocks until done.
 */
esp_err_t display_draw(int x, int y, int w, int h, const uint16_t *pixels);

/** Colour bars, grey ramp and a border: shows colour order, inversion and orientation. */
esp_err_t display_test_pattern(void);

esp_err_t display_set_invert(bool invert);
esp_err_t display_set_mirror(bool mirror_x, bool mirror_y);
esp_err_t display_set_on(bool on);

/**
 * Something that animates the screen on its own (the face in main) registers a
 * hook here; the lcd drawing commands call it first so the two do not fight.
 */
void display_set_takeover_hook(void (*hook)(void));
void display_takeover(void);

/** Console command: lcd. */
void display_register_commands(void);

#ifdef __cplusplus
}
#endif
