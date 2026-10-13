/*
 * The PWM backend on ESP-IDF: servo_pwm.c through src/servo_port_esp.c
 * against a fake LEDC that keeps, per timer and per channel, what the real
 * one would. The shared cases are ../pwm_cases.h; the LEDC settings and the
 * shared-timer rule are checked here.
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/ledc.h"

#include "servo.h"
#include "servo_pwm.h"

#define PIN		17
#define TIMERS		4
#define CHANNELS	4

static bool ledc_used;			/* any LEDC call has set it up */
static uint32_t timer_hz[TIMERS];	/* 0: never configured */
static int timer_configs;		/* calls to ledc_timer_config() */
static ledc_timer_config_t timer;	/* the last timer config */
static ledc_channel_config_t channels[CHANNELS];
static bool running[CHANNELS];
static uint32_t duty[CHANNELS];		/* on the pin */
static uint32_t pending[CHANNELS];	/* set, not yet updated */
static uint32_t idle_level = 9;
static bool fail_next;

static esp_err_t fail(void)
{
	fail_next = false;
	return ESP_FAIL;
}

/* As ledc.c:1112-1133: an error code as the rate before any LEDC use. */
uint32_t ledc_get_freq(ledc_mode_t mode, ledc_timer_t t)
{
	assert(mode == LEDC_LOW_SPEED_MODE);
	if (!ledc_used)
		return ESP_ERR_INVALID_STATE;
	return timer_hz[t];
}

esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf)
{
	if (fail_next)
		return fail();
	timer = *timer_conf;
	ledc_used = true;
	timer_hz[timer.timer_num] = timer.freq_hz;
	timer_configs++;
	return ESP_OK;
}

esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf)
{
	ledc_channel_t ch = ledc_conf->channel;

	if (fail_next)
		return fail();
	/* The channel's timer is running: the attach saw to it. */
	assert(ledc_used && timer_hz[ledc_conf->timer_sel]);
	channels[ch] = *ledc_conf;
	duty[ch] = pending[ch] = ledc_conf->duty;
	running[ch] = true;
	return ESP_OK;
}

esp_err_t ledc_set_duty(ledc_mode_t mode, ledc_channel_t ch, uint32_t value)
{
	assert(mode == LEDC_LOW_SPEED_MODE && running[ch]);
	if (fail_next)
		return fail();
	pending[ch] = value;
	return ESP_OK;
}

esp_err_t ledc_update_duty(ledc_mode_t mode, ledc_channel_t ch)
{
	assert(mode == LEDC_LOW_SPEED_MODE && running[ch]);
	duty[ch] = pending[ch];
	return ESP_OK;
}

esp_err_t ledc_stop(ledc_mode_t mode, ledc_channel_t ch, uint32_t level)
{
	assert(mode == LEDC_LOW_SPEED_MODE);
	running[ch] = false;
	idle_level = level;
	return ESP_OK;
}

static void config_at(struct servo_pwm_config *config, uint32_t rate_hz)
{
	/* each case starts from a fresh LEDC, so its rate is free */
	memset(timer_hz, 0, sizeof(timer_hz));
	*config = (struct servo_pwm_config){
		.gpio = PIN, .timer = LEDC_TIMER_1,
		.channel = LEDC_CHANNEL_2, .rate_hz = rate_hz,
	};
}

static uint32_t us_of(ledc_channel_t ch)
{
	return (uint32_t)lround((double)duty[ch] * 1e6 /
				((double)timer_hz[channels[ch].timer_sel] *
				 16384.0));
}

static uint32_t out_us(void)
{
	return us_of(LEDC_CHANNEL_2);
}

static bool out_running(void)
{
	return running[LEDC_CHANNEL_2];
}

#include "../pwm_cases.h"

