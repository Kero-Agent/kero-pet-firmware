/**
 * The default picture on the display: an animated face that blinks and looks
 * around. Runs in its own task; anything else that wants the screen (lcd test,
 * preview, an uploaded image) calls app_face_stop() first.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app_modules.h"
#include "face_render.h"
#include "display.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "face";

#define FRAME_MS        30
#define STRIP_LINES     40      /* the whole face is drawn in strips of this height */
#define BUF_PIXELS      (FACE_SIZE * STRIP_LINES)   /* also holds one eye box */

_Static_assert(FACE_EYE_BOX_W * FACE_EYE_BOX_H <= BUF_PIXELS, "eye box does not fit the buffer");

static SemaphoreHandle_t s_lock;    /* held while a frame is drawn */
static TaskHandle_t s_task;
static uint16_t *s_buf;
static bool s_run;                  /* protected by s_lock */
static bool s_redraw_all;           /* protected by s_lock */

static float smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

static void draw_all(const face_pose_t *pose)
{
    for (int y = 0; y < FACE_SIZE; y += STRIP_LINES) {
        face_render(pose, 0, y, FACE_SIZE, STRIP_LINES, s_buf);
        if (display_draw(0, y, FACE_SIZE, STRIP_LINES, s_buf) != ESP_OK) {
            return;
        }
    }
}

static void draw_eyes(const face_pose_t *pose)
{
    for (int eye = 0; eye < 2; eye++) {
        int x, y;
        face_eye_box(eye, &x, &y);
        face_render(pose, x, y, FACE_EYE_BOX_W, FACE_EYE_BOX_H, s_buf);
        if (display_draw(x, y, FACE_EYE_BOX_W, FACE_EYE_BOX_H, s_buf) != ESP_OK) {
            return;
        }
    }
}

static void face_task(void *arg)
{
    face_pose_t pose = { .open = 1.0f };
    face_pose_t drawn = pose;
    float target_x = 0, target_y = 0;
    int64_t now = esp_timer_get_time() / 1000;
    int64_t blink_at = now + 1500, blink_t0 = -1, gaze_at = now + 2500;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
        now = esp_timer_get_time() / 1000;

        /* blink: close in 80 ms, stay closed 50 ms, open in 130 ms */
        if (blink_t0 < 0 && now >= blink_at) {
            blink_t0 = now;
        }
        if (blink_t0 >= 0) {
            const int dt = (int)(now - blink_t0);
            if (dt < 80) {
                pose.open = 1.0f - smooth(dt / 80.0f);
            } else if (dt < 130) {
                pose.open = 0.0f;
            } else if (dt < 260) {
                pose.open = smooth((dt - 130) / 130.0f);
            } else {
                pose.open = 1.0f;
                blink_t0 = -1;
                /* now and then blink twice in a row, otherwise wait 1.8 .. 5.3 s */
                blink_at = now + ((esp_random() % 5 == 0) ? 150 : 1800 + esp_random() % 3500);
            }
        }

        /* gaze: pick a new place to look at every few seconds, glide there */
        if (now >= gaze_at) {
            if (esp_random() % 3 == 0) {
                target_x = target_y = 0;
            } else {
                target_x = (float)(esp_random() % 25) - 12.0f;
                target_y = (float)(esp_random() % 13) - 6.0f;
            }
            gaze_at = now + 1500 + esp_random() % 3000;
        }
        pose.gaze_x += (target_x - pose.gaze_x) * 0.3f;
        pose.gaze_y += (target_y - pose.gaze_y) * 0.3f;
        if (fabsf(target_x - pose.gaze_x) < 0.3f) {
            pose.gaze_x = target_x;
        }
        if (fabsf(target_y - pose.gaze_y) < 0.3f) {
            pose.gaze_y = target_y;
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_run) {
            if (s_redraw_all) {
                s_redraw_all = false;
                draw_all(&pose);
                drawn = pose;
            } else if (memcmp(&pose, &drawn, sizeof(pose)) != 0) {
                draw_eyes(&pose);
                drawn = pose;
            }
        }
        xSemaphoreGive(s_lock);
    }
}

esp_err_t app_face_start(void)
{
    if (!display_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    if (!s_buf) {
        s_buf = heap_caps_malloc(BUF_PIXELS * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_lock || !s_buf) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_redraw_all = true;
    s_run = true;
    xSemaphoreGive(s_lock);
    if (!s_task && xTaskCreate(face_task, "face", 4096, NULL, 2, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_face_stop(void)
{
    if (s_lock) {
        /* taking the lock waits for a frame that is being drawn right now */
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_run = false;
        xSemaphoreGive(s_lock);
    }
}

bool app_face_is_running(void)
{
    return s_run;
}

static int cmd_face(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "off") == 0) {
        app_face_stop();
        if (display_is_ready()) {
            display_fill(0);
        }
        printf("ok\n");
        return 0;
    }
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "on") == 0)) {
        const esp_err_t err = app_face_start();
        if (err != ESP_OK) {
            printf("failed: %s%s\n", esp_err_to_name(err), display_is_ready() ? "" : " (display is not initialised)");
            return 1;
        }
        printf("ok\n");
        return 0;
    }
    printf("usage: face [on] | face off\n");
    return 1;
}

void app_face_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "face",
        .help = "face [on]: show the animated face | face off: stop it and clear the screen",
        .func = cmd_face,
    };
    esp_console_cmd_register(&cmd);
    /* the lcd drawing commands take the screen away from the face */
    display_set_takeover_hook(app_face_stop);
    if (display_is_ready() && app_face_start() != ESP_OK) {
        ESP_LOGW(TAG, "could not start the face");
    }
}
