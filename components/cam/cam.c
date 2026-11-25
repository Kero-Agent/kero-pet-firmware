#include "sdkconfig.h"
#include "kcam.h"
#include "board.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "cam";

static bool s_ready;
static uint16_t s_pid;

bool kcam_is_ready(void)
{
    return s_ready;
}

uint16_t kcam_sensor_id(void)
{
    return s_pid;
}

esp_err_t kcam_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    const camera_config_t cfg = {
        .pin_pwdn = -1,             /* tied low on the board */
        .pin_reset = -1,            /* RC only */
        .pin_xclk = BOARD_CAM_XCLK,
        .pin_sccb_sda = BOARD_CAM_SDA,
        .pin_sccb_scl = BOARD_CAM_SCL,
        .pin_d7 = BOARD_CAM_D7,
        .pin_d6 = BOARD_CAM_D6,
        .pin_d5 = BOARD_CAM_D5,
        .pin_d4 = BOARD_CAM_D4,
        .pin_d3 = BOARD_CAM_D3,
        .pin_d2 = BOARD_CAM_D2,
        .pin_d1 = BOARD_CAM_D1,
        .pin_d0 = BOARD_CAM_D0,
        .pin_vsync = BOARD_CAM_VSYNC,
        .pin_href = BOARD_CAM_HREF,
        .pin_pclk = BOARD_CAM_PCLK,
        .xclk_freq_hz = CONFIG_KERO_CAM_XCLK_MHZ * 1000 * 1000,
        .ledc_timer = BOARD_LEDC_TIMER_CAM,
        .ledc_channel = BOARD_LEDC_CH_CAM,
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12,         /* unused: the GC2145 has no JPEG output */
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
        .sccb_i2c_port = BOARD_CAM_SCCB_PORT,   /* the library opens its own bus on the port chosen by CONFIG_SCCB_HARDWARE_I2C_PORT1 */
    };
    const esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        /* esp_camera_init() has already cleaned up after itself */
        ESP_LOGE(TAG, "esp_camera_init failed: %s - check the FPC direction and the 2.8 V / 1.8 V rails", esp_err_to_name(err));
        return err;
    }
    sensor_t *s = esp_camera_sensor_get();
    s_pid = s ? s->id.PID : 0;
    s_ready = true;
    ESP_LOGI(TAG, "sensor PID 0x%04X, QVGA RGB565, XCLK %d MHz", s_pid, CONFIG_KERO_CAM_XCLK_MHZ);
    return ESP_OK;
}

esp_err_t kcam_capture(kcam_frame_t *out)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null");
    camera_fb_t *fb = esp_camera_fb_get();
    ESP_RETURN_ON_FALSE(fb, ESP_ERR_TIMEOUT, TAG, "no frame (VSYNC / PCLK / data lines?)");
    out->data = fb->buf;
    out->len = fb->len;
    out->width = (uint16_t)fb->width;
    out->height = (uint16_t)fb->height;
    out->handle = fb;
    return ESP_OK;
}

void kcam_release(kcam_frame_t *frame)
{
    if (frame && frame->handle) {
        esp_camera_fb_return((camera_fb_t *)frame->handle);
        frame->handle = NULL;
    }
}

esp_err_t kcam_set_flip(bool hmirror, bool vflip)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    sensor_t *s = esp_camera_sensor_get();
    ESP_RETURN_ON_FALSE(s && s->set_hmirror && s->set_vflip, ESP_ERR_NOT_SUPPORTED, TAG, "sensor has no flip control");
    ESP_RETURN_ON_FALSE(s->set_hmirror(s, hmirror) == 0, ESP_FAIL, TAG, "hmirror");
    ESP_RETURN_ON_FALSE(s->set_vflip(s, vflip) == 0, ESP_FAIL, TAG, "vflip");
    return ESP_OK;
}

esp_err_t kcam_read_reg(uint8_t reg, uint8_t *value)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    sensor_t *s = esp_camera_sensor_get();
    ESP_RETURN_ON_FALSE(s && s->get_reg, ESP_ERR_NOT_SUPPORTED, TAG, "no register access");
    const int v = s->get_reg(s, reg, 0xFF);
    ESP_RETURN_ON_FALSE(v >= 0, ESP_FAIL, TAG, "read failed");
    *value = (uint8_t)v;
    return ESP_OK;
}

esp_err_t kcam_write_reg(uint8_t reg, uint8_t value)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    sensor_t *s = esp_camera_sensor_get();
    ESP_RETURN_ON_FALSE(s && s->set_reg, ESP_ERR_NOT_SUPPORTED, TAG, "no register access");
    ESP_RETURN_ON_FALSE(s->set_reg(s, reg, 0xFF, value) == 0, ESP_FAIL, TAG, "write failed");
    return ESP_OK;
}
