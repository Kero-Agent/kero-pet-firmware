#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "servo.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void print_usage(void)
{
    printf("usage:\n"
           "  servo                         show state\n"
           "  servo <id|all> <deg>          move to an angle (id 0..5)\n"
           "  servo us <id> <us>            raw pulse width, for calibration\n"
           "  servo off [id]                release one servo or all\n"
           "  servo sweep <id> [from to]    sweep slowly there and back once\n"
           "  servo cal <id> <min_us> <max_us> [trim_us]\n"
           "  servo limit <id> <min_deg> <max_deg>\n"
           "  servo invert <id> <0|1>\n");
}

static void print_state(void)
{
    for (uint8_t i = 0; i < SERVO_COUNT; i++) {
        servo_config_t c;
        servo_get_config(i, &c);
        const uint16_t us = servo_get_pulse_us(i);
        printf("  servo %d: ", i);
        if (us) {
            printf("%4d us", us);
        } else {
            printf("released");
        }
        printf("   cal %d..%d us trim %d, limit %.0f..%.0f deg%s\n",
               c.min_us, c.max_us, c.trim_us, c.limit_min_deg, c.limit_max_deg, c.invert ? ", inverted" : "");
    }
}

static int report(esp_err_t err)
{
    if (err != ESP_OK) {
        printf("failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("ok\n");
    return 0;
}

static int cmd_servo(int argc, char **argv)
{
    if (!servo_is_ready()) {
        printf("servo module is not initialised (power board connected?)\n");
        return 1;
    }
    if (argc == 1) {
        print_state();
        return 0;
    }
    const char *sub = argv[1];
    if (strcmp(sub, "off") == 0) {
        return report(argc >= 3 ? servo_release((uint8_t)atoi(argv[2])) : servo_release_all());
    }
    if (strcmp(sub, "us") == 0 && argc == 4) {
        return report(servo_set_pulse_us((uint8_t)atoi(argv[2]), (uint16_t)atoi(argv[3])));
    }
    if (strcmp(sub, "sweep") == 0 && argc >= 3) {
        const uint8_t id = (uint8_t)atoi(argv[2]);
        const float from = argc >= 5 ? strtof(argv[3], NULL) : 30.0f;
        const float to = argc >= 5 ? strtof(argv[4], NULL) : 150.0f;
        const float step = (to >= from) ? 2.0f : -2.0f;
        esp_err_t err = ESP_OK;
        for (float a = from; err == ESP_OK && ((step > 0) ? a <= to : a >= to); a += step) {
            err = servo_set_angle(id, a);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        for (float a = to; err == ESP_OK && ((step > 0) ? a >= from : a <= from); a -= step) {
            err = servo_set_angle(id, a);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        return report(err);
    }
    if (strcmp(sub, "cal") == 0 && argc >= 5) {
        const uint8_t id = (uint8_t)atoi(argv[2]);
        servo_config_t c;
        if (servo_get_config(id, &c) != ESP_OK) {
            return report(ESP_ERR_INVALID_ARG);
        }
        c.min_us = (uint16_t)atoi(argv[3]);
        c.max_us = (uint16_t)atoi(argv[4]);
        if (argc >= 6) {
            c.trim_us = (int16_t)atoi(argv[5]);
        }
        return report(servo_set_config(id, &c));
    }
    if (strcmp(sub, "limit") == 0 && argc == 5) {
        const uint8_t id = (uint8_t)atoi(argv[2]);
        servo_config_t c;
        if (servo_get_config(id, &c) != ESP_OK) {
            return report(ESP_ERR_INVALID_ARG);
        }
        c.limit_min_deg = strtof(argv[3], NULL);
        c.limit_max_deg = strtof(argv[4], NULL);
        return report(servo_set_config(id, &c));
    }
    if (strcmp(sub, "invert") == 0 && argc == 4) {
        const uint8_t id = (uint8_t)atoi(argv[2]);
        servo_config_t c;
        if (servo_get_config(id, &c) != ESP_OK) {
            return report(ESP_ERR_INVALID_ARG);
        }
        c.invert = atoi(argv[3]) != 0;
        return report(servo_set_config(id, &c));
    }
    if (argc == 3) {
        const float deg = strtof(argv[2], NULL);
        if (strcmp(sub, "all") == 0) {
            esp_err_t err = ESP_OK;
            for (uint8_t i = 0; i < SERVO_COUNT && err == ESP_OK; i++) {
                err = servo_set_angle(i, deg);
            }
            return report(err);
        }
        if (sub[0] >= '0' && sub[0] <= '9') {
            return report(servo_set_angle((uint8_t)atoi(sub), deg));
        }
    }
    print_usage();
    return 1;
}

void servo_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "servo",
        .help = "servos: <id|all> <deg> | us <id> <us> | off [id] | sweep <id> | cal | limit | invert (no args: state)",
        .func = cmd_servo,
    };
    esp_console_cmd_register(&cmd);
}
