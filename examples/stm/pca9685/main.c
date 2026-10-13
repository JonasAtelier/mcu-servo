/*
 * A PCA9685 on I2C1: twelve servos through servo.h, and an LED on channel
 * 15 as a plain PWM output.
 *
 * A snippet, not a project: hi2c1 is the app's, as CubeMX generates it, at
 * up to 1 MHz (Fast-mode Plus, datasheet section 1). Needs
 * HAL_I2C_MODULE_ENABLED, stm32<family>xx_hal_i2c.c in the build and
 * SERVO_PCA9685 = y. MIN_US, MAX_US and RANGE_DEG are placeholders, not any
 * servo's figures: measure yours (docs/usage.md, "Calibrating").
 */
#include <stdio.h>

#include CMSIS_device_header

#include "pca9685.h"
#include "servo.h"

#define SERVOS		12
#define LED_CHANNEL	15
#define MIN_US		1250	/* placeholder: measured pulse at 0 deg */
#define MAX_US		1750	/* placeholder: measured pulse at RANGE_DEG */
#define RANGE_DEG	60.0f	/* placeholder: measured travel */

extern I2C_HandleTypeDef hi2c1;		/* from MX_I2C1_Init() */

static pca9685_handle_t chip;

int servo_example_init(void)
{
	const pca9685_config_t cfg = {
		.frequency_hz = 50.0f,	/* placeholder: the servos' rate */
		.totem_pole = true,
	};
	const struct servo_calibration calibration = {
		.min_us = MIN_US, .max_us = MAX_US, .range_deg = RANGE_DEG,
	};
	int err;

	err = pca9685_init(&hi2c1, &cfg, &chip);
	if (err) {
		printf("pca9685: %d\n", err);	/* -ENXIO: nothing there */
		return err;
	}
	for (int ch = 0; ch < SERVOS; ch++) {
		err = servo_set_calibration(pca9685_servo(chip, ch),
					    &calibration);
		if (err)
			return err;
	}
	return pca9685_set_duty(chip, LED_CHANNEL, 0.25f);
}

void servo_example_move(const float angle_deg[SERVOS])
{
	for (int ch = 0; ch < SERVOS; ch++)
		servo_set_angle(pca9685_servo(chip, ch), angle_deg[ch]);
}

/* Every output full off in one write, the LED included. */
void servo_example_stop(void)
{
	pca9685_all_off(chip);
}
