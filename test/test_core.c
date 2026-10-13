/*
 * The core, src/servo.c and src/servo_math.h, on a fake backend: the
 * calibration checks, the angle-to-pulse mapping, the clamps, and what is
 * left alone when something is refused. No platform code is involved, so it
 * runs once.
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "servo.h"
#include "servo_math.h"

static int writes, stops;
static uint16_t written_us;
static int write_ret, stop_ret;
static uint16_t period_us = 20000;	/* what check_us lets through */

static int fake_write_us(struct servo_dev *servo, uint16_t pulse_us)
{
	(void)servo;
	writes++;
	if (write_ret)
		return write_ret;
	written_us = pulse_us;
	return 0;
}

static int fake_check_us(struct servo_dev *servo, uint16_t pulse_us)
{
	(void)servo;
	return pulse_us < period_us ? 0 : -EINVAL;
}

static int fake_stop(struct servo_dev *servo)
{
	(void)servo;
	stops++;
	return stop_ret;
}

static const struct servo_ops fake_ops = {
	.write_us = fake_write_us,
	.check_us = fake_check_us,
	.stop = fake_stop,
};

static const struct servo_ops unchecked_ops = {
	.write_us = fake_write_us,
	.stop = fake_stop,
};

static const struct servo_calibration plain = {
	.min_us = 1000, .max_us = 2000, .range_deg = 180.0f,
};

static struct servo_dev fresh(void)
{
	struct servo_dev servo = { .name = "fake", .ops = &fake_ops };

	writes = stops = 0;
	written_us = 0;
	write_ret = stop_ret = 0;
	return servo;
}

static void test_calibration_refused(void)
{
	struct servo_dev servo = fresh();
	struct servo_dev unbound = { .name = "unbound" };
	struct servo_calibration c;

	/* min below max */
	c = plain;
	c.min_us = 2000;
	assert(servo_set_calibration(&servo, &c) == -EINVAL);
	c.min_us = 2001;
	assert(servo_set_calibration(&servo, &c) == -EINVAL);

	/* range above 0 and finite */
	c = plain;
	c.range_deg = 0.0f;
	assert(servo_set_calibration(&servo, &c) == -EINVAL);
	c.range_deg = -90.0f;
	assert(servo_set_calibration(&servo, &c) == -EINVAL);
	c.range_deg = NAN;
	assert(servo_set_calibration(&servo, &c) == -EINVAL);
	c.range_deg = INFINITY;
	assert(servo_set_calibration(&servo, &c) == -EINVAL);

	/* a trim as wide as the span or wider, either way */
	c = plain;
	c.trim_us = 1000;
	assert(servo_set_calibration(&servo, &c) == -EINVAL);
	c.trim_us = -1000;
	assert(servo_set_calibration(&servo, &c) == -EINVAL);
	c.trim_us = 999;
	assert(servo_set_calibration(&servo, &c) == 0);
	c.trim_us = -999;
	assert(servo_set_calibration(&servo, &c) == 0);

	/* the backend says max_us does not fit: its error, nothing kept */
	servo = fresh();
	c = plain;
	c.max_us = 20000;
	assert(servo_set_calibration(&servo, &c) == -EINVAL);
	assert(servo.calibration.max_us == 0);
	assert(servo_set_angle(&servo, 90.0f) == -EINVAL && writes == 0);

	/* no backend, no calibration, no servo */
	assert(servo_set_calibration(&unbound, &plain) == -EINVAL);
	assert(servo_set_calibration(&servo, NULL) == -EINVAL);
	assert(servo_set_calibration(NULL, &plain) == -EINVAL);

	/* a backend without check_us takes any max_us */
	servo = fresh();
	servo.ops = &unchecked_ops;
	assert(servo_set_calibration(&servo, &c) == 0);
	assert(servo.calibration.max_us == 20000);
}

/* An uncalibrated servo refuses to move, and touches nothing. */
static void test_uncalibrated(void)
{
	struct servo_dev servo = fresh();

	assert(servo_set_angle(&servo, 90.0f) == -EINVAL);
	assert(servo_set_pulse_us(&servo, 1500) == -EINVAL);
	assert(writes == 0 && servo.pulse_us == 0);
	/* stopping needs no calibration */
	assert(servo_stop(&servo) == 0 && stops == 1);
}

static void test_angle(void)
{
	struct servo_dev servo = fresh();

	assert(servo_set_calibration(&servo, &plain) == 0);

	/* ends, middle, rounding to the nearest microsecond */
	assert(servo_set_angle(&servo, 0.0f) == 0 && written_us == 1000);
	assert(servo.pulse_us == 1000);
	assert(servo_set_angle(&servo, 180.0f) == 0 && written_us == 2000);
	assert(servo_set_angle(&servo, 90.0f) == 0 && written_us == 1500);
	/* 1000 + 45.1 / 180 * 1000 = 1250.56 -> 1251 */
	assert(servo_set_angle(&servo, 45.1f) == 0 && written_us == 1251);
	/* 1000 + 45.08 / 180 * 1000 = 1250.44 -> 1250 */
	assert(servo_set_angle(&servo, 45.08f) == 0 && written_us == 1250);

	/* outside the range clamps to the ends */
	assert(servo_set_angle(&servo, -30.0f) == 0 && written_us == 1000);
	assert(servo_set_angle(&servo, 999.0f) == 0 && written_us == 2000);

	/* not finite: refused, nothing written */
	writes = 0;
	assert(servo_set_angle(&servo, NAN) == -EINVAL);
	assert(servo_set_angle(&servo, INFINITY) == -EINVAL);
	assert(servo_set_angle(&servo, -INFINITY) == -EINVAL);
	assert(writes == 0 && servo.pulse_us == 2000);

	assert(servo_set_angle(NULL, 0.0f) == -EINVAL);
}

