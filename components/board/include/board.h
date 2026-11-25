/**
 * Kero main board (ESP32-S3-WROOM-1-N16R8) - the single place where pins and
 * shared resources are defined. Values come from the MainBoard / PowerBoard
 * schematics (netlist export of 2026-11-11).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- main I2C bus: IMU, touch controller, PCA9685 on the power board ---------- */
#define BOARD_I2C_PORT          0
#define BOARD_I2C_SDA           41
#define BOARD_I2C_SCL           42
#define BOARD_I2C_HZ            400000

#define BOARD_ADDR_PCA9685      0x40    /* A0..A5 tied to GND */
#define BOARD_ADDR_IMU          0x6A    /* LSM6DS3TR-C, SA0 tied to GND */

/* ---------- camera (GC2145, DVP 8 bit). PWDN is tied low, RESET has only an RC ---------- */
#define BOARD_CAM_SCCB_PORT     1       /* own I2C port, pulled up to 2.8 V */
#define BOARD_CAM_SDA           4
#define BOARD_CAM_SCL           5
#define BOARD_CAM_XCLK          15
#define BOARD_CAM_PCLK          13
#define BOARD_CAM_VSYNC         6
#define BOARD_CAM_HREF          7
#define BOARD_CAM_D0            11
#define BOARD_CAM_D1            9
#define BOARD_CAM_D2            8
#define BOARD_CAM_D3            10
#define BOARD_CAM_D4            12
#define BOARD_CAM_D5            18
#define BOARD_CAM_D6            17
#define BOARD_CAM_D7            16

/* ---------- display: 360x360 QSPI panel on the 14-pin header. Reset pins are not wired ---------- */
#define BOARD_LCD_CS            14
#define BOARD_LCD_SCK           21
#define BOARD_LCD_D0            47
#define BOARD_LCD_D1            48
#define BOARD_LCD_D2            38
#define BOARD_LCD_D3            39
#define BOARD_LCD_BL            43      /* former UART0 TX: the ROM prints here at reset */
#define BOARD_LCD_H_RES         360
#define BOARD_LCD_V_RES         360

/* ---------- audio: MAX98357A (left channel) and ICS-43434 (left channel) share BCLK / WS ---------- */
#define BOARD_I2S_BCLK          2
#define BOARD_I2S_WS            40
#define BOARD_I2S_DOUT          3       /* to amplifier */
#define BOARD_I2S_DIN           44      /* from microphone, former UART0 RX */

/* ---------- motors: DRV8833 on the power board ---------- */
#define BOARD_MOT_PWM_A         45      /* AIN1, strapping pin: must be low at reset */
#define BOARD_MOT_PWM_B         46      /* BIN1, strapping pin: must be low at reset */
#define BOARD_PCA_CH_MOT_DIR_A  6       /* AIN2 */
#define BOARD_PCA_CH_MOT_DIR_B  7       /* BIN2 */

/* ---------- servos: PCA9685 channels 0..5, channels 8..15 are spare on header H1 ---------- */
#define BOARD_SERVO_COUNT       6
#define BOARD_PCA_CH_SERVO0     0

/* ---------- battery: switched battery voltage / 2 on ADC1 channel 0 ---------- */
#define BOARD_BAT_ADC_GPIO      1
#define BOARD_BAT_DIVIDER_NUM   2       /* 100k / 100k */
#define BOARD_BAT_DIVIDER_DEN   1

#define BOARD_BOOT_BUTTON       0

/* ---------- LEDC allocation (all low-speed mode), so modules never collide ---------- */
#define BOARD_LEDC_TIMER_CAM    0       /* camera XCLK */
#define BOARD_LEDC_CH_CAM       0
#define BOARD_LEDC_TIMER_MOTOR  1
#define BOARD_LEDC_CH_MOT_A     1
#define BOARD_LEDC_CH_MOT_B     2
#define BOARD_LEDC_TIMER_BL     2
#define BOARD_LEDC_CH_BL        3

/** Create the main I2C bus. Safe to call more than once. */
esp_err_t board_i2c_init(void);

/** Bus handle, NULL before board_i2c_init() succeeded. */
i2c_master_bus_handle_t board_i2c_bus(void);

/** ESP_OK when a device answers at this 7-bit address. */
esp_err_t board_i2c_probe(uint8_t addr);

/** Attach a device to the main bus. */
esp_err_t board_i2c_add_device(uint8_t addr, i2c_master_dev_handle_t *out_dev);

/** Register helpers shared by the I2C device drivers (len up to 16 bytes for writes). */
esp_err_t board_i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, const uint8_t *data, size_t len);
esp_err_t board_i2c_write_reg8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value);
esp_err_t board_i2c_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len);

/** Console command: i2c scan / i2c r / i2c w. */
void board_register_commands(void);

#ifdef __cplusplus
}
#endif
