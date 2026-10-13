#include <errno.h>
#include <math.h>

#include "servo.h"
#include "servo_math.h"

static bool servo_bound(const struct servo_dev *servo)
{
	return servo && servo->ops && servo->ops->write_us && servo->ops->stop;
}

/* servo_set_calibration() never keeps a max_us of 0. */
static bool servo_calibrated(const struct servo_dev *servo)
{
	return servo->calibration.max_us != 0;
}

static int servo_write(struct servo_dev *servo, uint16_t pulse_us)
{
	int ret = servo->ops->write_us(servo, pulse_us);

	if (!ret)
		servo->pulse_us = pulse_us;
	return ret;
}

int servo_set_calibration(struct servo_dev *servo,
			  const struct servo_calibration *calibration)
{
	int ret;

	if (!servo_bound(servo) || !calibration ||
	    !servo_calibration_valid(calibration))
		return -EINVAL;
	/* The widest pulse must fit in a period, or max_us is unreachable. */
	if (servo->ops->check_us) {
		ret = servo->ops->check_us(servo, calibration->max_us);
		if (ret)
			return ret;
	}
	servo->calibration = *calibration;
	return 0;
}

int servo_set_angle(struct servo_dev *servo, float angle_deg)
{
	if (!servo_bound(servo) || !servo_calibrated(servo) ||
	    !isfinite(angle_deg))
		return -EINVAL;
	return servo_write(servo, servo_angle_to_us(angle_deg,
						    &servo->calibration));
}

int servo_set_pulse_us(struct servo_dev *servo, uint16_t pulse_us)
{
	if (!servo_bound(servo) || !servo_calibrated(servo))
		return -EINVAL;
	return servo_write(servo, servo_clamp_us(pulse_us,
						 servo->calibration.min_us,
						 servo->calibration.max_us));
}

int servo_stop(struct servo_dev *servo)
{
	int ret;

	if (!servo_bound(servo))
		return -EINVAL;
	ret = servo->ops->stop(servo);
	if (!ret)
		servo->pulse_us = 0;
	return ret;
}
