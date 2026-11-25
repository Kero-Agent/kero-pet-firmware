#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "display.h"
#include "esp_console.h"

static int report(esp_err_t err)
{
    if (err != ESP_OK) {
        printf("failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("ok\n");
    return 0;
}

static bool parse_color(const char *s, uint16_t *out)
{
    static const struct { const char *name; uint8_t r, g, b; } named[] = {
        { "black", 0, 0, 0 }, { "white", 255, 255, 255 }, { "red", 255, 0, 0 }, { "green", 0, 255, 0 },
        { "blue", 0, 0, 255 }, { "yellow", 255, 255, 0 }, { "cyan", 0, 255, 255 }, { "magenta", 255, 0, 255 },
        { "grey", 128, 128, 128 },
    };
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
        if (strcmp(s, named[i].name) == 0) {
            *out = display_rgb(named[i].r, named[i].g, named[i].b);
            return true;
        }
    }
    if (strlen(s) == 6) {       /* rrggbb in hex */
        char *end;
        const unsigned long v = strtoul(s, &end, 16);
        if (*end == '\0') {
            *out = display_rgb((uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v);
            return true;
        }
    }
    return false;
}

static int cmd_lcd(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "bl") == 0) {
        /* the backlight works without the panel */
        return report(display_set_backlight(atoi(argv[2])));
    }
    if (argc == 1) {
        printf("panel %s, %dx%d, backlight %d %%\n", display_is_ready() ? "ready" : "NOT initialised",
               display_width(), display_height(), display_get_backlight());
        return 0;
    }
    if (!display_is_ready()) {
        printf("display is not initialised (only 'lcd bl <0..100>' works)\n");
        return 1;
    }
    const char *sub = argv[1];
    if (strcmp(sub, "test") == 0) {
        return report(display_test_pattern());
    }
    if (strcmp(sub, "fill") == 0 && argc == 3) {
        uint16_t c;
        if (!parse_color(argv[2], &c)) {
            printf("colour: black white red green blue yellow cyan magenta grey, or rrggbb\n");
            return 1;
        }
        return report(display_fill(c));
    }
    if (strcmp(sub, "rect") == 0 && argc == 7) {
        uint16_t c;
        if (!parse_color(argv[6], &c)) {
            printf("bad colour\n");
            return 1;
        }
        return report(display_fill_rect(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), c));
    }
    if (strcmp(sub, "invert") == 0 && argc == 3) {
        return report(display_set_invert(atoi(argv[2]) != 0));
    }
    if (strcmp(sub, "mirror") == 0 && argc == 4) {
        return report(display_set_mirror(atoi(argv[2]) != 0, atoi(argv[3]) != 0));
    }
    if (strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0) {
        return report(display_set_on(strcmp(sub, "on") == 0));
    }
    printf("usage:\n"
           "  lcd                         state\n"
           "  lcd bl <0..100>             backlight\n"
           "  lcd test                    colour bars, grey ramp, orientation marks\n"
           "  lcd fill <colour|rrggbb>    whole screen\n"
           "  lcd rect <x> <y> <w> <h> <colour>\n"
           "  lcd invert <0|1>            colour inversion\n"
           "  lcd mirror <x:0|1> <y:0|1>\n"
           "  lcd on | off                panel output\n");
    return 1;
}

void display_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "lcd",
        .help = "display: bl <0..100> | test | fill <colour> | rect | invert <0|1> | mirror <x> <y> | on | off",
        .func = cmd_lcd,
    };
    esp_console_cmd_register(&cmd);
}
