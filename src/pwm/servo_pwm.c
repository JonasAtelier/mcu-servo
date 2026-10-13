/* First: on ESP-IDF it brings in sdkconfig.h, which the #if below reads. */
#include "servo_priv.h"

#if CONFIG_SERVO_PWM

#include <string.h>

#include "servo_pwm.h"

static int servo_pwm_write_us(struct servo_dev *servo, uint16_t pulse_us);
static int servo_pwm_check_us(struct servo_dev *servo, uint16_t pulse_us);
static int servo_pwm_stop(struct servo_dev *servo);

static const struct servo_ops servo_pwm_ops = {
	.write_us = servo_pwm_write_us,
	.check_us = servo_pwm_check_us,
	.stop = servo_pwm_stop,
};

int servo_pwm_init(struct servo_pwm *pwm,
		   const struct servo_pwm_config *config)
{
	int ret;

	if (!pwm || !config)
		return -EINVAL;
	memset(pwm, 0, sizeof(*pwm));

#ifdef ESP_PLATFORM
	ret = servo_output_attach(&pwm->output, config->gpio, config->timer,
				  config->channel, config->rate_hz);
#else
	ret = servo_output_attach(&pwm->output, config->tim, config->channel,
				  config->tick_hz);
	if (!ret && !pwm->output.rate_hz)
		ret = -EINVAL;	/* tick_hz below one period */
#endif
	if (ret)
		return ret;

	pwm->servo.name = "pwm";
	pwm->servo.ops = &servo_pwm_ops;
	pwm->servo.priv = pwm;
	return 0;
}

struct servo_dev *servo_pwm_servo(struct servo_pwm *pwm)
{
	return pwm && pwm->servo.ops ? &pwm->servo : NULL;
}

void servo_pwm_del(struct servo_pwm *pwm)
{
	if (!pwm || !pwm->servo.ops)
		return;
	if (pwm->running)
		servo_output_stop(&pwm->output);
	pwm->running = false;
	pwm->servo.ops = NULL;
}

/* The first pulse starts the channel; later ones only change its width. */
static int servo_pwm_write_us(struct servo_dev *servo, uint16_t pulse_us)
{
	struct servo_pwm *pwm = servo->priv;
	int ret;

	if (pwm->running)
		return servo_output_write_us(&pwm->output, pulse_us);
	ret = servo_output_start(&pwm->output, pulse_us);
	if (!ret)
		pwm->running = true;
	return ret;
}

static int servo_pwm_check_us(struct servo_dev *servo, uint16_t pulse_us)
{
	struct servo_pwm *pwm = servo->priv;

	return servo_output_check(&pwm->output, pulse_us);
}

static int servo_pwm_stop(struct servo_dev *servo)
{
	struct servo_pwm *pwm = servo->priv;

	if (pwm->running)
		servo_output_stop(&pwm->output);
	pwm->running = false;
	return 0;
}

#endif /* CONFIG_SERVO_PWM */
