/*
 * The PWM backend on the STM32 HAL: servo_pwm.c through
 * src/servo_port_stm.c against a fake TIM whose compare registers are the
 * pin. The shared cases are ../pwm_cases.h; the rate read back from ARR and
 * the compares are checked here.
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "stm32_hal_stub.h"

#include "servo.h"
#include "servo_pwm.h"

#define TICK_HZ		1000000u	/* 1 us per count */

static TIM_TypeDef regs;
static TIM_HandleTypeDef tim = { .Instance = &regs };
static bool running;
static uint32_t started_channel;
static bool fail_next;

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t ch)
{
	assert(htim == &tim);
	if (fail_next) {
		fail_next = false;
		return HAL_ERROR;
	}
	started_channel = ch;
	running = true;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *htim, uint32_t ch)
{
	assert(htim == &tim && ch == started_channel);
	/* The port has pulled the compare to 0 before stopping. */
	assert(regs.CCR3 == 0);
	running = false;
	return HAL_OK;
}

static void config_at(struct servo_pwm_config *config, uint32_t rate_hz)
{
	regs.ARR = TICK_HZ / rate_hz - 1;
	regs.CCMR2 = TIM_CCMR2_OC3PE;	/* preload on, as CubeMX leaves it */
	*config = (struct servo_pwm_config){
		.tim = &tim, .channel = TIM_CHANNEL_3, .tick_hz = TICK_HZ,
	};
}

static uint32_t out_us(void)
{
	return (uint32_t)((uint64_t)regs.CCR3 * 1000000u / TICK_HZ);
}

static bool out_running(void)
{
	return running;
}

#include "../pwm_cases.h"

/* The rate from ARR, and the compare at other counter clocks. */
static void test_tim(void)
{
	struct servo_pwm pwm;
	struct servo_pwm_config config;
	struct servo_dev *servo;

	/* 50 Hz from a 1 MHz counter: ARR 19999 */
	config_at(&config, 50);
	assert(servo_pwm_init(&pwm, &config) == 0);
	assert(pwm.output.rate_hz == 50);
	servo = servo_pwm_servo(&pwm);
	assert(servo_set_calibration(servo, &calibration) == 0);
	assert(servo_set_angle(servo, 90.0f) == 0);
	assert(started_channel == TIM_CHANNEL_3 && regs.CCR3 == 1500);
	servo_pwm_del(&pwm);

	/* 84 MHz counter, ARR 1679999 (32-bit timer) is 50 Hz: 1.5 ms is
	 * 126000 counts */
	regs.ARR = 1679999;
	config.tick_hz = 84000000;
	assert(servo_pwm_init(&pwm, &config) == 0);
	assert(pwm.output.rate_hz == 50);
	servo = servo_pwm_servo(&pwm);
	assert(servo_set_calibration(servo, &calibration) == 0);
	assert(servo_set_angle(servo, 90.0f) == 0 && regs.CCR3 == 126000);
	servo_pwm_del(&pwm);

	/* A counter clock below one period: a rate of 0, refused */
	regs.ARR = 19999;
	config.tick_hz = 10000;
	assert(servo_pwm_init(&pwm, &config) == -EINVAL);

	/* Refused handles, channels and clocks */
	config_at(&config, 50);
	config.tim = NULL;
	assert(servo_pwm_init(&pwm, &config) == -EINVAL);
	config_at(&config, 50);
	config.channel = 0x10;
	assert(servo_pwm_init(&pwm, &config) == -EINVAL);
	config_at(&config, 50);
	config.tick_hz = 0;
	assert(servo_pwm_init(&pwm, &config) == -EINVAL);
	/* Output-compare preload off: refused, for each channel's own bit */
	{
		static const struct {
			uint32_t channel, ccmr1, ccmr2;
		} preload[] = {
			{ TIM_CHANNEL_1, TIM_CCMR1_OC1PE, 0 },
			{ TIM_CHANNEL_2, TIM_CCMR1_OC2PE, 0 },
			{ TIM_CHANNEL_3, 0, TIM_CCMR2_OC3PE },
			{ TIM_CHANNEL_4, 0, TIM_CCMR2_OC4PE },
		};

		for (size_t i = 0; i < 4; i++) {
			config_at(&config, 50);
			config.channel = preload[i].channel;
			/* every other channel's bit on, this one's off */
			regs.CCMR1 = (TIM_CCMR1_OC1PE | TIM_CCMR1_OC2PE) &
				     ~preload[i].ccmr1;
			regs.CCMR2 = (TIM_CCMR2_OC3PE | TIM_CCMR2_OC4PE) &
				     ~preload[i].ccmr2;
			assert(servo_pwm_init(&pwm, &config) == -EINVAL);
			assert(servo_pwm_servo(&pwm) == NULL);
			regs.CCMR1 |= preload[i].ccmr1;
			regs.CCMR2 |= preload[i].ccmr2;
			assert(servo_pwm_init(&pwm, &config) == 0);
			servo_pwm_del(&pwm);
		}
	}
	/* ARR 0xFFFFFFFF wraps the period to 0 */
	config_at(&config, 50);
	regs.ARR = 0xFFFFFFFFu;
	assert(servo_pwm_init(&pwm, &config) == -EINVAL);
}

int main(void)
{
	run_pwm_cases();
	test_tim();
	printf("test_pwm (stm): all passed\n");
	return 0;
}
