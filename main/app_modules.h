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

/**
 * Animated face, the default picture on the display ("face" command).
 * app_face_register_commands() also starts it when the display is ready.
 * app_face_start() redraws the whole face; app_face_stop() returns once the
 * face task no longer touches the screen.
 */
void app_face_register_commands(void);
esp_err_t app_face_start(void);
void app_face_stop(void);
bool app_face_is_running(void);

/** Picture upload from the PC ("img" command, used by the web console in docs/). */
void app_image_register_commands(void);
