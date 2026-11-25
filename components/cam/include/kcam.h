/**
 * GC2145 camera on the 24-pin FPC connector (8-bit DVP, pin map of the ESP32-S3-EYE).
 *
 * The sensor has no JPEG encoder: frames are QVGA (320 x 240) RGB565, high
 * byte first - the same format display_draw() takes.
 * PWDN is tied low and RESET only has a power-on RC, so the sensor cannot be
 * power-cycled from firmware.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t *data;    /* RGB565, 2 bytes per pixel, high byte first */
    size_t len;
    uint16_t width;
    uint16_t height;
    void *handle;           /* internal, needed by kcam_release() */
} kcam_frame_t;

esp_err_t kcam_init(void);

bool kcam_is_ready(void);

/** Sensor product id read over SCCB (0x2145 for the GC2145), 0 when not initialised. */
uint16_t kcam_sensor_id(void);

/** Get the newest frame. Every successful call must be followed by kcam_release(). */
esp_err_t kcam_capture(kcam_frame_t *out);
void kcam_release(kcam_frame_t *frame);

/** Mirror / flip the image in the sensor. */
esp_err_t kcam_set_flip(bool hmirror, bool vflip);

/** Raw sensor register access (debug). */
esp_err_t kcam_read_reg(uint8_t reg, uint8_t *value);
esp_err_t kcam_write_reg(uint8_t reg, uint8_t value);

/** Console command: cam. */
void kcam_register_commands(void);

#ifdef __cplusplus
}
#endif
