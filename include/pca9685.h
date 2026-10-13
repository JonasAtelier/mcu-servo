/*
 * pca9685 -- ESP-IDF driver for the NXP PCA9685 16-channel 12-bit PWM driver.
 *
 * Sixteen independent PWM outputs over I2C, all sharing one frequency. The
 * usual reason to want it: an ESP32 has 16 LEDC channels in total, and a
 * walking robot wants more servos than that.
 *
 * Every register value comes from NXP's datasheet revision 4, included as
 * docs/pca9685.pdf. Nothing here has been run against a physical chip.
 */
/*
 * In mcu-servo: the same calls on the STM32 HAL, where they return 0 or a
 * negative errno and the chip hangs off an I2C handle the app set up; and
 * pca9685_servo(), each output as a struct servo_dev. pca9685_init() now
 * probes the address first, and an I2C error reaches the app as the code
 * ESP-IDF gave it; see docs/usage.md.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "driver/i2c_master.h"
#include "esp_err.h"
#else
#include CMSIS_device_header
#ifndef HAL_I2C_MODULE_ENABLED
#error "pca9685.h needs the HAL I2C module: #define HAL_I2C_MODULE_ENABLED in the app's stm32<family>xx_hal_conf.h, with stm32<family>xx_hal_i2c.c in the build"
#endif
#endif

#include "servo.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCA9685_MAX_CHANNELS 16

typedef struct pca9685_t *pca9685_handle_t;

typedef struct {
    uint8_t  address;      /* 0 selects 0x40 */
    uint32_t scl_hz;       /* 0 selects 400 kHz */
    float    frequency_hz; /* 24 to 1526; 0 selects 50 Hz, which suits servos */
    bool     totem_pole;   /* true for a push-pull output, false for open drain */
    bool     invert;       /* for a driver stage that inverts */
    uint32_t ext_clock_hz; /* 0 uses the internal 25 MHz oscillator */
} pca9685_config_t;

#ifdef ESP_PLATFORM
/*
 * Reset, set the frequency, wake the oscillator and leave every output off.
 *
 * ESP_ERR_NOT_FOUND if nothing answers, or if what answers does not store
 * what is written to it -- the part has no identity register.
 * ESP_ERR_INVALID_ARG for an address outside 0x40..0x7F, or a frequency the
 * prescaler cannot reach.
 */
esp_err_t pca9685_init(i2c_master_bus_handle_t bus, const pca9685_config_t *cfg,
                       pca9685_handle_t *out);
esp_err_t pca9685_del(pca9685_handle_t dev);

/*
 * Raw 12-bit control: the counts at which the output turns on and off within
 * each period. Both run 0..4095. Staggering the on-times across channels is
 * how you avoid sixteen outputs switching on the same edge and browning out
 * the supply.
 */
esp_err_t pca9685_set_raw(pca9685_handle_t dev, int channel, uint16_t on,
                          uint16_t off);

/* Duty cycle, 0.0 to 1.0. Exactly 0 and 1 use the part's full-OFF and full-ON
 * bits rather than a count, which avoids a one-tick glitch every period. */
esp_err_t pca9685_set_duty(pca9685_handle_t dev, int channel, float duty);

/* A pulse of a given width, which is what servos and ESCs actually want.
 * ESP_ERR_INVALID_ARG if the pulse is longer than one period. */
esp_err_t pca9685_set_pulse_us(pca9685_handle_t dev, int channel, float us);

/*
 * A servo angle, using the pulse range set by pca9685_set_servo_range().
 *
 * Servo timing is a convention, not a standard: the defaults here are
 * 500-2500 us over 180 degrees, and a given servo's real range may be
 * anything. Measure yours.
 */
esp_err_t pca9685_set_angle(pca9685_handle_t dev, int channel, float degrees);
void pca9685_set_servo_range(pca9685_handle_t dev, float min_us, float max_us,
                             float span_degrees);

/* Turn one channel fully off, or all of them at once. Worth having as its own
 * call: it is what you want in an error path. */
esp_err_t pca9685_off(pca9685_handle_t dev, int channel);
esp_err_t pca9685_all_off(pca9685_handle_t dev);

/* Change the PWM frequency. Every channel shares it, so this moves all
 * sixteen -- and any pulse widths already set now mean different duty
 * cycles. Re-issue them afterwards. */
esp_err_t pca9685_set_frequency(pca9685_handle_t dev, float hz);

/* The frequency actually produced, which differs from what was asked for
 * because the prescaler is an integer. */
float pca9685_frequency(pca9685_handle_t dev);

/* Oscillator off, outputs held. About 500 us to come back. */
esp_err_t pca9685_sleep(pca9685_handle_t dev, bool sleep);

esp_err_t pca9685_reg_read(pca9685_handle_t dev, uint8_t reg, uint8_t *value);
esp_err_t pca9685_reg_write(pca9685_handle_t dev, uint8_t reg, uint8_t value);

#else
/*
 * The STM32 HAL: the same calls, returning 0 or a negative errno. bus is an
 * I2C handle the app has HAL_I2C_Init()ed; scl_hz is the handle's and is
 * ignored. -ENXIO when nothing answers, -ENODEV when what answers does not
 * store what is written to it.
 */
int pca9685_init(I2C_HandleTypeDef *bus, const pca9685_config_t *cfg,
                 pca9685_handle_t *out);
int pca9685_del(pca9685_handle_t dev);
int pca9685_set_raw(pca9685_handle_t dev, int channel, uint16_t on,
                    uint16_t off);
int pca9685_set_duty(pca9685_handle_t dev, int channel, float duty);
int pca9685_set_pulse_us(pca9685_handle_t dev, int channel, float us);
int pca9685_set_angle(pca9685_handle_t dev, int channel, float degrees);
void pca9685_set_servo_range(pca9685_handle_t dev, float min_us, float max_us,
                             float span_degrees);
int pca9685_off(pca9685_handle_t dev, int channel);
int pca9685_all_off(pca9685_handle_t dev);
int pca9685_set_frequency(pca9685_handle_t dev, float hz);
float pca9685_frequency(pca9685_handle_t dev);
int pca9685_sleep(pca9685_handle_t dev, bool sleep);
int pca9685_reg_read(pca9685_handle_t dev, uint8_t reg, uint8_t *value);
int pca9685_reg_write(pca9685_handle_t dev, uint8_t reg, uint8_t value);
#endif

/*
 * Output channel as a struct servo_dev, to calibrate and move through
 * servo.h; NULL for a NULL handle or a channel outside 0..15. Valid until
 * pca9685_del(). Each starts uncalibrated. Its pulses follow the frequency
 * in force, as pca9685_set_pulse_us() does; after pca9685_set_frequency(),
 * move it again. The calls above reach the same outputs around it: a
 * servo_dev does not see what pca9685_off() or pca9685_set_angle() did.
 */
struct servo_dev *pca9685_servo(pca9685_handle_t dev, int channel);

#ifdef __cplusplus
}
#endif
