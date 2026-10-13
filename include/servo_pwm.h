/*
 * One hobby servo on one pin of the MCU's own PWM: an LEDC channel on
 * ESP-IDF, a TIM channel on the STM32 HAL. The library's replacement for
 * esp-servo-pwm, whose API it does not keep.
 *
 * The app places a struct servo_pwm per servo and picks its timer and
 * channel, as for mcu-esc's C615. Init binds the pin and drives nothing: no
 * pulse goes out until the first move, so a servo powered up with the
 * board stays where it was parked. Calibrate the struct servo_dev from
 * servo_pwm_servo() before the first move; see servo.h.
 *
 * ESP-IDF: low-speed mode, 14-bit duty. Servos may share an LEDC timer at
 * one rate; the first init on a timer configures it, later ones at the
 * same rate leave it running untouched, and one at another rate is
 * -EBUSY. A timer the app configured itself must be at 14 bits.
 *
 * HAL: the app sets the TIM channel up in PWM mode 1, with output-compare
 * preload (CCMRx OCxPE) so a pulse in flight is never cut, and gives the
 * counter clock; the rate is tick_hz / (ARR + 1).
 */
#ifndef SERVO_PWM_H
#define SERVO_PWM_H

#include <stdint.h>

#include "servo.h"

#ifdef ESP_PLATFORM
#include "driver/ledc.h"
#else
#include CMSIS_device_header
#ifndef HAL_TIM_MODULE_ENABLED
#error "servo_pwm.h needs the HAL TIM module: #define HAL_TIM_MODULE_ENABLED in the app's stm32<family>xx_hal_conf.h, with stm32<family>xx_hal_tim.c in the build"
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct servo_pwm_config {
#ifdef ESP_PLATFORM
	int gpio;
	ledc_timer_t timer;	/* low-speed LEDC timer, 14-bit duty */
	ledc_channel_t channel;	/* low-speed LEDC channel */
	uint32_t rate_hz;	/* the servo's frame rate */
#else
	TIM_HandleTypeDef *tim;	/* in PWM mode 1 with preload, see above */
	uint32_t channel;	/* TIM_CHANNEL_1..4 */
	uint32_t tick_hz;	/* counter clock after the prescaler */
#endif
};

/*
 * The output port's state: one pin, see src/servo_priv.h. Private, like
 * struct servo_pwm: here only so the app can place one.
 */
struct servo_output {
#ifdef ESP_PLATFORM
	int gpio;
	ledc_timer_t timer;
	ledc_channel_t channel;
#else
	TIM_HandleTypeDef *tim;
	uint32_t channel;
	uint32_t tick_hz;
#endif
	uint32_t rate_hz;
};

/* Private. Declared here only so the app can place a struct servo_pwm. */
struct servo_pwm {
	struct servo_dev servo;
	struct servo_output output;
	bool running;		/* pulses on the pin */
};

/*
 * Check the config and bind the pin, driving nothing. -EINVAL for a rate
 * of 0, a bad pin, handle or channel (on ESP-IDF a channel from
 * LEDC_CHANNEL_MAX up), and on the HAL a channel without output-compare
 * preload; -EBUSY on ESP-IDF for a timer already running at another rate;
 * the LEDC's or the HAL's error otherwise.
 */
int servo_pwm_init(struct servo_pwm *pwm,
		   const struct servo_pwm_config *config);

/* The servo, to calibrate and move; NULL before init or after del. */
struct servo_dev *servo_pwm_servo(struct servo_pwm *pwm);

/*
 * Stop the pulses and unbind. The timer is left running: on ESP-IDF a
 * later servo_pwm_init() on that timer at another rate is -EBUSY.
 */
void servo_pwm_del(struct servo_pwm *pwm);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_PWM_H */
