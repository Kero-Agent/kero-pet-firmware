/**
 * The default face: two big eyes with highlights, a small smile and pink cheeks.
 * Pure drawing code with no hardware dependency (it also builds on a PC, see
 * tools/face_preview.c), used by app_face.c.
 */
#pragma once

#include <stdint.h>

#define FACE_SIZE       360     /* the face is drawn for a 360 x 360 screen */

/* Each eye is redrawn inside a fixed box, large enough for every pose. */
#define FACE_EYE_BOX_W  108
#define FACE_EYE_BOX_H  124

typedef struct {
    float open;     /* 1 = eyes open, 0 = closed */
    float gaze_x;   /* where the eyes look, in pixels: -12..12 */
    float gaze_y;   /* -6..6 */
} face_pose_t;

/**
 * Draw the part of the face inside the rectangle (x0, y0, w, h) into `out`,
 * w * h pixels, RGB565 with the high byte first (what display_draw() takes).
 */
void face_render(const face_pose_t *pose, int x0, int y0, int w, int h, uint16_t *out);

/** Top-left corner of the box of one eye (0 = left, 1 = right). */
void face_eye_box(int eye, int *x, int *y);
