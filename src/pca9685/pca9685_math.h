/*
 * PWM arithmetic, host testable. All from NXP's datasheet section 7.3.5.
 */
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "pca9685_regs.h"

/*
 * prescale = round(osc_clock / (4096 * update_rate)) - 1
 *
 * The datasheet's own worked example: 25 MHz and 200 Hz gives 30, or 0x1E.
 */
static inline int pca9685_prescale_for(float hz, uint32_t osc_hz)
{
    float p;

    if (!(hz > 0.0f) || osc_hz == 0)
        return -1;

    p = roundf((float)osc_hz / (float)(PCA9685_COUNTS * hz)) - 1.0f;
    if (p < PCA9685_PRESCALE_MIN || p > PCA9685_PRESCALE_MAX)
        return -1;
    return (int)p;
}

/* The frequency a prescale value actually produces -- which is not quite what
 * was asked for, because the prescaler is an integer. */
static inline float pca9685_frequency_of(int prescale, uint32_t osc_hz)
{
    if (prescale < PCA9685_PRESCALE_MIN || prescale > PCA9685_PRESCALE_MAX)
        return 0.0f;
    return (float)osc_hz / (float)(PCA9685_COUNTS * (prescale + 1));
}

/* Register offset of a channel's first byte. */
static inline int pca9685_channel_reg(int ch)
{
    return (ch >= 0 && ch < PCA9685_CHANNELS)
           ? PCA9685_LED0_ON_L + ch * PCA9685_LED_STRIDE : -1;
}

/*
 * A pulse width in microseconds as a 12-bit count, at the frequency in force.
 *
 * Returns -1 if the pulse does not fit inside one period -- asking for 2 ms at
 * 500 Hz is impossible, and silently truncating it would move a servo
 * somewhere unexpected.
 */
static inline int pca9685_us_to_counts(float us, float freq_hz)
{
    float period_us, counts;

    if (!(us >= 0.0f) || !(freq_hz > 0.0f))
        return -1;

    period_us = 1e6f / freq_hz;
    if (us > period_us)
        return -1;

    counts = roundf(us / period_us * (float)PCA9685_COUNTS);
    if (counts > PCA9685_COUNTS - 1)
        counts = PCA9685_COUNTS - 1;
    return (int)counts;
}

static inline float pca9685_counts_to_us(int counts, float freq_hz)
{
    if (counts < 0 || counts >= PCA9685_COUNTS || !(freq_hz > 0.0f))
        return 0.0f;
    return (float)counts / (float)PCA9685_COUNTS * (1e6f / freq_hz);
}

/*
 * Duty cycle 0..1 as a count. 0 and 1 are special: the part has explicit
 * full-OFF and full-ON bits that bypass the comparator, and using a count of
 * 0 or 4095 instead leaves a one-tick glitch every period.
 */
static inline int pca9685_duty_to_counts(float duty)
{
    if (!(duty > 0.0f))
        return 0;
    if (duty >= 1.0f)
        return PCA9685_COUNTS;
    return (int)(duty * (float)PCA9685_COUNTS + 0.5f);
}

/* A servo angle mapped onto its pulse range. Nothing about this is in the
 * datasheet -- servo timing is a convention, and a cheap servo's real range
 * is whatever it happens to be. Treat the defaults as a starting point. */
#define PCA9685_SERVO_MIN_US  500.0f
#define PCA9685_SERVO_MAX_US  2500.0f

static inline float pca9685_angle_to_us(float deg, float span_deg,
                                        float min_us, float max_us)
{
    if (!(span_deg > 0.0f))
        return min_us;
    if (deg < 0.0f)
        deg = 0.0f;
    if (deg > span_deg)
        deg = span_deg;
    return min_us + (max_us - min_us) * (deg / span_deg);
}
