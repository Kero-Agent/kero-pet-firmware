#include "imu.h"
#include "board.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "imu";

#define REG_WHO_AM_I    0x0F
#define REG_CTRL1_XL    0x10
#define REG_CTRL2_G     0x11
#define REG_CTRL3_C     0x12
#define REG_OUT_TEMP_L  0x20    /* temp(2) gyro(6) accel(6) follow each other */

#define WHO_AM_I_VALUE  0x6A

#define CTRL1_XL_104HZ_4G       0x48    /* ODR 104 Hz, FS +-4 g */
#define CTRL2_G_104HZ_1000DPS   0x48    /* ODR 104 Hz, FS +-1000 dps */
#define CTRL3_C_BDU_IFINC       0x44    /* block data update, register auto-increment */
#define CTRL3_C_SW_RESET        0x01

#define ACCEL_G_PER_LSB         0.000122f   /* +-4 g */
#define GYRO_DPS_PER_LSB        0.035f      /* +-1000 dps */

static i2c_master_dev_handle_t s_dev;
static bool s_ready;
static float s_gyro_offset[3];

static inline int16_t le16(const uint8_t *p)
{
    return (int16_t)(p[0] | (p[1] << 8));
}

bool imu_is_ready(void)
{
    return s_ready;
}

esp_err_t imu_init(void)
{
    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "no I2C bus");
    ESP_RETURN_ON_ERROR(board_i2c_probe(BOARD_ADDR_IMU), TAG, "no answer at 0x%02X", BOARD_ADDR_IMU);
    if (!s_dev) {
        ESP_RETURN_ON_ERROR(board_i2c_add_device(BOARD_ADDR_IMU, &s_dev), TAG, "add device failed");
    }
    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(board_i2c_read_reg(s_dev, REG_WHO_AM_I, &id, 1), TAG, "WHO_AM_I read failed");
    ESP_RETURN_ON_FALSE(id == WHO_AM_I_VALUE, ESP_ERR_NOT_FOUND, TAG, "WHO_AM_I is 0x%02X, expected 0x%02X", id, WHO_AM_I_VALUE);

    ESP_RETURN_ON_ERROR(board_i2c_write_reg8(s_dev, REG_CTRL3_C, CTRL3_C_SW_RESET), TAG, "reset failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(board_i2c_write_reg8(s_dev, REG_CTRL3_C, CTRL3_C_BDU_IFINC), TAG, "CTRL3_C");
    ESP_RETURN_ON_ERROR(board_i2c_write_reg8(s_dev, REG_CTRL1_XL, CTRL1_XL_104HZ_4G), TAG, "CTRL1_XL");
    ESP_RETURN_ON_ERROR(board_i2c_write_reg8(s_dev, REG_CTRL2_G, CTRL2_G_104HZ_1000DPS), TAG, "CTRL2_G");

    s_ready = true;
    ESP_LOGI(TAG, "LSM6DS3TR-C ready: 104 Hz, +-4 g, +-1000 dps");
    return ESP_OK;
}

static esp_err_t read_raw(imu_sample_t *out)
{
    uint8_t d[14];
    ESP_RETURN_ON_ERROR(board_i2c_read_reg(s_dev, REG_OUT_TEMP_L, d, sizeof(d)), TAG, "read failed");
    out->temperature_c = 25.0f + le16(&d[0]) / 256.0f;
    for (int i = 0; i < 3; i++) {
        out->gyro_dps[i] = le16(&d[2 + 2 * i]) * GYRO_DPS_PER_LSB;
        out->accel_g[i] = le16(&d[8 + 2 * i]) * ACCEL_G_PER_LSB;
    }
    return ESP_OK;
}

esp_err_t imu_read(imu_sample_t *out)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null");
    ESP_RETURN_ON_ERROR(read_raw(out), TAG, "read");
    for (int i = 0; i < 3; i++) {
        out->gyro_dps[i] -= s_gyro_offset[i];
    }
    return ESP_OK;
}

esp_err_t imu_calibrate_gyro(void)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    const int n = 100;
    float sum[3] = { 0 };
    for (int k = 0; k < n; k++) {
        imu_sample_t s;
        ESP_RETURN_ON_ERROR(read_raw(&s), TAG, "read");
        for (int i = 0; i < 3; i++) {
            sum[i] += s.gyro_dps[i];
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (int i = 0; i < 3; i++) {
        s_gyro_offset[i] = sum[i] / n;
    }
    ESP_LOGI(TAG, "gyro offset %.2f %.2f %.2f dps", s_gyro_offset[0], s_gyro_offset[1], s_gyro_offset[2]);
    return ESP_OK;
}
