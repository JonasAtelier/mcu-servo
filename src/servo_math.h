/*
 * The angle-to-pulse mapping, pure, so the host tests can assert it
 * without a backend. No figure here is a servo's: every number comes from
 * the app's struct servo_calibration.
 */
#ifndef SERVO_MATH_H
#define SERVO_MATH_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "servo.h"

/* us clamped to min_us..max_us: the calibration's hard limits. */
static inline uint16_t servo_clamp_us(int32_t us, uint16_t min_us,
				      uint16_t max_us)
{
	if (us < min_us)
		return min_us;
	return us > max_us ? max_us : (uint16_t)us;
}

/*
 * min_us below max_us, range_deg above 0 and finite, and a trim narrower
 * than the span, so that some angle still lands strictly inside the limits.
 */
static inline bool servo_calibration_valid(const struct servo_calibration *c)
{
	int32_t span = (int32_t)c->max_us - c->min_us;

	return span > 0 && c->range_deg > 0.0f && isfinite(c->range_deg) &&
	       c->trim_us > -span && c->trim_us < span;
}

/*
 * angle_deg, finite, onto a pulse: clamped to 0..range_deg, inverted if
 * asked, linear from min_us at 0 to max_us at range_deg, rounded to the
 * nearest microsecond, trimmed, then clamped to min_us..max_us.
 */
static inline uint16_t servo_angle_to_us(float angle_deg,
					 const struct servo_calibration *c)
{
	float span = (float)(c->max_us - c->min_us);
	float pulse;

	if (angle_deg < 0.0f)
		angle_deg = 0.0f;
	if (angle_deg > c->range_deg)
		angle_deg = c->range_deg;
	if (c->invert)
		angle_deg = c->range_deg - angle_deg;
	pulse = (float)c->min_us + angle_deg / c->range_deg * span;
	return servo_clamp_us((int32_t)(pulse + 0.5f) + c->trim_us,
			      c->min_us, c->max_us);
}

#endif /* SERVO_MATH_H */
