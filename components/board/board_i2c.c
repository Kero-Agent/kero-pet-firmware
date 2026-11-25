#include <string.h>
#include "board.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "board";

#define I2C_TIMEOUT_MS 50

static i2c_master_bus_handle_t s_bus;

esp_err_t board_i2c_init(void)
{
    if (s_bus) {
        return ESP_OK;
    }
    const i2c_master_bus_config_t cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,  /* 4.7k pull-ups are on the board */
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "i2c bus create failed");
    ESP_LOGI(TAG, "I2C%d ready: SDA=%d SCL=%d %d Hz", BOARD_I2C_PORT, BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_HZ);
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_bus;
}

esp_err_t board_i2c_probe(uint8_t addr)
{
    if (!s_bus) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_probe(s_bus, addr, I2C_TIMEOUT_MS);
}

esp_err_t board_i2c_add_device(uint8_t addr, i2c_master_dev_handle_t *out_dev)
{
    if (!s_bus) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BOARD_I2C_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &cfg, out_dev);
}

esp_err_t board_i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[1 + 16];
    if (len > sizeof(buf) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(dev, buf, len + 1, I2C_TIMEOUT_MS);
}

esp_err_t board_i2c_write_reg8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    return board_i2c_write_reg(dev, reg, &value, 1);
}

esp_err_t board_i2c_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, I2C_TIMEOUT_MS);
}
