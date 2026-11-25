/**
 * Two DC gear motors on the DRV8833 (power board).
 *
 *   xIN1 = PWM from the ESP32 (GPIO45 / GPIO46)
 *   xIN2 = direction from the PCA9685 (channel 6 / 7)
 *
 * With one PWM pin per motor the two directions are not symmetric:
 *   forward: IN2 = 0, IN1 = PWM       -> drive / coast  (fast decay)
 *   reverse: IN2 = 1, IN1 = inverted  -> drive / brake  (slow decay)
 * The duty inversion for reverse is handled here; expect reverse to feel
 * a little stronger at low speeds than forward.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MOTOR_A = 0,    /* connector J10 */
    MOTOR_B = 1,    /* connector J11 */
    MOTOR_COUNT
} motor_id_t;

/** Needs pca9685_init() first. Both motors coasting. */
esp_err_t motor_init(void);

bool motor_is_ready(void);

/** speed in percent, -100..100. 0 lets the motor coast. */
esp_err_t motor_set(motor_id_t id, int speed_percent);

/** Coast (outputs off) or brake (both outputs low-side on). */
esp_err_t motor_stop(motor_id_t id, bool brake);
esp_err_t motor_stop_all(bool brake);

/** Last commanded speed in percent. */
int motor_get(motor_id_t id);

/** Swap the meaning of forward for one motor (wiring / mounting). */
void motor_set_inverted(motor_id_t id, bool inverted);

/**
 * Safety timeout: when no motor_set() arrives for timeout_ms, both motors coast.
 * 0 disables it (default).
 */
esp_err_t motor_set_watchdog_ms(uint32_t timeout_ms);

/** Console command: motor. */
void motor_register_commands(void);

#ifdef __cplusplus
}
#endif
