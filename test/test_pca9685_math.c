/*
 * Host-side check of the PWM arithmetic. Expected values come from NXP's
 * datasheet revision 4, section 7.3.5.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "pca9685_math.h"
#include "pca9685_regs.h"

#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-3 * fabs(b) + 1e-6)

static void test_prescale_matches_the_datasheet_example(void)
{
    /* Section 7.3.5's example: 25 MHz at 200 Hz gives 30, which is 0x1E
     * -- and 0x1E is also the register's reset default. */
    assert(pca9685_prescale_for(200.0f, PCA9685_OSC_HZ) == 30);
    assert(pca9685_prescale_for(200.0f, PCA9685_OSC_HZ) == PCA9685_PRESCALE_DEFAULT);

    /* Section 7.3.5 quotes 1526 Hz at prescale 3 and 24 Hz at 255. Both are
     * rounded: the exact figures are 25e6/(4096*4) and 25e6/(4096*256), so
     * the low end is really 23.84 Hz. Assert the arithmetic, not the prose --
     * a driver that clamps at a literal 24 Hz rejects a reachable rate. */
    assert(NEAR(pca9685_frequency_of(PCA9685_PRESCALE_MIN, PCA9685_OSC_HZ),
                25000000.0f / (4096.0f * 4.0f)));
    assert(NEAR(pca9685_frequency_of(PCA9685_PRESCALE_MAX, PCA9685_OSC_HZ),
                25000000.0f / (4096.0f * 256.0f)));
    assert(fabs(pca9685_frequency_of(PCA9685_PRESCALE_MIN, PCA9685_OSC_HZ) - 1526.0f) < 1.0f);
    assert(fabs(pca9685_frequency_of(PCA9685_PRESCALE_MAX, PCA9685_OSC_HZ) - 24.0f) < 0.2f);

    /* Round trip across the reachable span. */
    for (int p = PCA9685_PRESCALE_MIN; p <= PCA9685_PRESCALE_MAX; p++) {
        float f = pca9685_frequency_of(p, PCA9685_OSC_HZ);

        assert(pca9685_prescale_for(f, PCA9685_OSC_HZ) == p);
    }

    /* Outside the span there is nothing to pick, rather than a clamp that
     * would silently run the outputs at the wrong rate. */
    assert(pca9685_prescale_for(23.0f, PCA9685_OSC_HZ) == -1);
    /* ...but anything down to the true floor of 23.84 Hz IS reachable, so a
     * driver clamping at a literal 24 would reject valid rates. */
    assert(pca9685_prescale_for(23.9f, PCA9685_OSC_HZ) >= 0);
    assert(pca9685_prescale_for(25000000.0f / (4096.0f * 256.0f),
                                PCA9685_OSC_HZ) == PCA9685_PRESCALE_MAX);
    assert(pca9685_prescale_for(2000.0f, PCA9685_OSC_HZ) == -1);
    assert(pca9685_prescale_for(0.0f, PCA9685_OSC_HZ) == -1);
    assert(pca9685_prescale_for(-50.0f, PCA9685_OSC_HZ) == -1);
    assert(pca9685_prescale_for(50.0f, 0) == -1);

    assert(pca9685_frequency_of(2, PCA9685_OSC_HZ) == 0.0f);
    assert(pca9685_frequency_of(256, PCA9685_OSC_HZ) == 0.0f);

    /* An external clock moves everything. */
    assert(pca9685_prescale_for(200.0f, 50000000UL) !=
           pca9685_prescale_for(200.0f, PCA9685_OSC_HZ));
}

static void test_channel_registers(void)
{
    /* LED0_ON_L is 0x06 and each channel is four bytes. */
    assert(pca9685_channel_reg(0) == 0x06);
    assert(pca9685_channel_reg(1) == 0x0A);
    assert(pca9685_channel_reg(15) == 0x42);
    /* LED15_OFF_H is therefore 0x45, exactly as Table 4 says. */
    assert(pca9685_channel_reg(15) + 3 == 0x45);

    assert(pca9685_channel_reg(16) == -1);
    assert(pca9685_channel_reg(-1) == -1);
}