/* What the LEDC is told: low-speed mode, 14 bits, the rate, the pin. */
static void test_ledc(void)
{
	struct servo_pwm pwm;
	struct servo_pwm_config config;
	struct servo_dev *servo;

	config_at(&config, 50);
	assert(servo_pwm_init(&pwm, &config) == 0);
	assert(timer.speed_mode == LEDC_LOW_SPEED_MODE);
	assert(timer.duty_resolution == LEDC_TIMER_14_BIT);
	assert(timer.timer_num == LEDC_TIMER_1 && timer.freq_hz == 50);
	assert(timer.clk_cfg == LEDC_AUTO_CLK && !timer.deconfigure);
	servo = servo_pwm_servo(&pwm);
	assert(servo_set_calibration(servo, &calibration) == 0);
	assert(servo_set_angle(servo, 90.0f) == 0);
	assert(channels[2].gpio_num == PIN);
	assert(channels[2].channel == LEDC_CHANNEL_2);
	assert(channels[2].speed_mode == LEDC_LOW_SPEED_MODE);
	assert(channels[2].timer_sel == LEDC_TIMER_1);
	assert(channels[2].intr_type == LEDC_INTR_DISABLE);
	assert(channels[2].hpoint == 0 && !channels[2].flags.output_invert);
	/* 1500 us of 20000: 0.075 * 16384 = 1228.8 -> 1229 */
	assert(duty[2] == 1229);
	/* 1000 us: 0.05 * 16384 = 819.2 -> 819 */
	assert(servo_set_angle(servo, 0.0f) == 0 && duty[2] == 819);
	/* 2000 us: 0.1 * 16384 = 1638.4 -> 1638 */
	assert(servo_set_angle(servo, 180.0f) == 0 && duty[2] == 1638);
	servo_pwm_del(&pwm);
	assert(idle_level == 0);

	/* No rate at all, no pin, or no such channel: nothing configured */
	timer_configs = 0;
	config_at(&config, 0);
	assert(servo_pwm_init(&pwm, &config) == -EINVAL);
	config_at(&config, 50);
	config.gpio = -1;
	assert(servo_pwm_init(&pwm, &config) == -EINVAL);
	config_at(&config, 50);
	config.channel = LEDC_CHANNEL_MAX;
	assert(servo_pwm_init(&pwm, &config) == -EINVAL);
	config.channel = (ledc_channel_t)-1;
	assert(servo_pwm_init(&pwm, &config) == -EINVAL);
	assert(timer_configs == 0 && servo_pwm_servo(&pwm) == NULL);
}

/*
 * The shared timer: configured once, never again under a running servo,
 * and refused at another rate.
 */
static void test_shared_timer(void)
{
	struct servo_pwm first, second, other;
	struct servo_pwm_config config;

	/* Before any LEDC use ledc_get_freq() reads ESP_ERR_INVALID_STATE */
	ledc_used = false;
	timer_configs = 0;
	config_at(&config, 50);
	assert(servo_pwm_init(&first, &config) == 0);
	assert(timer_configs == 1 && timer_hz[LEDC_TIMER_1] == 50);
	assert(servo_set_calibration(servo_pwm_servo(&first),
				     &calibration) == 0);
	assert(servo_set_angle(servo_pwm_servo(&first), 90.0f) == 0);

	/* A second servo at the same rate binds without touching the timer */
	config.channel = LEDC_CHANNEL_3;
	config.gpio = PIN + 1;
	assert(servo_pwm_init(&second, &config) == 0);
	assert(timer_configs == 1);
	assert(servo_set_calibration(servo_pwm_servo(&second),
				     &calibration) == 0);
	assert(servo_set_angle(servo_pwm_servo(&second), 0.0f) == 0);
	assert(us_of(LEDC_CHANNEL_2) == 1500 && us_of(LEDC_CHANNEL_3) == 1000);

	/* Another rate on that timer: -EBUSY, nothing touched */
	config.channel = LEDC_CHANNEL_0;
	config.rate_hz = 333;
	assert(servo_pwm_init(&other, &config) == -EBUSY);
	assert(timer_configs == 1 && timer_hz[LEDC_TIMER_1] == 50);
	assert(servo_pwm_servo(&other) == NULL);

	/* ... but on a timer of its own it runs */
	config.timer = LEDC_TIMER_2;
	assert(servo_pwm_init(&other, &config) == 0);
	assert(timer_configs == 2 && timer_hz[LEDC_TIMER_2] == 333);

	/* A timer configured once and then never touched reads 0: configure */
	config.timer = LEDC_TIMER_3;
	config.rate_hz = 50;
	assert(ledc_used && timer_hz[LEDC_TIMER_3] == 0);
	assert(servo_pwm_init(&other, &config) == 0);
	assert(timer_configs == 3);

	/* Deleting a servo leaves its timer running for the others */
	servo_pwm_del(&first);
	assert(timer_hz[LEDC_TIMER_1] == 50 && running[LEDC_CHANNEL_3]);
	servo_pwm_del(&second);
	servo_pwm_del(&other);

	/* The timer config failing: its error, nothing bound */
	ledc_used = false;
	config_at(&config, 50);
	fail_next = true;
	assert(servo_pwm_init(&first, &config) == -EIO);
	assert(servo_pwm_servo(&first) == NULL);
}

int main(void)
{
	run_pwm_cases();
	test_ledc();
	test_shared_timer();
	printf("test_pwm (esp): all passed\n");
	return 0;
}
