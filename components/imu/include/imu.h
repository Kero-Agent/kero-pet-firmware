/**
 * LSM6DS3TR-C accelerometer + gyroscope on the main I2C bus (0x6A).
 * INT1 / INT2 are not wired on the board, so the sensor is polled.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float accel_g[3];       /* X, Y, Z in g */
    float gyro_dps[3];      /* X, Y, Z in degrees per second */
    float temperature_c;
} imu_sample_t;

/** Check WHO_AM_I and start both sensors at 104 Hz, +-4 g, +-1000 dps. */
esp_err_t imu_init(void);

bool imu_is_ready(void);

/** Latest sample (accelerometer, gyroscope and temperature in one bus read). */
esp_err_t imu_read(imu_sample_t *out);

/**
 * Average the gyroscope for about one second and store the result as zero offset.
 * The robot must stand still while this runs.
 */
esp_err_t imu_calibrate_gyro(void);

/** Console command: imu. */
void imu_register_commands(void);

#ifdef __cplusplus
}
#endif
