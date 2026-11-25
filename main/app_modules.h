/**
 * Module table: every hardware module is brought up independently, so one
 * missing or broken part (no power board, no screen, no camera) never stops
 * the others. The console commands "status" and "init" work on this table.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    const char *name;
    bool enabled;                       /* from menuconfig -> Kero robot */
    esp_err_t (*init)(void);
    void (*register_commands)(void);
    const char *needs;                  /* name of a module that must be up first, or NULL */
    bool tried;
    esp_err_t result;
} app_module_t;

/** Initialise all enabled modules in order and log a summary. */
void app_modules_init_all(void);

/** Register the commands of every module plus "status" and "init". Call after shell_init(). */
void app_modules_register_commands(void);

/** Print the module table. */
void app_modules_print_status(void);

/** Camera to LCD preview command ("preview"), glue between two otherwise independent modules. */
void app_preview_register_commands(void);
