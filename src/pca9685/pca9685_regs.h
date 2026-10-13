/*
 * PCA9685 registers and bit fields.
 *
 * Every value here is transcribed from NXP's datasheet, revision 4 --
 * included as docs/pca9685.pdf, Table 4 (register summary), Table 5 (MODE1),
 * Table 6 (MODE2), Table 7 (LEDn_ON/OFF), section 7.1 (addresses) and
 * section 7.3.5 (PRE_SCALE). No other driver was consulted.
 */
#pragma once

#define PCA9685_MODE1        0x00
#define PCA9685_MODE2        0x01
#define PCA9685_SUBADR1      0x02
#define PCA9685_SUBADR2      0x03
#define PCA9685_SUBADR3      0x04
#define PCA9685_ALLCALLADR   0x05

/* LED0_ON_L is 0x06 and each channel takes four consecutive bytes:
 * ON_L, ON_H, OFF_L, OFF_H. LED15_OFF_H is therefore 0x45. */
#define PCA9685_LED0_ON_L    0x06
#define PCA9685_LED_STRIDE   4
#define PCA9685_CHANNELS     16

/* Writing here loads every channel at once. */
#define PCA9685_ALL_LED_ON_L 0xFA

#define PCA9685_PRE_SCALE    0xFE

/* Address pins A5..A0; 0x40 with all of them low (section 7.1.1, Fig 4). */
#define PCA9685_ADDR_DEFAULT 0x40
#define PCA9685_ADDR_MIN     0x40
#define PCA9685_ADDR_MAX     0x7F

/* ---- MODE1 (0x00), Table 5 ---- */
#define PCA9685_ALLCALL      0x01
#define PCA9685_SUB3         0x02
#define PCA9685_SUB2         0x04
#define PCA9685_SUB1         0x08
/*
 * SLEEP defaults to 1: out of reset the oscillator is OFF and nothing moves
 * until it is cleared. It is also the gate on PRE_SCALE -- see the driver.
 */
#define PCA9685_SLEEP        0x10
#define PCA9685_AI           0x20   /* register auto-increment */
#define PCA9685_EXTCLK       0x40   /* sticky: only a reset or power cycle clears it */
#define PCA9685_RESTART      0x80   /* write 1 to clear */

/* ---- MODE2 (0x01), Table 6 ---- */
#define PCA9685_OUTNE_MASK   0x03
#define PCA9685_OUTDRV       0x04   /* 1 = totem pole, 0 = open drain */
#define PCA9685_OCH          0x08
#define PCA9685_INVRT        0x10

/* ---- LEDn_ON_H / LEDn_OFF_H, bit 4, Table 7 ----
 * Full ON and full OFF bypass the comparator entirely. Note the reset default
 * of the full-OFF bit is 1, so every output starts off. */
#define PCA9685_LED_FULL     0x10

/* 12-bit counter: the ON and OFF values run 0..4095. */
#define PCA9685_COUNTS       4096

/* Internal oscillator, section 7.3.5. */
#define PCA9685_OSC_HZ       25000000UL

/* "The hardware forces a minimum value that can be loaded into the PRE_SCALE
 * register at 3", giving 1526 Hz; 0xFF gives 24 Hz. */
#define PCA9685_PRESCALE_MIN 3
#define PCA9685_PRESCALE_MAX 255

/* Reset default, 0x1E, which is 200 Hz at 25 MHz. */
#define PCA9685_PRESCALE_DEFAULT 0x1E

/* "It takes 500 us max for the oscillator to be up and running once SLEEP
 * has been set to logic 0." */
#define PCA9685_OSC_WAKE_US  500
