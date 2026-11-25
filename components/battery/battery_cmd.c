#include <stdio.h>
#include <stdlib.h>
#include "battery.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int cmd_bat(int argc, char **argv)
{
    if (!battery_is_ready()) {
        printf("battery module is not initialised\n");
        return 1;
    }
    /* bat [count] [interval_ms]: repeat to watch the voltage sag while servos move */
    const int count = argc >= 2 ? atoi(argv[1]) : 1;
    const int interval_ms = argc >= 3 ? atoi(argv[2]) : 200;
    for (int i = 0; i < count; i++) {
        int mv;
        const esp_err_t err = battery_read_mv(&mv);
        if (err != ESP_OK) {
            printf("read failed: %s\n", esp_err_to_name(err));
            return 1;
        }
        if (battery_is_absent(mv)) {
            printf("%d mV - no battery voltage (power switch off, or power board not connected)\n", mv);
        } else {
            printf("%d mV, about %d %%\n", mv, battery_percent_from_mv(mv));
        }
        if (i + 1 < count) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }
    return 0;
}

void battery_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "bat",
        .help = "battery voltage: bat [count] [interval_ms]",
        .func = cmd_bat,
    };
    esp_console_cmd_register(&cmd);
}
