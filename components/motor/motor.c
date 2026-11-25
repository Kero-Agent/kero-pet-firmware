#include "motor.h"
#include "pca9685.h"
#include "board.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "motor";

#define PWM_FREQ_HZ     20000
#define PWM_RESOLUTION  LEDC_TIMER_10_BIT
#define PWM_FULL        1024    /* 2^10: output constantly high */

typedef struct {
    int gpio;
    ledc_channel_t ledc_ch;
    uint8_t dir_ch;     /* PCA9685 channel wired to xIN2 */
    bool dir_high;      /* current xIN2 level */
    bool inverted;
    int speed;
} motor_t;

static motor_t s_motor[MOTOR_COUNT] = {
    [MOTOR_A] = { .gpio = BOARD_MOT_PWM_A, .ledc_ch = BOARD_LEDC_CH_MOT_A, .dir_ch = BOARD_PCA_CH_MOT_DIR_A },
    [MOTOR_B] = { .gpio = BOARD_MOT_PWM_B, .ledc_ch = BOARD_LEDC_CH_MOT_B, .dir_ch = BOARD_PCA_CH_MOT_DIR_B },
};
static bool s_ready;
static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_wdt;
static uint32_t s_wdt_ms;

static esp_err_t set_in1(motor_t *m, uint32_t duty)
{
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, m->ledc_ch, duty), TAG, "ledc_set_duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, m->ledc_ch);
}

/* Drive xIN1 with `duty` and xIN2 with `dir_high`, sequenced so the bridge never
 * sees a state that pushes the motor against the direction it is leaving. */
static esp_err_t apply(motor_t *m, uint32_t duty, bool dir_high)
{
    if (dir_high != m->dir_high) {
        if (!dir_high) {
            ESP_RETURN_ON_ERROR(set_in1(m, 0), TAG, "pwm");     /* leaving reverse: IN1 low first */
        }
        ESP_RETURN_ON_ERROR(pca9685_set_level(m->dir_ch, dir_high), TAG, "direction line (PCA9685 ch%d)", m->dir_ch);
        m->dir_high = dir_high;
    }
    return set_in1(m, duty);
}

static esp_err_t set_locked(motor_t *m, int speed)
{
    if (speed > 100) {
        speed = 100;
    }
    if (speed < -100) {
        speed = -100;
    }
    const int drive = m->inverted ? -speed : speed;
    const uint32_t mag = (uint32_t)(drive < 0 ? -drive : drive) * PWM_FULL / 100;
    esp_err_t err;
    if (drive >= 0) {
        err = apply(m, mag, false);                 /* IN2 = 0: duty is the speed */
    } else {
        err = apply(m, PWM_FULL - mag, true);       /* IN2 = 1: duty is inverted */
    }
    if (err == ESP_OK) {
        m->speed = speed;
    }
    return err;
}

static void wdt_expired(void *arg)
{
    ESP_LOGW(TAG, "watchdog: no command for %u ms, motors stopped", (unsigned)s_wdt_ms);
    motor_stop_all(false);
}

static void wdt_feed(void)
{
    if (s_wdt && s_wdt_ms) {
        esp_timer_stop(s_wdt);
        esp_timer_start_once(s_wdt, (uint64_t)s_wdt_ms * 1000);
    }
}

bool motor_is_ready(void)
{
    return s_ready;
}

esp_err_t motor_init(void)
{
    ESP_RETURN_ON_FALSE(pca9685_is_ready(), ESP_ERR_INVALID_STATE, TAG, "pca9685 is not ready (direction lines)");
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    }
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BOARD_LEDC_TIMER_MOTOR,
        .duty_resolution = PWM_RESOLUTION,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");
    for (int i = 0; i < MOTOR_COUNT; i++) {
        motor_t *m = &s_motor[i];
        ESP_RETURN_ON_ERROR(pca9685_set_level(m->dir_ch, false), TAG, "direction line");
        m->dir_high = false;
        m->speed = 0;
        const ledc_channel_config_t ch = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = m->ledc_ch,
            .timer_sel = BOARD_LEDC_TIMER_MOTOR,
            .gpio_num = m->gpio,
            .duty = 0,
            .hpoint = 0,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "ledc channel");
    }
    if (!s_wdt) {
        const esp_timer_create_args_t args = { .callback = wdt_expired, .name = "motor_wdt" };
        ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_wdt), TAG, "watchdog timer");
    }
    s_ready = true;
    ESP_LOGI(TAG, "ready: PWM %d Hz on GPIO%d / GPIO%d, direction on PCA9685 ch%d / ch%d",
             PWM_FREQ_HZ, BOARD_MOT_PWM_A, BOARD_MOT_PWM_B, BOARD_PCA_CH_MOT_DIR_A, BOARD_PCA_CH_MOT_DIR_B);
    return ESP_OK;
}

esp_err_t motor_set(motor_id_t id, int speed_percent)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(id < MOTOR_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad motor id");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const esp_err_t err = set_locked(&s_motor[id], speed_percent);
    xSemaphoreGive(s_lock);
    wdt_feed();
    return err;
}

esp_err_t motor_stop(motor_id_t id, bool brake)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(id < MOTOR_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad motor id");
    motor_t *m = &s_motor[id];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* brake: IN1 = IN2 = 1, coast: IN1 = IN2 = 0 */
    const esp_err_t err = brake ? apply(m, PWM_FULL, true) : apply(m, 0, false);
    if (err == ESP_OK) {
        m->speed = 0;
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t motor_stop_all(bool brake)
{
    esp_err_t ret = ESP_OK;
    for (int i = 0; i < MOTOR_COUNT; i++) {
        const esp_err_t err = motor_stop((motor_id_t)i, brake);
        if (err != ESP_OK) {
            ret = err;
        }
    }
    return ret;
}

int motor_get(motor_id_t id)
{
    return id < MOTOR_COUNT ? s_motor[id].speed : 0;
}

void motor_set_inverted(motor_id_t id, bool inverted)
{
    if (id < MOTOR_COUNT) {
        s_motor[id].inverted = inverted;
    }
}

esp_err_t motor_set_watchdog_ms(uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_wdt, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    esp_timer_stop(s_wdt);
    s_wdt_ms = timeout_ms;
    wdt_feed();
    return ESP_OK;
}
