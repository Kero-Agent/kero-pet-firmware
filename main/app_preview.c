/**
 * "preview": show the camera picture on the display.
 * Lives in main because it is the only code that knows both modules.
 */
#include <stdio.h>
#include <stdlib.h>
#include "app_modules.h"
#include "kcam.h"
#include "display.h"
#include "esp_console.h"
#include "esp_timer.h"

static int cmd_preview(int argc, char **argv)
{
    if (!kcam_is_ready() || !display_is_ready()) {
        printf("needs camera (%s) and display (%s)\n", kcam_is_ready() ? "ok" : "not ready", display_is_ready() ? "ok" : "not ready");
        return 1;
    }
    /* preview [seconds], default 10 */
    const int seconds = argc >= 2 ? atoi(argv[1]) : 10;
    const int64_t t_end = esp_timer_get_time() + (int64_t)seconds * 1000000;
    const int64_t t0 = esp_timer_get_time();
    int frames = 0;
    const bool face_was_running = app_face_is_running();
    app_face_stop();
    display_fill(0);
    esp_err_t err = ESP_OK;
    while (esp_timer_get_time() < t_end) {
        kcam_frame_t f;
        err = kcam_capture(&f);
        if (err != ESP_OK) {
            printf("capture failed: %s\n", esp_err_to_name(err));
            break;
        }
        /* centre the 320 x 240 picture on the 360 x 360 screen */
        const int x = (display_width() - f.width) / 2;
        const int y = (display_height() - f.height) / 2;
        err = display_draw(x, y, f.width, f.height, (const uint16_t *)f.data);
        kcam_release(&f);
        if (err != ESP_OK) {
            printf("draw failed: %s\n", esp_err_to_name(err));
            break;
        }
        frames++;
    }
    if (face_was_running) {
        app_face_start();
    }
    if (err != ESP_OK) {
        return 1;
    }
    printf("%d frames, %.1f per second\n", frames, frames * 1e6 / (double)(esp_timer_get_time() - t0));
    return 0;
}

void app_preview_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "preview",
        .help = "preview [seconds]: camera picture on the display",
        .func = cmd_preview,
    };
    esp_console_cmd_register(&cmd);
}
