/**
 * PCA9685 16-channel PWM driver (power board, I2C 0x40).
 * Channels 0..5 drive the servos, 6/7 are the motor direction lines,
 * 8..15 are spare on header H1. All 16 channels share one frequency.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCA9685_CHANNELS    16
#define PCA9685_RESOLUTION  4096

/** Probe the chip, set totem-pole outputs, all channels off, PWM at freq_hz (24..1526). */
esp_err_t pca9685_init(uint16_t freq_hz);

bool pca9685_is_ready(void);

/** Change the PWM frequency of all channels. */
esp_err_t pca9685_set_frequency(uint16_t freq_hz);

/** Real output frequency after prescaler rounding (0 before init). */
float pca9685_get_frequency(void);

/**
 * The internal oscillator is nominally 25 MHz but varies a few percent from chip
 * to chip. Measure a servo pulse with a scope and correct it here if needed.
 */
void pca9685_set_oscillator_hz(uint32_t hz);

/** Duty in counts, 0..4095. 0 = always low, values >= 4096 = always high. */
esp_err_t pca9685_set_duty(uint8_t channel, uint16_t counts);

/** Pulse width in microseconds at the current frequency (used for servos). */
esp_err_t pca9685_set_pulse_us(uint8_t channel, uint16_t pulse_us);

/** Static level: used for the motor direction lines. */
esp_err_t pca9685_set_level(uint8_t channel, bool high);

/** All 16 outputs low. */
esp_err_t pca9685_all_off(void);

/** Read back the four LEDn registers of one channel (debug). */
esp_err_t pca9685_read_channel(uint8_t channel, uint16_t *on, uint16_t *off);

/** Console command: pca. */
void pca9685_register_commands(void);

#ifdef __cplusplus
}
#endif
