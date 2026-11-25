#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kcam.h"
#include "esp_console.h"
#include "esp_timer.h"

static int report(esp_err_t err)
{
    if (err != ESP_OK) {
        printf("failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("ok\n");
    return 0;
}

/* Mean R, G, B of a frame (every 8th pixel) - enough to see that the picture reacts to light. */
static void frame_stats(const kcam_frame_t *f, int *r, int *g, int *b)
{
    uint32_t sr = 0, sg = 0, sb = 0, n = 0;
    for (size_t i = 0; i + 1 < f->len; i += 16) {
        const uint16_t px = (uint16_t)((f->data[i] << 8) | f->data[i + 1]);
        sr += (px >> 11) << 3;
        sg += ((px >> 5) & 0x3F) << 2;
        sb += (px & 0x1F) << 3;
        n++;
    }
    if (n == 0) {
        n = 1;
    }
    *r = (int)(sr / n);
    *g = (int)(sg / n);
    *b = (int)(sb / n);
}

static int cmd_cam(int argc, char **argv)
{
    if (!kcam_is_ready()) {
        printf("camera is not initialised\n");
        return 1;
    }
    if (argc == 1) {
        printf("sensor PID 0x%04X %s\n", kcam_sensor_id(), kcam_sensor_id() == 0x2145 ? "(GC2145)" : "");
        return 0;
    }
    const char *sub = argv[1];
    if (strcmp(sub, "shot") == 0) {
        /* cam shot [count]: grab frames, print size, mean colour and the frame rate */
        const int count = argc >= 3 ? atoi(argv[2]) : 1;
        const int64_t t0 = esp_timer_get_time();
        for (int i = 0; i < count; i++) {
            kcam_frame_t f;
            const esp_err_t err = kcam_capture(&f);
            if (err != ESP_OK) {
                return report(err);
            }
            int r, g, b;
            frame_stats(&f, &r, &g, &b);
            printf("frame %d: %ux%u, %u bytes, mean R %d G %d B %d\n", i, f.width, f.height, (unsigned)f.len, r, g, b);
            kcam_release(&f);
        }
        const int64_t dt = esp_timer_get_time() - t0;
        if (count > 1 && dt > 0) {
            printf("%.1f frames per second\n", count * 1e6 / (double)dt);
        }
        return 0;
    }
    if (strcmp(sub, "flip") == 0 && argc == 4) {
        return report(kcam_set_flip(atoi(argv[2]) != 0, atoi(argv[3]) != 0));
    }
    if (strcmp(sub, "reg") == 0 && argc >= 3) {
        const uint8_t reg = (uint8_t)strtoul(argv[2], NULL, 0);
        if (argc >= 4) {
            return report(kcam_write_reg(reg, (uint8_t)strtoul(argv[3], NULL, 0)));
        }
        uint8_t v;
        const esp_err_t err = kcam_read_reg(reg, &v);
        if (err != ESP_OK) {
            return report(err);
        }
        printf("reg 0x%02X = 0x%02X\n", reg, v);
        return 0;
    }
    printf("usage: cam | cam shot [count] | cam flip <hmirror:0|1> <vflip:0|1> | cam reg <reg> [value]\n");
    return 1;
}

void kcam_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "cam",
        .help = "camera: shot [count] | flip <h> <v> | reg <reg> [value] (no args: sensor id)",
        .func = cmd_cam,
    };
    esp_console_cmd_register(&cmd);
}
