/**
 * Panel-specific part of the display module. To support another screen
 * (for example the 1.5" one), add a display_panel_xxx.c that implements this
 * function and select it in CMakeLists.txt - nothing else has to change.
 */
#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

/** QSPI clock mode and command format differ per controller; fill the bus-independent parts. */
typedef struct {
    int lcd_cmd_bits;
    int lcd_param_bits;
    int spi_mode;
} display_panel_io_params_t;

void display_panel_get_io_params(display_panel_io_params_t *out);

/** Create the panel on an existing QSPI panel IO. The panel is reset and initialised by the caller. */
esp_err_t display_panel_create(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t *out_panel);

const char *display_panel_name(void);

/** A panel driver may know several init sequences; display_panel_create() uses the selected one. */
int display_panel_variant_count(void);
const char *display_panel_variant_name(int index);
int display_panel_get_variant(void);
void display_panel_set_variant(int index);
