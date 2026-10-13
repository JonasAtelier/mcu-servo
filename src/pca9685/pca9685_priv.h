/* Internals shared inside the component. Not part of the public API. */
#pragma once

#include "servo.h"
#include "servo_priv.h"
#include "pca9685.h"
#include "pca9685_regs.h"

struct pca9685_t {
    struct servo_bus bus;   /* I2C, at the chip's address */
    uint32_t osc_hz;
    int      prescale;
    float    servo_min_us;  /* pca9685_set_angle()'s range, not a servo_dev's */
    float    servo_max_us;
    float    servo_span_deg;
    /* One per output; each one's priv is the chip. */
    struct servo_dev servo[PCA9685_CHANNELS];
};
