/*
 * Two servos on TIM3 channels 1 and 2, at the rate the app's timer runs.
 *
 * A snippet, not a project: htim3 is the app's, as CubeMX generates it in
 * PWM mode 1 on both channels, with the prescaler set for a 1 MHz counter
 * -- 1 us per count -- an ARR that gives the servos' frame rate, and
 * output-compare preload on each channel (CCMRx OCxPE), so a new width
 * waits for the next period instead of cutting the pulse in flight. Needs
 * HAL_TIM_MODULE_ENABLED, stm32<family>xx_hal_tim.c in the build and
 * SERVO_PWM = y. MIN_US, MAX_US and RANGE_DEG are placeholders, not any
 * servo's figures: measure yours (docs/usage.md, "Calibrating").
 */
#include <stdio.h>

#include CMSIS_device_header

#include "servo.h"
#include "servo_pwm.h"

#define SERVOS		2
#define MIN_US		1250	/* placeholder: measured pulse at 0 deg */
#define MAX_US		1750	/* placeholder: measured pulse at RANGE_DEG */
#define RANGE_DEG	60.0f	/* placeholder: measured travel */

extern TIM_HandleTypeDef htim3;		/* from MX_TIM3_Init() */

static struct servo_pwm joint[SERVOS];

void servo_example_init(void)
{
	static const uint32_t channel[SERVOS] = {
		TIM_CHANNEL_1, TIM_CHANNEL_2,
	};
	const struct servo_calibration calibration = {
		.min_us = MIN_US, .max_us = MAX_US, .range_deg = RANGE_DEG,
	};

	for (int i = 0; i < SERVOS; i++) {
		struct servo_pwm_config cfg = {
			.tim = &htim3,
			.channel = channel[i],
			.tick_hz = 1000000,
		};
		int err = servo_pwm_init(&joint[i], &cfg);

		if (!err)
			err = servo_set_calibration(servo_pwm_servo(&joint[i]),
						    &calibration);
		if (err)
			printf("servo %d: %d\n", i + 1, err);
	}
}

/* Each joint's angle, 0..RANGE_DEG; the first move starts its pulses. */
void servo_example_move(const float angle_deg[SERVOS])
{
	for (int i = 0; i < SERVOS; i++)
		servo_set_angle(servo_pwm_servo(&joint[i]), angle_deg[i]);
}

/* Let go: no pulses, the servos go slack. */
void servo_example_stop(void)
{
	for (int i = 0; i < SERVOS; i++)
		servo_pwm_del(&joint[i]);
}
