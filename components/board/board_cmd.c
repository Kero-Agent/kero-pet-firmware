#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "board.h"
#include "esp_console.h"

static const char *known_device(uint8_t addr)
{
    switch (addr) {
    case BOARD_ADDR_PCA9685: return "PCA9685 (power board)";
    case BOARD_ADDR_IMU:     return "LSM6DS3TR-C";
    case 0x70:               return "PCA9685 all-call";
    default:                 return "";
    }
}

static int cmd_i2c(int argc, char **argv)
{
    if (!board_i2c_bus()) {
        printf("main I2C bus is not initialised\n");
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "scan") == 0) {
        int found = 0;
        for (uint8_t addr = 0x08; addr < 0x78; addr++) {
            if (board_i2c_probe(addr) == ESP_OK) {
                printf("  0x%02X  %s\n", addr, known_device(addr));
                found++;
            }
        }
        printf("%d device(s) on SDA=%d SCL=%d\n", found, BOARD_I2C_SDA, BOARD_I2C_SCL);
        return 0;
    }
    const bool is_read = argc >= 4 && strcmp(argv[1], "r") == 0;
    const bool is_write = argc >= 5 && strcmp(argv[1], "w") == 0;
    if (is_read || is_write) {
        const uint8_t addr = (uint8_t)strtoul(argv[2], NULL, 0);
        const uint8_t reg = (uint8_t)strtoul(argv[3], NULL, 0);
        i2c_master_dev_handle_t dev = NULL;
        esp_err_t err = board_i2c_add_device(addr, &dev);
        if (err != ESP_OK) {
            printf("cannot attach 0x%02X: %s\n", addr, esp_err_to_name(err));
            return 1;
        }
        if (is_read) {
            uint8_t buf[16];
            size_t n = (argc >= 5) ? strtoul(argv[4], NULL, 0) : 1;
            if (n < 1) {
                n = 1;
            }
            if (n > sizeof(buf)) {
                n = sizeof(buf);
            }
            err = board_i2c_read_reg(dev, reg, buf, n);
            if (err == ESP_OK) {
                printf("0x%02X[0x%02X]:", addr, reg);
                for (size_t i = 0; i < n; i++) {
                    printf(" %02X", buf[i]);
                }
                printf("\n");
            }
        } else {
            err = board_i2c_write_reg8(dev, reg, (uint8_t)strtoul(argv[4], NULL, 0));
            if (err == ESP_OK) {
                printf("ok\n");
            }
        }
        i2c_master_bus_rm_device(dev);
        if (err != ESP_OK) {
            printf("failed: %s\n", esp_err_to_name(err));
            return 1;
        }
        return 0;
    }
    printf("usage: i2c scan | i2c r <addr> <reg> [count] | i2c w <addr> <reg> <value>\n");
    return 1;
}

void board_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "i2c",
        .help = "main I2C bus: scan | r <addr> <reg> [count] | w <addr> <reg> <value>",
        .func = cmd_i2c,
    };
    esp_console_cmd_register(&cmd);
}
