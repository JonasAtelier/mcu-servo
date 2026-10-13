/* First: on ESP-IDF it brings in sdkconfig.h, which the #if below reads. */
#include "servo_priv.h"

#if CONFIG_SERVO_PCA9685

#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "servo_log.h"
#include "pca9685.h"
#include "pca9685_math.h"
#include "pca9685_priv.h"
#include "pca9685_regs.h"

static const char *TAG = "pca9685";

#define DEFAULT_SCL_HZ  400000
#define DEFAULT_FREQ_HZ 50.0f
#define I2C_TIMEOUT_MS  100

#define TRY(expr) do { int _e = (expr); if (_e) return _e; } while (0)

/*
 * The driver works in negative errno; each public call converts once, with
 * SERVO_HANDLE_RET(), to the esp_err_t esp-pca9685-pwm returned on ESP-IDF.
 * On the HAL the calls return the errno itself.
 */

/* ---------------------------------------------------------------- I2C ---- */

/* Register address, repeated start, one byte (section 7.2: the Control
 * register selects which register a read returns; Fig 22 for the read). */
static int reg_read(pca9685_handle_t d, uint8_t reg, uint8_t *value)
{
    return d->bus.ops->transfer(&d->bus, reg, NULL, 0, value, 1);
}

static int reg_write(pca9685_handle_t d, uint8_t reg, uint8_t value)
{
    return d->bus.ops->transfer(&d->bus, reg, &value, 1, NULL, 0);
}

servo_handle_ret pca9685_reg_read(pca9685_handle_t d, uint8_t reg, uint8_t *value)
{
    if (!d || !value)
        return SERVO_HANDLE_RET(-EINVAL);

    return SERVO_HANDLE_RET(reg_read(d, reg, value));
}

servo_handle_ret pca9685_reg_write(pca9685_handle_t d, uint8_t reg, uint8_t value)
{
    if (!d)
        return SERVO_HANDLE_RET(-EINVAL);

    return SERVO_HANDLE_RET(reg_write(d, reg, value));
}

/* Four bytes at once: ON_L, ON_H, OFF_L, OFF_H. Auto-increment is enabled at
 * init so the part walks the register pointer for us (Fig 21, Fig 23). */
static int write_channel(pca9685_handle_t d, int reg, uint16_t on, uint16_t off)
{
    uint8_t b[4] = {
        (uint8_t)(on & 0xFF), (uint8_t)(on >> 8),
        (uint8_t)(off & 0xFF), (uint8_t)(off >> 8),
    };

    return d->bus.ops->transfer(&d->bus, (uint8_t)reg, b, sizeof(b), NULL, 0);
}

/* -------------------------------------------------------- frequency ------ */

/*
 * PRE_SCALE can only be written while SLEEP is set -- "Writes to PRE_SCALE
 * register are blocked when SLEEP bit is logic 0" (Table 4, note 1). So the
 * sequence is: sleep, write, wake, wait for the oscillator, and only then
 * restart (section 7.3.1.1).
 */
static int set_frequency(pca9685_handle_t d, float hz)
{
    int prescale;
    uint8_t mode1 = 0;

    prescale = pca9685_prescale_for(hz, d->osc_hz);
    if (prescale < 0) {
        servo_err(TAG, "%.1f Hz is out of reach: the prescaler spans %.0f to %.0f Hz",
                  (double)hz,
                  (double)pca9685_frequency_of(PCA9685_PRESCALE_MAX, d->osc_hz),
                  (double)pca9685_frequency_of(PCA9685_PRESCALE_MIN, d->osc_hz));
        return -EINVAL;
    }

    TRY(reg_read(d, PCA9685_MODE1, &mode1));
    TRY(reg_write(d, PCA9685_MODE1,
                  (uint8_t)((mode1 & ~PCA9685_RESTART) | PCA9685_SLEEP)));
    TRY(reg_write(d, PCA9685_PRE_SCALE, (uint8_t)prescale));
    /* Wake with RESTART written 0: a chip put to sleep while its outputs ran
     * reads RESTART 1, and a 1 written now, before the oscillator has run
     * 500 us, breaks section 7.3.1.1's Remark. */
    TRY(reg_write(d, PCA9685_MODE1,
                  (uint8_t)(mode1 & ~(PCA9685_SLEEP | PCA9685_RESTART))));

    /* The oscillator needs up to 500 us; PWM timings are not guaranteed if
     * the control registers are touched inside that window (Table 5, note 2). */
    servo_delay_us(PCA9685_OSC_WAKE_US);

    /* RESTART is write-1-to-clear and resumes the outputs that were running. */
    TRY(reg_write(d, PCA9685_MODE1,
                  (uint8_t)((mode1 & ~PCA9685_SLEEP) | PCA9685_RESTART)));

    d->prescale = prescale;
    if (fabsf(pca9685_frequency_of(prescale, d->osc_hz) - hz) > 0.02f * hz)
        servo_warn(TAG, "asked %.1f Hz, the integer prescaler gives %.2f Hz",
                   (double)hz, (double)pca9685_frequency_of(prescale, d->osc_hz));
    return 0;
}

