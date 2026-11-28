#include <math.h>
#include "face_render.h"

#define EYE_CENTER_Y    158.0f
#define EYE_OFFSET_X    62.0f       /* eye centres are at 180 -/+ this */
#define EYE_RX          36.0f
#define EYE_RY          50.0f
#define EYE_RY_CLOSED   4.0f

#define MOUTH_X         180.0f
#define MOUTH_Y         224.0f
#define MOUTH_R         18.0f
#define MOUTH_THICK     5.0f

#define CHEEK_OFFSET_X  98.0f
#define CHEEK_Y         236.0f
#define CHEEK_RX        26.0f
#define CHEEK_RY        15.0f

typedef struct {
    float r, g, b;
} rgb_t;

static const rgb_t BACKGROUND = { 10, 14, 32 };
static const rgb_t EYE = { 170, 245, 255 };
static const rgb_t HIGHLIGHT = { 255, 255, 255 };
static const rgb_t CHEEK = { 255, 110, 150 };

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* Coverage (0..1) of a pixel by an ellipse, with a one pixel soft edge. */
static float ellipse(float x, float y, float cx, float cy, float rx, float ry)
{
    const float dx = (x - cx) / rx, dy = (y - cy) / ry;
    const float r = sqrtf(dx * dx + dy * dy);
    return clamp01(0.5f - (r - 1.0f) * (rx < ry ? rx : ry));
}

static inline void blend(rgb_t *c, const rgb_t *over, float a)
{
    c->r += (over->r - c->r) * a;
    c->g += (over->g - c->g) * a;
    c->b += (over->b - c->b) * a;
}

static void shade(const face_pose_t *p, float x, float y, rgb_t *c)
{
    *c = BACKGROUND;

    /* cheeks: soft pink blobs, strongest in the middle */
    for (int side = -1; side <= 1; side += 2) {
        const float dx = (x - (180.0f + side * CHEEK_OFFSET_X)) / CHEEK_RX, dy = (y - CHEEK_Y) / CHEEK_RY;
        const float r2 = dx * dx + dy * dy;
        if (r2 < 1.0f) {
            blend(c, &CHEEK, clamp01((1.0f - sqrtf(r2)) * 2.5f) * 0.6f);
        }
    }

    /* mouth: the lower part of a ring */
    {
        const float dx = x - MOUTH_X, dy = y - MOUTH_Y;
        const float d = fabsf(sqrtf(dx * dx + dy * dy) - MOUTH_R) - MOUTH_THICK / 2;
        blend(c, &EYE, clamp01(0.5f - d) * clamp01(y - (MOUTH_Y + 5.0f)));
    }

    /* eyes: squashed towards a line when they close */
    const float ry = EYE_RY_CLOSED + (EYE_RY - EYE_RY_CLOSED) * p->open;
    const float rx = EYE_RX + 4.0f * (1.0f - p->open);
    const float cy = EYE_CENTER_Y + p->gaze_y;
    for (int side = -1; side <= 1; side += 2) {
        const float cx = 180.0f + side * EYE_OFFSET_X + p->gaze_x;
        const float a = ellipse(x, y, cx, cy, rx, ry);
        if (a <= 0.0f) {
            continue;
        }
        blend(c, &EYE, a);
        /* two highlights, they shrink away while the eye closes */
        const float h = clamp01((p->open - 0.4f) / 0.6f);
        if (h > 0.0f) {
            blend(c, &HIGHLIGHT, a * ellipse(x, y, cx - 11.0f, cy - 17.0f * p->open, 9.0f * h, 9.0f * h));
            blend(c, &HIGHLIGHT, a * ellipse(x, y, cx + 12.0f, cy + 17.0f * p->open, 4.0f * h, 4.0f * h) * 0.8f);
        }
    }
}

void face_render(const face_pose_t *pose, int x0, int y0, int w, int h, uint16_t *out)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            rgb_t c;
            shade(pose, (float)(x0 + i), (float)(y0 + j), &c);
            const unsigned r = (unsigned)(c.r + 0.5f), g = (unsigned)(c.g + 0.5f), b = (unsigned)(c.b + 0.5f);
            const uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            *out++ = (uint16_t)((v << 8) | (v >> 8));
        }
    }
}

void face_eye_box(int eye, int *x, int *y)
{
    const float cx = 180.0f + (eye == 0 ? -EYE_OFFSET_X : EYE_OFFSET_X);
    *x = (int)cx - FACE_EYE_BOX_W / 2;
    *y = (int)EYE_CENTER_Y - FACE_EYE_BOX_H / 2;
}
