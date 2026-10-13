/*
 * servo -- a small Linux-IIO-style subsystem for hobby RC servos on ESP-IDF
 * and the STM32 HAL.
 *
 * A hobby servo is told where to go by the width of a pulse repeated at a
 * fixed rate; the controller inside it drives the motor until its
 * potentiometer agrees. Nothing comes back. Which width is which angle is
 * the servo's own, and no published standard fixes it, so this library
 * holds no servo figures at all: the app measures its servo and gives the
 * calibration below. A servo with none refuses to move.
 *
 * One struct servo_dev per servo. Each backend hands them out:
 *
 *   - servo_pwm.h: one servo on one pin of the MCU's own PWM (an LEDC
 *     channel on ESP-IDF, a TIM channel on the STM32 HAL).
 *   - pca9685.h: sixteen servos on one NXP PCA9685, pca9685_servo().
 *
 * The core maps an angle onto a pulse through the calibration; a backend
 * only puts pulses on a pin. No clock is read and nothing is ramped: every
 * call is a step, which the servo crosses at its own speed.
 *
 * Every int-returning function returns 0 or a negative errno.
 */
#ifndef SERVO_H
#define SERVO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One servo's own numbers, measured by the app. min_us is the pulse at
 * 0 degrees and max_us the pulse at range_deg; between them the mapping is
 * linear. Both are hard limits: no call puts out a pulse outside them, as a
 * servo driven past its mechanical stop pushes against it.
 *
 * trim_us moves the whole mapping, for a horn a spline tooth off; the
 * result is still clamped to min_us..max_us. invert swaps the ends, for a
 * servo mounted mirrored: angle a goes where range_deg - a would.
 */
struct servo_calibration {
	uint16_t min_us;	/* pulse at 0 degrees, before trim */
	uint16_t max_us;	/* pulse at range_deg, before trim */
	float range_deg;	/* travel between them, above 0 */
	int16_t trim_us;	/* added to every angle's pulse */
	bool invert;
};

struct servo_dev;

struct servo_ops {
	/*
	 * Put out pulse_us from the next period on, starting the pulses if
	 * they are stopped. pulse_us is inside the calibration already. A
	 * pulse that does not fit in one period is -EINVAL.
	 */
	int (*write_us)(struct servo_dev *servo, uint16_t pulse_us);
	/* optional: 0 when pulse_us fits in one period; touches nothing */
	int (*check_us)(struct servo_dev *servo, uint16_t pulse_us);
	/* No more pulses: the servo stops holding. */
	int (*stop)(struct servo_dev *servo);
};

struct servo_dev {
	const char *name;		/* "pwm", "pca9685"; log prefix */
	const struct servo_ops *ops;
	void *priv;			/* the backend's per-servo state */
	struct servo_calibration calibration;	/* all 0 until set */
	uint16_t pulse_us;		/* on the pin now; 0 when stopped */
};

/*
 * Check and keep a calibration. -EINVAL for min_us not below max_us, a
 * range_deg not above 0 or not finite, a trim_us as wide as the span or
 * wider, or a max_us that does not fit in one period of the backend. The
 * pulse on the pin is left as it is until the next move.
 */
int servo_set_calibration(struct servo_dev *servo,
			  const struct servo_calibration *calibration);

/*
 * Move to angle_deg, clamped to 0..range_deg, inverted if asked, trimmed,
 * then clamped to min_us..max_us. -EINVAL for an angle that is not finite
 * or a servo with no calibration; the pulse is then left as it was.
 * Returns once the pulse is set, not once the servo is there.
 */
int servo_set_angle(struct servo_dev *servo, float angle_deg);

/*
 * The pulse itself, clamped to min_us..max_us; no trim, no invert. For
 * calibrating and for continuous-rotation servos, where the pulse sets a
 * speed. -EINVAL for a servo with no calibration.
 */
int servo_set_pulse_us(struct servo_dev *servo, uint16_t pulse_us);

/* Stop the pulses. The servo goes slack: a load it held can move it. */
int servo_stop(struct servo_dev *servo);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_H */