static void test_invert_and_trim(void)
{
	struct servo_dev servo = fresh();
	struct servo_calibration c = plain;

	/* invert: 0 goes where 180 would, and the middle stays */
	c.invert = true;
	assert(servo_set_calibration(&servo, &c) == 0);
	assert(servo_set_angle(&servo, 0.0f) == 0 && written_us == 2000);
	assert(servo_set_angle(&servo, 180.0f) == 0 && written_us == 1000);
	assert(servo_set_angle(&servo, 90.0f) == 0 && written_us == 1500);
	assert(servo_set_angle(&servo, 30.0f) == 0 && written_us == 1833);
	assert(servo_set_angle(&servo, -5.0f) == 0 && written_us == 2000);

	/* trim moves every angle, then the limits clamp */
	c = plain;
	c.trim_us = 40;
	assert(servo_set_calibration(&servo, &c) == 0);
	assert(servo_set_angle(&servo, 90.0f) == 0 && written_us == 1540);
	assert(servo_set_angle(&servo, 0.0f) == 0 && written_us == 1040);
	assert(servo_set_angle(&servo, 180.0f) == 0 && written_us == 2000);
	c.trim_us = -40;
	assert(servo_set_calibration(&servo, &c) == 0);
	assert(servo_set_angle(&servo, 90.0f) == 0 && written_us == 1460);
	assert(servo_set_angle(&servo, 0.0f) == 0 && written_us == 1000);
	assert(servo_set_angle(&servo, 180.0f) == 0 && written_us == 1960);

	/* both: invert first, then trim */
	c.invert = true;
	assert(servo_set_calibration(&servo, &c) == 0);
	assert(servo_set_angle(&servo, 0.0f) == 0 && written_us == 1960);
	assert(servo_set_angle(&servo, 180.0f) == 0 && written_us == 1000);

	/* a 270-degree servo with its own pulses */
	c = (struct servo_calibration){
		.min_us = 600, .max_us = 2400, .range_deg = 270.0f,
	};
	assert(servo_set_calibration(&servo, &c) == 0);
	assert(servo_set_angle(&servo, 135.0f) == 0 && written_us == 1500);
	assert(servo_set_angle(&servo, 270.0f) == 0 && written_us == 2400);
}

static void test_pulse(void)
{
	struct servo_dev servo = fresh();
	struct servo_calibration c = plain;

	c.trim_us = 100;
	c.invert = true;
	assert(servo_set_calibration(&servo, &c) == 0);

	/* raw: no trim, no invert, clamped to the limits */
	assert(servo_set_pulse_us(&servo, 1234) == 0 && written_us == 1234);
	assert(servo_set_pulse_us(&servo, 500) == 0 && written_us == 1000);
	assert(servo_set_pulse_us(&servo, 2500) == 0 && written_us == 2000);
	assert(servo.pulse_us == 2000);
	assert(servo_set_pulse_us(NULL, 1500) == -EINVAL);
}

/* A backend's error passes through, and the pulse on record stays. */
static void test_backend_errors(void)
{
	struct servo_dev servo = fresh();

	assert(servo_set_calibration(&servo, &plain) == 0);
	assert(servo_set_angle(&servo, 90.0f) == 0);
	write_ret = -EIO;
	assert(servo_set_angle(&servo, 0.0f) == -EIO);
	assert(servo_set_pulse_us(&servo, 1200) == -EIO);
	assert(servo.pulse_us == 1500);
	write_ret = 0;

	stop_ret = -ETIMEDOUT;
	assert(servo_stop(&servo) == -ETIMEDOUT && servo.pulse_us == 1500);
	stop_ret = 0;
	assert(servo_stop(&servo) == 0 && servo.pulse_us == 0);
	assert(servo_stop(NULL) == -EINVAL);
}

/* servo_math.h on its own: the clamp at both ends and in between. */
static void test_math(void)
{
	struct servo_calibration c = plain;

	assert(servo_clamp_us(-5, 1000, 2000) == 1000);
	assert(servo_clamp_us(999, 1000, 2000) == 1000);
	assert(servo_clamp_us(1000, 1000, 2000) == 1000);
	assert(servo_clamp_us(2001, 1000, 2000) == 2000);
	assert(servo_clamp_us(70000, 1000, 2000) == 2000);
	assert(servo_calibration_valid(&c));
	c.min_us = 0;
	assert(servo_calibration_valid(&c));
	assert(servo_angle_to_us(0.0f, &c) == 0);
	/* the widest the types allow, no overflow */
	c = (struct servo_calibration){
		.min_us = 0, .max_us = UINT16_MAX, .range_deg = 1.0f,
		.trim_us = INT16_MAX,
	};
	assert(servo_calibration_valid(&c));
	assert(servo_angle_to_us(1.0f, &c) == UINT16_MAX);
	assert(servo_angle_to_us(0.0f, &c) == INT16_MAX);
}

int main(void)
{
	test_calibration_refused();
	test_uncalibrated();
	test_angle();
	test_invert_and_trim();
	test_pulse();
	test_backend_errors();
	test_math();
	printf("test_core: all passed\n");
	return 0;
}