servo_handle_ret pca9685_set_frequency(pca9685_handle_t d, float hz)
{
    if (!d)
        return SERVO_HANDLE_RET(-EINVAL);

    return SERVO_HANDLE_RET(set_frequency(d, hz));
}

float pca9685_frequency(pca9685_handle_t d)
{
    return d ? pca9685_frequency_of(d->prescale, d->osc_hz) : 0.0f;
}

/* ----------------------------------------------------------- servo_dev --- */

static int channel_of(const struct servo_dev *servo)
{
    pca9685_handle_t d = servo->priv;

    return (int)(servo - d->servo);
}

/*
 * pulse_us at the frequency in force, as pca9685_set_pulse_us() puts it out:
 * ON at count 0, OFF at the pulse's count. -EINVAL for a pulse longer than
 * one period.
 */
static int servo_write_us(struct servo_dev *servo, uint16_t pulse_us)
{
    pca9685_handle_t d = servo->priv;
    int counts = pca9685_us_to_counts((float)pulse_us, pca9685_frequency(d));

    if (counts < 0)
        return -EINVAL;
    return write_channel(d, pca9685_channel_reg(channel_of(servo)), 0,
                         (uint16_t)counts);
}

static int servo_check_us(struct servo_dev *servo, uint16_t pulse_us)
{
    pca9685_handle_t d = servo->priv;

    return pca9685_us_to_counts((float)pulse_us, pca9685_frequency(d)) < 0
           ? -EINVAL : 0;
}

/* Full OFF, as pca9685_off(): LEDn_OFF_H bit 4 (Table 7). */
static int servo_stop_output(struct servo_dev *servo)
{
    pca9685_handle_t d = servo->priv;

    return write_channel(d, pca9685_channel_reg(channel_of(servo)), 0,
                         PCA9685_LED_FULL << 8);
}

static const struct servo_ops pca9685_servo_ops = {
    .write_us = servo_write_us,
    .check_us = servo_check_us,
    .stop     = servo_stop_output,
};

struct servo_dev *pca9685_servo(pca9685_handle_t d, int channel)
{
    if (!d || pca9685_channel_reg(channel) < 0)
        return NULL;
    return &d->servo[channel];
}

/* -------------------------------------------------------------- init ----- */

static int all_off(pca9685_handle_t d)
{
    /* ALL_LED_* loads every channel in one transaction (Table 4, 0xFA). */
    return write_channel(d, PCA9685_ALL_LED_ON_L, 0, PCA9685_LED_FULL << 8);
}

