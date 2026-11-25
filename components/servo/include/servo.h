/**
 * Six hobby servos on PCA9685 channels 0..5 (connectors J4..J9 on the power board).
 * Servos start released (no pulses) and only move when commanded.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SERVO_COUNT 6

typedef struct {
    uint16_t min_us;        /* pulse at 0 degrees */
    uint16_t max_us;        /* pulse at range_deg */
    float    range_deg;     /* mechanical range, normally 180 */
    float    limit_min_deg; /* software end stops, protect the mechanism */
    float    limit_max_deg;
    int16_t  trim_us;       /* added to every pulse, for centring */
    bool     invert;        /* mirror the direction */
} servo_config_t;

/** Needs pca9685_init() first (50 Hz). All servos released. */
esp_err_t servo_init(void);

bool servo_is_ready(void);

/** Move to an angle; clamped to the software end stops. */
esp_err_t servo_set_angle(uint8_t id, float deg);

/** Raw pulse width, clamped to 400..2600 us. Bypasses angle limits - for calibration. */
esp_err_t servo_set_pulse_us(uint8_t id, uint16_t pulse_us);

/** Stop sending pulses: the servo goes limp. */
esp_err_t servo_release(uint8_t id);
esp_err_t servo_release_all(void);

/** Last commanded pulse in us, 0 when released. */
uint16_t servo_get_pulse_us(uint8_t id);

/** Per-servo calibration. Changes apply to the next command. */
esp_err_t servo_get_config(uint8_t id, servo_config_t *out);
esp_err_t servo_set_config(uint8_t id, const servo_config_t *cfg);

/** Console command: servo. */
void servo_register_commands(void);

#ifdef __cplusplus
}
#endif
