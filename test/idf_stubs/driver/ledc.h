#ifndef LEDC_H
#define LEDC_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Host stub for the ESP-IDF LEDC driver: only what the PWM port uses. Names,
 * fields and values as in IDF v5.5.5's hal/ledc_types.h and driver/ledc.h.
 */

typedef enum {
	LEDC_LOW_SPEED_MODE = 0,
} ledc_mode_t;

typedef enum {
	LEDC_TIMER_0 = 0,
	LEDC_TIMER_1,
	LEDC_TIMER_2,
	LEDC_TIMER_3,
} ledc_timer_t;

typedef enum {
	LEDC_CHANNEL_0 = 0,
	LEDC_CHANNEL_1,
	LEDC_CHANNEL_2,
	LEDC_CHANNEL_3,
	LEDC_CHANNEL_MAX,	/* SOC_LEDC_CHANNEL_NUM in IDF; 4 here */
} ledc_channel_t;

typedef enum {
	LEDC_TIMER_14_BIT = 14,
} ledc_timer_bit_t;

typedef enum {
	LEDC_AUTO_CLK = 0,
} ledc_clk_cfg_t;

typedef enum {
	LEDC_INTR_DISABLE = 0,
} ledc_intr_type_t;

typedef struct {
	ledc_mode_t speed_mode;
	ledc_timer_bit_t duty_resolution;
	ledc_timer_t timer_num;
	uint32_t freq_hz;
	ledc_clk_cfg_t clk_cfg;
	bool deconfigure;
} ledc_timer_config_t;

typedef struct {
	int gpio_num;
	ledc_mode_t speed_mode;
	ledc_channel_t channel;
	ledc_intr_type_t intr_type;
	ledc_timer_t timer_sel;
	uint32_t duty;
	int hpoint;
	struct {
		unsigned int output_invert : 1;
	} flags;
} ledc_channel_config_t;

esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf);
esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf);
esp_err_t ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel,
			uint32_t duty);
esp_err_t ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel);
esp_err_t ledc_stop(ledc_mode_t speed_mode, ledc_channel_t channel,
		    uint32_t idle_level);
/*
 * The rate the timer runs at, 0 for one never configured; before the LEDC
 * has been used at all, ESP_ERR_INVALID_STATE cast to uint32_t
 * (ledc.c:1112-1133).
 */
uint32_t ledc_get_freq(ledc_mode_t speed_mode, ledc_timer_t timer_num);

#endif