static void test_pulse_width(void)
{
    /* At 50 Hz a period is 20 ms, so 1.5 ms is 1.5/20 of 4096 = 307. */
    assert(pca9685_us_to_counts(1500.0f, 50.0f) == 307);
    assert(pca9685_us_to_counts(0.0f, 50.0f) == 0);
    assert(NEAR(pca9685_counts_to_us(307, 50.0f), 1499.0f));

    /* A pulse longer than the period is impossible -- refuse it rather than
     * truncate, which would move a servo somewhere unasked for. */
    /* At 500 Hz a period is exactly 2000 us, so 2000 is the largest that
     * fits and 2500 does not. */
    assert(pca9685_us_to_counts(2000.0f, 500.0f) == PCA9685_COUNTS - 1);
    assert(pca9685_us_to_counts(2500.0f, 500.0f) == -1);
    assert(pca9685_us_to_counts(21000.0f, 50.0f) == -1);
    assert(pca9685_us_to_counts(-5.0f, 50.0f) == -1);
    assert(pca9685_us_to_counts(1500.0f, 0.0f) == -1);

    /* Exactly one full period is the largest count, not an overflow. */
    assert(pca9685_us_to_counts(20000.0f, 50.0f) == PCA9685_COUNTS - 1);

    /* Round trip within a count. */
    for (float us = 500.0f; us <= 2500.0f; us += 37.0f) {
        int c = pca9685_us_to_counts(us, 50.0f);

        assert(c >= 0);
        assert(fabs(pca9685_counts_to_us(c, 50.0f) - us) < 6.0f);
    }
}

static void test_duty(void)
{
    assert(pca9685_duty_to_counts(0.0f) == 0);
    assert(pca9685_duty_to_counts(-1.0f) == 0);
    assert(pca9685_duty_to_counts(1.0f) == PCA9685_COUNTS);
    assert(pca9685_duty_to_counts(2.0f) == PCA9685_COUNTS);
    assert(pca9685_duty_to_counts(0.5f) == 2048);
    assert(pca9685_duty_to_counts(0.25f) == 1024);
    /* Full scale is signalled as 4096, out of the 12-bit range, so the driver
     * knows to use the full-ON bit rather than a count. */
    assert(pca9685_duty_to_counts(1.0f) > PCA9685_COUNTS - 1);
}

static void test_servo_angle(void)
{
    const float lo = 500.0f, hi = 2500.0f;

    assert(NEAR(pca9685_angle_to_us(0.0f, 180.0f, lo, hi), lo));
    assert(NEAR(pca9685_angle_to_us(180.0f, 180.0f, lo, hi), hi));
    assert(NEAR(pca9685_angle_to_us(90.0f, 180.0f, lo, hi), 1500.0f));

    /* Out of range clamps rather than driving a servo past its stop. */
    assert(NEAR(pca9685_angle_to_us(-20.0f, 180.0f, lo, hi), lo));
    assert(NEAR(pca9685_angle_to_us(300.0f, 180.0f, lo, hi), hi));

    /* A different span rescales it. */
    assert(NEAR(pca9685_angle_to_us(135.0f, 270.0f, lo, hi), 1500.0f));
    /* A nonsense span must not divide by zero. */
    assert(NEAR(pca9685_angle_to_us(90.0f, 0.0f, lo, hi), lo));
}

static void test_register_constants(void)
{
    /* Table 4. */
    assert(PCA9685_MODE1 == 0x00 && PCA9685_MODE2 == 0x01);
    assert(PCA9685_LED0_ON_L == 0x06 && PCA9685_LED_STRIDE == 4);
    assert(PCA9685_ALL_LED_ON_L == 0xFA && PCA9685_PRE_SCALE == 0xFE);
    assert(PCA9685_CHANNELS == 16 && PCA9685_COUNTS == 4096);
    /* Table 5. */
    assert(PCA9685_SLEEP == 0x10 && PCA9685_AI == 0x20);
    assert(PCA9685_EXTCLK == 0x40 && PCA9685_RESTART == 0x80);
    assert(PCA9685_LED_FULL == 0x10);
    assert(PCA9685_OSC_HZ == 25000000UL);
    assert(PCA9685_PRESCALE_MIN == 3 && PCA9685_PRESCALE_MAX == 255);
}

int main(void)
{
    test_prescale_matches_the_datasheet_example();
    test_channel_registers();
    test_pulse_width();
    test_duty();
    test_servo_angle();
    test_register_constants();
    printf("all pca9685 math checks passed\n");
    return 0;
}
