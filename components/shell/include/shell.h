/**
 * Debug shell on the USB Serial/JTAG port (the Type-C socket of the power board).
 *
 * Usage:
 *   shell_init();                  // creates the console, registers help / sys / log / reboot
 *   xxx_register_commands();       // every module adds its own commands
 *   shell_start();                 // prompt appears
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t shell_init(void);
esp_err_t shell_start(void);

#ifdef __cplusplus
}
#endif