static int init(servo_i2c_bus bus, const pca9685_config_t *cfg,
                pca9685_handle_t *out)
{
    pca9685_handle_t d;
    uint8_t addr, mode1, mode2 = 0, back = 0;
    float hz;
    int err;

    if (!cfg || !out)
        return -EINVAL;

    addr = cfg->address ? cfg->address : PCA9685_ADDR_DEFAULT;
    if (addr < PCA9685_ADDR_MIN || addr > PCA9685_ADDR_MAX) {
        servo_err(TAG, "address must be 0x40..0x7F, not 0x%02x", addr);
        return -EINVAL;
    }

    hz = cfg->frequency_hz > 0.0f ? cfg->frequency_hz : DEFAULT_FREQ_HZ;

    /* Nothing ACKs at the address: -ENXIO, before anything is allocated. */
    err = servo_i2c_probe(bus, addr, I2C_TIMEOUT_MS);
    if (err) {
        servo_err(TAG, "nothing answers at 0x%02x", addr);
        return err;
    }

    d = calloc(1, sizeof(*d));
    if (!d)
        return -ENOMEM;

    d->osc_hz         = cfg->ext_clock_hz ? cfg->ext_clock_hz : PCA9685_OSC_HZ;
    d->servo_min_us   = PCA9685_SERVO_MIN_US;
    d->servo_max_us   = PCA9685_SERVO_MAX_US;
    d->servo_span_deg = 180.0f;

    err = servo_i2c_attach(&d->bus, bus, addr,
                           cfg->scl_hz ? cfg->scl_hz : DEFAULT_SCL_HZ,
                           I2C_TIMEOUT_MS);
    if (err) {
        free(d);
        return err;
    }

    /*
     * No identity register on this part. SUBADR1 is plain read/write storage,
     * so writing a pattern and reading it back distinguishes a device from a
     * bus that merely acknowledges.
     */
    err = reg_write(d, PCA9685_SUBADR1, 0xE2);
    if (!err)
        err = reg_read(d, PCA9685_SUBADR1, &back);
    if (err)
        goto fail;
    if (back != 0xE2) {
        servo_err(TAG, "no PCA9685 at 0x%02x: wrote 0xE2 to SUBADR1, read 0x%02x",
                  addr, back);
        err = -ENODEV;
        goto fail;
    }
    /* Put the subaddress back to its reset default (Table 4: E2h). */
    err = reg_write(d, PCA9685_SUBADR1, 0xE2);
    if (err)
        goto fail;

    /*
     * Out of reset SLEEP is 1 and the oscillator is off, so a fresh part does
     * nothing at all until it is cleared. Auto-increment is also off, and the
     * four-byte channel writes below depend on it (Table 5).
     */
    mode1 = PCA9685_AI | PCA9685_ALLCALL | PCA9685_SLEEP;
    err = reg_write(d, PCA9685_MODE1, mode1);
    if (err)
        goto fail;

    if (cfg->totem_pole)
        mode2 |= PCA9685_OUTDRV;
    if (cfg->invert)
        mode2 |= PCA9685_INVRT;
    err = reg_write(d, PCA9685_MODE2, mode2);
    if (err)
        goto fail;

    err = set_frequency(d, hz);
    if (err)
        goto fail;

    /* Nothing should twitch on power-up; leave every output off. */
    err = all_off(d);
    if (err)
        goto fail;

    for (int ch = 0; ch < PCA9685_CHANNELS; ch++) {
        d->servo[ch].name = TAG;
        d->servo[ch].ops  = &pca9685_servo_ops;
        d->servo[ch].priv = d;
    }

    servo_info(TAG, "PCA9685 at 0x%02x: %.2f Hz (prescale %d), %s",
               addr, (double)pca9685_frequency(d), d->prescale,
               cfg->totem_pole ? "totem pole" : "open drain");

    *out = d;
    return 0;

fail:
    servo_bus_detach(&d->bus);
    free(d);
    return err;
}

#ifdef ESP_PLATFORM
esp_err_t pca9685_init(i2c_master_bus_handle_t bus, const pca9685_config_t *cfg,
                       pca9685_handle_t *out)
#else
int pca9685_init(I2C_HandleTypeDef *bus, const pca9685_config_t *cfg,
                 pca9685_handle_t *out)
#endif
{
    return SERVO_HANDLE_RET(init(bus, cfg, out));
}

servo_handle_ret pca9685_del(pca9685_handle_t d)
{
    if (!d)
        return SERVO_HANDLE_RET(-EINVAL);

    /* Outputs off before the oscillator stops, or they hold their last
     * state: the "orderly shutdown" of section 7.3.1.1, footnote 1. */
    all_off(d);
    reg_write(d, PCA9685_MODE1, PCA9685_SLEEP);
    servo_bus_detach(&d->bus);
    free(d);
    return SERVO_HANDLE_RET(0);
}

/* ------------------------------------------------------------ outputs ---- */

servo_handle_ret pca9685_set_raw(pca9685_handle_t d, int channel, uint16_t on, uint16_t off)
{
    int reg;

    if (!d)
        return SERVO_HANDLE_RET(-EINVAL);

    reg = pca9685_channel_reg(channel);
    if (reg < 0 || on >= PCA9685_COUNTS || off >= PCA9685_COUNTS)
        return SERVO_HANDLE_RET(-EINVAL);

    return SERVO_HANDLE_RET(write_channel(d, reg, on, off));
}

