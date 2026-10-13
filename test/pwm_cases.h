/*
 * The PWM backend's cases, shared by esp/test_pwm.c and stm/test_pwm.c. Each
 * includes this after providing, over its own fake output:
 *
 *   config_at(config, rate_hz)   a valid config at that rate, servo 0
 *   out_us()        the pulse the pin puts out now, to the nearest us
 *   out_running()   whether the pin is putting out pulses at all
 *   fail_next       the platform call that starts or writes the output fails
 */

static const struct servo_calibration calibration = {
	.min_us = 1000, .max_us = 2000, .range_deg = 180.0f,
};

/* Init binds the pin and drives nothing; the first move starts it. */
static void test_nothing_until_moved(void)
{
	struct servo_pwm pwm;
	struct servo_pwm_config config;
	struct servo_dev *servo;

	config_at(&config, 50);
	assert(servo_pwm_init(&pwm, &config) == 0);
	assert(!out_running());
	servo = servo_pwm_servo(&pwm);
	assert(servo && servo->priv == &pwm && servo->pulse_us == 0);
	assert(strcmp(servo->name, "pwm") == 0);

	/* uncalibrated: refused, still nothing on the pin */
	assert(servo_set_angle(servo, 90.0f) == -EINVAL);
	assert(!out_running());

	assert(servo_set_calibration(servo, &calibration) == 0);
	assert(!out_running());
	assert(servo_set_angle(servo, 90.0f) == 0);
	assert(out_running() && out_us() == 1500 && servo->pulse_us == 1500);
	assert(servo_set_angle(servo, 0.0f) == 0 && out_us() == 1000);
	assert(servo_set_pulse_us(servo, 1777) == 0 && out_us() == 1777);

	/* stop: no pulses; the next move starts them again */
	assert(servo_stop(servo) == 0);
	assert(!out_running() && servo->pulse_us == 0);
	assert(servo_stop(servo) == 0 && !out_running());
	assert(servo_set_angle(servo, 180.0f) == 0);
	assert(out_running() && out_us() == 2000);

	servo_pwm_del(&pwm);
	assert(!out_running());
	assert(servo_pwm_servo(&pwm) == NULL);
	servo_pwm_del(&pwm);	/* twice is harmless */
}

/* The widest pulse must fit in a period with low time left. */
static void test_period(void)
{
	struct servo_pwm pwm;
	struct servo_pwm_config config;
	struct servo_dev *servo;
	struct servo_calibration c = calibration;

	/* 500 Hz: 2000 us is the whole period, 1999 leaves a moment low */
	config_at(&config, 500);
	assert(servo_pwm_init(&pwm, &config) == 0);
	servo = servo_pwm_servo(&pwm);
	assert(servo_set_calibration(servo, &c) == -EINVAL);
	c.max_us = 1999;
	assert(servo_set_calibration(servo, &c) == 0);
	assert(servo_set_angle(servo, 180.0f) == 0 && out_us() == 1999);
	servo_pwm_del(&pwm);
}

/* A failed start leaves the pin and the record as they were. */
static void test_failures(void)
{
	struct servo_pwm pwm;
	struct servo_pwm_config config;
	struct servo_dev *servo;

	config_at(&config, 50);
	assert(servo_pwm_init(&pwm, &config) == 0);
	servo = servo_pwm_servo(&pwm);
	assert(servo_set_calibration(servo, &calibration) == 0);
	fail_next = true;
	assert(servo_set_angle(servo, 90.0f) != 0);
	fail_next = false;
	assert(!out_running() && servo->pulse_us == 0);
	assert(servo_set_angle(servo, 90.0f) == 0 && out_us() == 1500);
	servo_pwm_del(&pwm);

	assert(servo_pwm_init(NULL, &config) == -EINVAL);
	assert(servo_pwm_init(&pwm, NULL) == -EINVAL);
	assert(servo_pwm_servo(NULL) == NULL);
	servo_pwm_del(NULL);
}

static void run_pwm_cases(void)
{
	test_nothing_until_moved();
	test_period();
	test_failures();
}
