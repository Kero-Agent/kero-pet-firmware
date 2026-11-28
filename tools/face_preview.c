/*
 * Draw the default face on a PC, to look at it without flashing the board.
 *
 *   cc -O2 -Imain tools/face_preview.c main/face_render.c -lm -o /tmp/face_preview
 *   /tmp/face_preview 1.0 0 0 > face.ppm        (arguments: open gaze_x gaze_y)
 */
#include <stdio.h>
#include <stdlib.h>
#include "face_render.h"

int main(int argc, char **argv)
{
    face_pose_t pose = { 1.0f, 0.0f, 0.0f };
    if (argc >= 2) pose.open = strtof(argv[1], NULL);
    if (argc >= 3) pose.gaze_x = strtof(argv[2], NULL);
    if (argc >= 4) pose.gaze_y = strtof(argv[3], NULL);

    static uint16_t px[FACE_SIZE * FACE_SIZE];
    face_render(&pose, 0, 0, FACE_SIZE, FACE_SIZE, px);

    printf("P6\n%d %d\n255\n", FACE_SIZE, FACE_SIZE);
    for (int i = 0; i < FACE_SIZE * FACE_SIZE; i++) {
        const uint16_t v = (uint16_t)((px[i] << 8) | (px[i] >> 8));   /* back to native order */
        const int x = i % FACE_SIZE - FACE_SIZE / 2, y = i / FACE_SIZE - FACE_SIZE / 2;
        const int visible = x * x + y * y <= (FACE_SIZE / 2) * (FACE_SIZE / 2);     /* the screen is round */
        putchar(visible ? (v >> 11) << 3 : 60);
        putchar(visible ? ((v >> 5) & 0x3F) << 2 : 60);
        putchar(visible ? (v & 0x1F) << 3 : 60);
    }
    return 0;
}
