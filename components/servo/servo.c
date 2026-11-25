#include "servo.h"
#include "pca9685.h"
#include "board.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "servo";

#define PULSE_HARD_MIN_US   400
#define PULSE_HARD_MAX_US   2600

static const servo_config_t DEFAULT_CONFIG = {
    .min_us = 500,
    .max_us = 2500,
    .range_deg = 180.0f,
    .limit_min_deg = 0.0f,
    .limit_max_deg = 180.0f,
    .trim_us = 0,
    .invert = false,
};

static servo_config_t s_cfg[SERVO_COUNT];
static uint16_t s_pulse[SERVO_COUNT];
static bool s_ready;

bool servo_is_ready(void)
{
    return s_ready;
}

esp_err_t servo_init(void)
{
    ESP_RETURN_ON_FALSE(pca9685_is_ready(), ESP_ERR_INVALID_STATE, TAG, "pca9685 is not ready");
    const float f = pca9685_get_frequency();
    if (f < 45.0f || f > 60.0f) {
        ESP_LOGW(TAG, "PWM frequency is %.1f Hz, servos expect about 50 Hz", f);
    }
    for (int i = 0; i < SERVO_COUNT; i++) {
        s_cfg[i] = DEFAULT_CONFIG;
        s_pulse[i] = 0;
        ESP_RETURN_ON_ERROR(pca9685_set_duty(BOARD_PCA_CH_SERVO0 + i, 0), TAG, "release %d failed", i);
    }
    s_ready = true;
    ESP_LOGI(TAG, "%d servos ready, all released", SERVO_COUNT);
    return ESP_OK;
}

esp_err_t servo_set_pulse_us(uint8_t id, uint16_t pulse_us)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(id < SERVO_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad servo id %d", id);
    if (pulse_us < PULSE_HARD_MIN_US) {
        pulse_us = PULSE_HARD_MIN_US;
    }
    if (pulse_us > PULSE_HARD_MAX_US) {
        pulse_us = PULSE_HARD_MAX_US;
    }
    ESP_RETURN_ON_ERROR(pca9685_set_pulse_us(BOARD_PCA_CH_SERVO0 + id, pulse_us), TAG, "write failed");
    s_pulse[id] = pulse_us;
    return ESP_OK;
}

esp_err_t servo_set_angle(uint8_t id, float deg)
{
    ESP_RETURN_ON_FALSE(id < SERVO_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad servo id %d", id);
    const servo_config_t *c = &s_cfg[id];
    if (deg < c->limit_min_deg) {
        deg = c->limit_min_deg;
    }
    if (deg > c->limit_max_deg) {
        deg = c->limit_max_deg;
    }
    if (c->invert) {
        deg = c->range_deg - deg;
    }
    const float us = c->min_us + (c->max_us - c->min_us) * deg / c->range_deg + c->trim_us;
    return servo_set_pulse_us(id, (uint16_t)(us + 0.5f));
}

esp_err_t servo_release(uint8_t id)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(id < SERVO_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad servo id %d", id);
    ESP_RETURN_ON_ERROR(pca9685_set_duty(BOARD_PCA_CH_SERVO0 + id, 0), TAG, "write failed");
    s_pulse[id] = 0;
    return ESP_OK;
}

esp_err_t servo_release_all(void)
{
    esp_err_t ret = ESP_OK;
    for (uint8_t i = 0; i < SERVO_COUNT; i++) {
        const esp_err_t err = servo_release(i);
        if (err != ESP_OK) {
            ret = err;
        }
    }
    return ret;
}

uint16_t servo_get_pulse_us(uint8_t id)
{
    return id < SERVO_COUNT ? s_pulse[id] : 0;
}

esp_err_t servo_get_config(uint8_t id, servo_config_t *out)
{
    if (id >= SERVO_COUNT || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = s_cfg[id];
    return ESP_OK;
}

esp_err_t servo_set_config(uint8_t id, const servo_config_t *cfg)
{
    if (id >= SERVO_COUNT || !cfg || cfg->range_deg <= 0 || cfg->max_us <= cfg->min_us) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg[id] = *cfg;
    return ESP_OK;
}
