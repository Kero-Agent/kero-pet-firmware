#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "imu.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int cmd_imu(int argc, char **argv)
{
    if (!imu_is_ready()) {
        printf("imu module is not initialised\n");
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "cal") == 0) {
        printf("keep the robot still...\n");
        const esp_err_t err = imu_calibrate_gyro();
        printf("%s\n", err == ESP_OK ? "ok" : esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    /* imu [count] [interval_ms] */
    const int count = argc >= 2 ? atoi(argv[1]) : 1;
    const int interval_ms = argc >= 3 ? atoi(argv[2]) : 100;
    if (count < 1) {
        printf("usage: imu [count] [interval_ms] | imu cal\n");
        return 1;
    }
    printf("   ax      ay      az   (g) |    gx       gy       gz  (dps) |  temp\n");
    for (int i = 0; i < count; i++) {
        imu_sample_t s;
        const esp_err_t err = imu_read(&s);
        if (err != ESP_OK) {
            printf("read failed: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("%7.3f %7.3f %7.3f     | %8.2f %8.2f %8.2f       | %5.1f\n",
               s.accel_g[0], s.accel_g[1], s.accel_g[2],
               s.gyro_dps[0], s.gyro_dps[1], s.gyro_dps[2], s.temperature_c);
        if (i + 1 < count) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }
    return 0;
}

void imu_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "imu",
        .help = "IMU: imu [count] [interval_ms] | imu cal (gyro zero, keep still)",
        .func = cmd_imu,
    };
    esp_console_cmd_register(&cmd);
}
