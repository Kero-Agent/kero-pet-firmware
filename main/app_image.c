/**
 * "img": show a picture sent from the PC (tools/kero-image.html).
 *
 * Protocol of "img recv", all text so it survives any terminal:
 *   PC:    img recv\n
 *   board: READY <chunk>\n          chunk = base64 characters per block
 *   PC:    <chunk> base64 characters     (repeat until the whole picture is sent)
 *   board: .                             one dot per block: send the next one
 *   board: \nDONE\n                 or   \nERR <reason>\n
 * The picture is 360 x 360 pixels, RGB565 with the high byte first, row by row.
 * Blocks are small and acknowledged one by one because the USB console can
 * only buffer 256 bytes. The picture is kept in RAM: it is gone after a reset.
 */
#include <stdio.h>
#include <string.h>
#include "app_modules.h"
#include "display.h"
#include "driver/usb_serial_jtag.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"

#define IMG_W           360
#define IMG_H           360
#define IMG_BYTES       (IMG_W * IMG_H * 2)
#define CHUNK_CHARS     192                         /* base64 characters per block */
#define CHUNK_BYTES     (CHUNK_CHARS / 4 * 3)
#define ROWS_PER_DRAW   8
#define RX_TIMEOUT_MS   3000

_Static_assert(IMG_BYTES % CHUNK_BYTES == 0, "the picture must be a whole number of blocks");

static uint16_t *s_img;
static bool s_img_valid;

static int b64_value(uint8_t c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    return c == '+' ? 62 : (c == '/' ? 63 : -1);
}

/* Read one block of base64 characters from the console, skipping line ends. */
static const char *read_chunk(uint8_t *chars)
{
    int have = 0;
    while (have < CHUNK_CHARS) {
        uint8_t in[CHUNK_CHARS];
        const int n = usb_serial_jtag_read_bytes(in, CHUNK_CHARS - have, pdMS_TO_TICKS(RX_TIMEOUT_MS));
        if (n <= 0) {
            return "timeout";
        }
        for (int i = 0; i < n; i++) {
            if (in[i] == '\r' || in[i] == '\n') {
                continue;
            }
            if (b64_value(in[i]) < 0) {
                return "bad character";
            }
            chars[have++] = in[i];
        }
    }
    return NULL;
}

static int receive(void)
{
    if (!s_img) {
        s_img = heap_caps_malloc(IMG_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_img) {
            printf("ERR out of memory\n");
            return 1;
        }
    }
    app_face_stop();
    s_img_valid = false;
    printf("READY %d\n", CHUNK_CHARS);
    fflush(stdout);

    uint8_t *dst = (uint8_t *)s_img;
    size_t got = 0;
    int drawn_rows = 0;
    while (got < IMG_BYTES) {
        uint8_t chars[CHUNK_CHARS];
        const char *problem = read_chunk(chars);
        if (problem) {
            printf("\nERR %s after %u of %u bytes\n", problem, (unsigned)got, (unsigned)IMG_BYTES);
            return 1;
        }
        for (int i = 0; i < CHUNK_CHARS; i += 4) {
            const uint32_t v = (uint32_t)b64_value(chars[i]) << 18 | (uint32_t)b64_value(chars[i + 1]) << 12 |
                               (uint32_t)b64_value(chars[i + 2]) << 6 | (uint32_t)b64_value(chars[i + 3]);
            dst[got++] = (uint8_t)(v >> 16);
            dst[got++] = (uint8_t)(v >> 8);
            dst[got++] = (uint8_t)v;
        }
        /* show the rows that are complete: the picture builds up from the top */
        const int rows = (int)(got / (IMG_W * 2));
        if (rows - drawn_rows >= ROWS_PER_DRAW || got == IMG_BYTES) {
            const esp_err_t err = display_draw(0, drawn_rows, IMG_W, rows - drawn_rows, s_img + (size_t)drawn_rows * IMG_W);
            if (err != ESP_OK) {
                printf("\nERR display: %s\n", esp_err_to_name(err));
                return 1;
            }
            drawn_rows = rows;
        }
        putchar('.');
        fflush(stdout);
    }
    s_img_valid = true;
    printf("\nDONE\n");
    return 0;
}

static int cmd_img(int argc, char **argv)
{
    if (!display_is_ready()) {
        printf("ERR display is not initialised\n");
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "recv") == 0) {
        return receive();
    }
    if (argc == 2 && strcmp(argv[1], "show") == 0) {
        if (!s_img_valid) {
            printf("no picture received yet\n");
            return 1;
        }
        app_face_stop();
        const esp_err_t err = display_draw(0, 0, IMG_W, IMG_H, s_img);
        printf("%s\n", err == ESP_OK ? "ok" : esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    printf("usage: img recv   receive a picture from tools/kero-image.html and show it\n"
           "       img show   show the last received picture again\n"
           "       (\"face\" brings the animated face back)\n");
    return 1;
}

void app_image_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "img",
        .help = "picture from the PC: recv (used by tools/kero-image.html) | show",
        .func = cmd_img,
    };
    esp_console_cmd_register(&cmd);
}