servo_handle_ret pca9685_set_duty(pca9685_handle_t d, int channel, float duty)
{
    int reg, counts;

    if (!d)
        return SERVO_HANDLE_RET(-EINVAL);

    reg = pca9685_channel_reg(channel);
    if (reg < 0)
        return SERVO_HANDLE_RET(-EINVAL);

    counts = pca9685_duty_to_counts(duty);

    /* The full-ON and full-OFF bits bypass the comparator. Using a count of
     * 0 or 4095 instead leaves a single-tick glitch every period. */
    if (counts <= 0)
        return SERVO_HANDLE_RET(write_channel(d, reg, 0, PCA9685_LED_FULL << 8));
    if (counts >= PCA9685_COUNTS)
        return SERVO_HANDLE_RET(write_channel(d, reg, PCA9685_LED_FULL << 8, 0));

    return SERVO_HANDLE_RET(write_channel(d, reg, 0, (uint16_t)counts));
}

servo_handle_ret pca9685_set_pulse_us(pca9685_handle_t d, int channel, float us)
{
    int counts;

    if (!d)
        return SERVO_HANDLE_RET(-EINVAL);

    counts = pca9685_us_to_counts(us, pca9685_frequency(d));
    if (counts < 0) {
        servo_err(TAG, "%.0f us does not fit in a period at %.1f Hz",
                  (double)us, (double)pca9685_frequency(d));
        return SERVO_HANDLE_RET(-EINVAL);
    }
    return pca9685_set_raw(d, channel, 0, (uint16_t)counts);
}

void pca9685_set_servo_range(pca9685_handle_t d, float min_us, float max_us,
                             float span_degrees)
{
    if (!d || !(span_degrees > 0.0f) || !(max_us > min_us))
        return;
    d->servo_min_us   = min_us;
    d->servo_max_us   = max_us;
    d->servo_span_deg = span_degrees;
}

servo_handle_ret pca9685_set_angle(pca9685_handle_t d, int channel, float degrees)
{
    if (!d)
        return SERVO_HANDLE_RET(-EINVAL);

    return pca9685_set_pulse_us(d, channel,
                                pca9685_angle_to_us(degrees, d->servo_span_deg,
                                                    d->servo_min_us,
                                                    d->servo_max_us));
}

servo_handle_ret pca9685_off(pca9685_handle_t d, int channel)
{
    int reg;

    if (!d)
        return SERVO_HANDLE_RET(-EINVAL);

    reg = pca9685_channel_reg(channel);
    if (reg < 0)
        return SERVO_HANDLE_RET(-EINVAL);

    return SERVO_HANDLE_RET(write_channel(d, reg, 0, PCA9685_LED_FULL << 8));
}

servo_handle_ret pca9685_all_off(pca9685_handle_t d)
{
    if (!d)
        return SERVO_HANDLE_RET(-EINVAL);

    return SERVO_HANDLE_RET(all_off(d));
}

servo_handle_ret pca9685_sleep(pca9685_handle_t d, bool sleep)
{
    uint8_t mode1 = 0;
    int err;

    if (!d)
        return SERVO_HANDLE_RET(-EINVAL);

    err = reg_read(d, PCA9685_MODE1, &mode1);
    if (err)
        return SERVO_HANDLE_RET(err);
    /* RESTART is written 0 either way, as in set_frequency(): it may read 1,
     * and a 1 written back would clear the pending restart, or break the
     * 500 us rule on waking (section 7.3.1.1). */
    mode1 = (uint8_t)(sleep ? ((mode1 & ~PCA9685_RESTART) | PCA9685_SLEEP)
                            : (mode1 & ~(PCA9685_SLEEP | PCA9685_RESTART)));
    err = reg_write(d, PCA9685_MODE1, mode1);
    if (err)
        return SERVO_HANDLE_RET(err);

    if (!sleep) {
        servo_delay_us(PCA9685_OSC_WAKE_US);
        err = reg_write(d, PCA9685_MODE1, (uint8_t)(mode1 | PCA9685_RESTART));
    }
    return SERVO_HANDLE_RET(err);
}

#endif /* CONFIG_SERVO_PCA9685 */
