/**
 * ST77916 (1.8" 360x360 round screen WLK1804RCTP-14P) in QSPI mode.
 *
 * The init sequence built into the esp_lcd_st77916 component is used. If the
 * screen stays dark or shows wrong colours, paste the sequence from the
 * screen vendor into vendor_init_cmds[] below and set USE_VENDOR_INIT to 1.
 */
#include "display_panel.h"
#include "esp_lcd_st77916.h"
#include "esp_lcd_panel_vendor.h"

#define USE_VENDOR_INIT 0

#if USE_VENDOR_INIT
static const st77916_lcd_init_cmd_t vendor_init_cmds[] = {
    /* {cmd, (uint8_t[]){ data... }, data_bytes, delay_ms}, */
    { 0x11, (uint8_t[]){ 0x00 }, 0, 120 },   /* sleep out */
    { 0x29, (uint8_t[]){ 0x00 }, 0, 20 },    /* display on */
};
#endif

const char *display_panel_name(void)
{
    return "ST77916";
}

void display_panel_get_io_params(display_panel_io_params_t *out)
{
    out->lcd_cmd_bits = 32;     /* QSPI: 0x02 / 0x32 opcode + 24-bit address carrying the command */
    out->lcd_param_bits = 8;
    out->spi_mode = 0;
}

esp_err_t display_panel_create(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t *out_panel)
{
    st77916_vendor_config_t vendor = {
#if USE_VENDOR_INIT
        .init_cmds = vendor_init_cmds,
        .init_cmds_size = sizeof(vendor_init_cmds) / sizeof(vendor_init_cmds[0]),
#endif
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t cfg = {
        .reset_gpio_num = -1,       /* not wired: the driver sends a software reset */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    return esp_lcd_new_panel_st77916(io, &cfg, out_panel);
}
