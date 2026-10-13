/*
 * Every public header in one file under -Werror, with ESP-IDF's flags: they
 * must not clash with each other. Which global symbols the library defines
 * is the Makefile's `names` check, on both platforms.
 */
#include <stdio.h>

#include "pca9685.h"
#include "servo.h"
#include "servo_log.h"
#include "servo_pwm.h"

int main(void)
{
	struct servo_pwm pwm = { 0 };
	struct servo_dev *servo = &pwm.servo;

	(void)servo;
	(void)sizeof(pca9685_config_t);
	printf("test_names: all passed\n");
	return 0;
}
