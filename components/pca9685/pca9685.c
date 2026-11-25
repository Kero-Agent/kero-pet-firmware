#include "pca9685.h"
#include "board.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pca9685";

#define REG_MODE1       0x00
#define REG_MODE2       0x01
#define REG_LED0_ON_L   0x06
#define REG_ALL_ON_L    0xFA
#define REG_PRESCALE    0xFE

#define MODE1_RESTART   0x80
#define MODE1_AI        0x20    /* register auto-increment */
#define MODE1_SLEEP     0x10
#define MODE2_OUTDRV    0x04    /* totem-pole outputs */

#define FULL_BIT        0x1000  /* bit 4 of LEDn_ON_H / LEDn_OFF_H */

static i2c_master_dev_handle_t s_dev;
static uint32_t s_osc_hz = 25000000;
static float s_freq_hz;

static esp_err_t write_channel(uint8_t reg, uint16_t on, uint16_t off)
{
    const uint8_t data[4] = { on & 0xFF, on >> 8, off & 0xFF, off >> 8 };
    return board_i2c_write_reg(s_dev, reg, data, sizeof(data));
}

bool pca9685_is_ready(void)
{
    return s_dev != NULL && s_freq_hz > 0;
}

void pca9685_set_oscillator_hz(uint32_t hz)
{
    s_osc_hz = hz;
}

float pca9685_get_frequency(void)
{
    return s_freq_hz;
}

esp_err_t pca9685_set_frequency(uint16_t freq_hz)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(freq_hz >= 24 && freq_hz <= 1526, ESP_ERR_INVALID_ARG, TAG, "frequency out of range");

    int prescale = (int)((float)s_osc_hz / (PCA9685_RESOLUTION * (float)freq_hz) + 0.5f) - 1;
    if (prescale < 3) {
        prescale = 3;
    }
    if (prescale > 255) {
        prescale = 255;
    }
    /* The prescaler can only be written while the oscillator is stopped. */
    ESP_RETURN_ON_ERROR(board_i2c_write_reg8(s_dev, REG_MODE1, MODE1_AI | MODE1_SLEEP), TAG, "sleep failed");
    ESP_RETURN_ON_ERROR(board_i2c_write_reg8(s_dev, REG_PRESCALE, (uint8_t)prescale), TAG, "prescale failed");
    ESP_RETURN_ON_ERROR(board_i2c_write_reg8(s_dev, REG_MODE1, MODE1_AI), TAG, "wake failed");
    vTaskDelay(pdMS_TO_TICKS(2));   /* oscillator needs 500 us to start */
    ESP_RETURN_ON_ERROR(board_i2c_write_reg8(s_dev, REG_MODE1, MODE1_AI | MODE1_RESTART), TAG, "restart failed");

    s_freq_hz = (float)s_osc_hz / (PCA9685_RESOLUTION * (float)(prescale + 1));
    ESP_LOGI(TAG, "prescale %d -> %.2f Hz", prescale, s_freq_hz);
    return ESP_OK;
}

esp_err_t pca9685_init(uint16_t freq_hz)
{
    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "no I2C bus");
    ESP_RETURN_ON_ERROR(board_i2c_probe(BOARD_ADDR_PCA9685), TAG,
                        "no answer at 0x%02X - is the power board connected and 3V3 present?", BOARD_ADDR_PCA9685);
    if (!s_dev) {
        ESP_RETURN_ON_ERROR(board_i2c_add_device(BOARD_ADDR_PCA9685, &s_dev), TAG, "add device failed");
    }
    /* Outputs low before anything else, so servos and motor direction lines stay quiet. */
    ESP_RETURN_ON_ERROR(write_channel(REG_ALL_ON_L, 0, FULL_BIT), TAG, "all-off failed");
    ESP_RETURN_ON_ERROR(board_i2c_write_reg8(s_dev, REG_MODE2, MODE2_OUTDRV), TAG, "mode2 failed");
    /* MODE1 written by set_frequency() also clears ALLCALL, so the chip stops answering at 0x70. */
    return pca9685_set_frequency(freq_hz);
}

esp_err_t pca9685_set_duty(uint8_t channel, uint16_t counts)
{
    ESP_RETURN_ON_FALSE(pca9685_is_ready(), ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(channel < PCA9685_CHANNELS, ESP_ERR_INVALID_ARG, TAG, "bad channel %d", channel);
    const uint8_t reg = REG_LED0_ON_L + 4 * channel;
    if (counts == 0) {
        return write_channel(reg, 0, FULL_BIT);         /* full off */
    }
    if (counts >= PCA9685_RESOLUTION) {
        return write_channel(reg, FULL_BIT, 0);         /* full on */
    }
    return write_channel(reg, 0, counts);
}

esp_err_t pca9685_set_pulse_us(uint8_t channel, uint16_t pulse_us)
{
    ESP_RETURN_ON_FALSE(pca9685_is_ready(), ESP_ERR_INVALID_STATE, TAG, "not initialised");
    const float period_us = 1e6f / s_freq_hz;
    uint32_t counts = (uint32_t)((float)pulse_us * PCA9685_RESOLUTION / period_us + 0.5f);
    if (counts > PCA9685_RESOLUTION - 1) {
        counts = PCA9685_RESOLUTION - 1;
    }
    return pca9685_set_duty(channel, (uint16_t)counts);
}

esp_err_t pca9685_set_level(uint8_t channel, bool high)
{
    return pca9685_set_duty(channel, high ? PCA9685_RESOLUTION : 0);
}

esp_err_t pca9685_all_off(void)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    return write_channel(REG_ALL_ON_L, 0, FULL_BIT);
}

esp_err_t pca9685_read_channel(uint8_t channel, uint16_t *on, uint16_t *off)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(channel < PCA9685_CHANNELS, ESP_ERR_INVALID_ARG, TAG, "bad channel %d", channel);
    uint8_t d[4];
    ESP_RETURN_ON_ERROR(board_i2c_read_reg(s_dev, REG_LED0_ON_L + 4 * channel, d, sizeof(d)), TAG, "read failed");
    *on = (uint16_t)(d[0] | (d[1] << 8));
    *off = (uint16_t)(d[2] | (d[3] << 8));
    return ESP_OK;
}
