#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pca9685.h"
#include "esp_console.h"

static int cmd_pca(int argc, char **argv)
{
    if (!pca9685_is_ready()) {
        printf("pca9685 is not initialised (power board connected?)\n");
        return 1;
    }
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (argc == 1 || strcmp(argv[1], "dump") == 0) {
        printf("frequency %.2f Hz\n", pca9685_get_frequency());
        for (uint8_t ch = 0; ch < PCA9685_CHANNELS; ch++) {
            uint16_t on, off;
            if (pca9685_read_channel(ch, &on, &off) != ESP_OK) {
                printf("  ch%-2d read error\n", ch);
                continue;
            }
            if (off & 0x1000) {
                printf("  ch%-2d off\n", ch);
            } else if (on & 0x1000) {
                printf("  ch%-2d on\n", ch);
            } else {
                printf("  ch%-2d duty %4d / 4096\n", ch, (off - on) & 0xFFF);
            }
        }
        return 0;
    } else if (argc == 3 && strcmp(argv[1], "freq") == 0) {
        err = pca9685_set_frequency((uint16_t)atoi(argv[2]));
    } else if (argc == 4 && strcmp(argv[1], "duty") == 0) {
        err = pca9685_set_duty((uint8_t)atoi(argv[2]), (uint16_t)atoi(argv[3]));
    } else if (argc == 4 && strcmp(argv[1], "us") == 0) {
        err = pca9685_set_pulse_us((uint8_t)atoi(argv[2]), (uint16_t)atoi(argv[3]));
    } else if (argc == 4 && strcmp(argv[1], "level") == 0) {
        err = pca9685_set_level((uint8_t)atoi(argv[2]), atoi(argv[3]) != 0);
    } else if (argc == 3 && strcmp(argv[1], "osc") == 0) {
        pca9685_set_oscillator_hz((uint32_t)strtoul(argv[2], NULL, 0));
        err = pca9685_set_frequency((uint16_t)(pca9685_get_frequency() + 0.5f));
    } else if (argc == 2 && strcmp(argv[1], "off") == 0) {
        err = pca9685_all_off();
    } else {
        printf("usage: pca [dump] | freq <hz> | duty <ch> <0..4096> | us <ch> <us> | level <ch> <0|1> | osc <hz> | off\n");
        return 1;
    }
    if (err != ESP_OK) {
        printf("failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("ok\n");
    return 0;
}

void pca9685_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "pca",
        .help = "PCA9685 raw access: dump | freq <hz> | duty <ch> <0..4096> | us <ch> <us> | level <ch> <0|1> | osc <hz> | off",
        .func = cmd_pca,
    };
    esp_console_cmd_register(&cmd);
}
