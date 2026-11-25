/**
 * Battery voltage on GPIO1 (ADC1 channel 0) through a 100k / 100k divider with 100 nF.
 * The divider hangs on the switched battery rail of the power board, so the
 * reading is near 0 V when the power switch is off and the robot runs from USB.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t battery_init(void);

bool battery_is_ready(void);

/** Battery voltage in millivolts, averaged over 32 samples. */
esp_err_t battery_read_mv(int *out_mv);

/** Rough state of charge for a single Li-ion cell at rest, 0..100. */
int battery_percent_from_mv(int mv);

/** True when the reading is too low to be a battery: switch off or power board missing. */
bool battery_is_absent(int mv);

/** Console command: bat. */
void battery_register_commands(void);

#ifdef __cplusplus
}
#endif
