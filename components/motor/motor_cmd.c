#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "motor.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int report(esp_err_t err)
{
    if (err != ESP_OK) {
        printf("failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("ok\n");
    return 0;
}

static int cmd_motor(int argc, char **argv)
{
    if (!motor_is_ready()) {
        printf("motor module is not initialised (power board connected?)\n");
        return 1;
    }
    if (argc == 1) {
        printf("motor A %d %%, motor B %d %%\n", motor_get(MOTOR_A), motor_get(MOTOR_B));
        return 0;
    }
    const char *sub = argv[1];
    if (strcmp(sub, "stop") == 0) {
        return report(motor_stop_all(false));
    }
    if (strcmp(sub, "brake") == 0) {
        return report(motor_stop_all(true));
    }
    if (strcmp(sub, "wdt") == 0 && argc == 3) {
        return report(motor_set_watchdog_ms((uint32_t)strtoul(argv[2], NULL, 0)));
    }
    if (strcmp(sub, "invert") == 0 && argc == 4) {
        motor_set_inverted((argv[2][0] == 'b' || argv[2][0] == 'B') ? MOTOR_B : MOTOR_A, atoi(argv[3]) != 0);
        printf("ok\n");
        return 0;
    }
    if (argc >= 3) {
        const int speed = atoi(argv[2]);
        const bool a = strcmp(sub, "a") == 0 || strcmp(sub, "both") == 0;
        const bool b = strcmp(sub, "b") == 0 || strcmp(sub, "both") == 0;
        if (a || b) {
            esp_err_t err = ESP_OK;
            if (a) {
                err = motor_set(MOTOR_A, speed);
            }
            if (b && err == ESP_OK) {
                err = motor_set(MOTOR_B, speed);
            }
            if (err == ESP_OK && argc >= 4) {
                /* optional run time: stop again after <ms> */
                vTaskDelay(pdMS_TO_TICKS(strtoul(argv[3], NULL, 0)));
                err = motor_stop_all(false);
            }
            return report(err);
        }
    }
    printf("usage:\n"
           "  motor                          show speeds\n"
           "  motor <a|b|both> <-100..100> [ms]   set speed, optionally stop after ms\n"
           "  motor stop | brake             coast / brake both motors\n"
           "  motor wdt <ms>                 auto-stop when no command arrives (0 = off)\n"
           "  motor invert <a|b> <0|1>       swap forward / reverse\n");
    return 1;
}

void motor_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "motor",
        .help = "motors: <a|b|both> <-100..100> [ms] | stop | brake | wdt <ms> | invert <a|b> <0|1>",
        .func = cmd_motor,
    };
    esp_console_cmd_register(&cmd);
}
