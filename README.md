# mcu-servo

<p align="center">
  <img alt="platform: ESP-IDF | STM32 HAL" src="https://img.shields.io/badge/platform-ESP--IDF%20%7C%20STM32%20HAL-70e1b2?style=flat-square&labelColor=0d2238">
  <img alt="language: C" src="https://img.shields.io/badge/language-C-70e1b2?style=flat-square&labelColor=0d2238">
  <img alt="license: MIT" src="https://img.shields.io/badge/license-MIT-f2bd68?style=flat-square&labelColor=0d2238">
</p>

A small C library for **hobby RC servos** on **ESP-IDF 5.2+** and the
**STM32 HAL**. Each servo is a `struct servo_dev` with its own calibration --
pulse limits, travel, trim, invert -- and the same calls move it whichever
backend puts the pulses out: a pin of the MCU's own PWM, or one of the
sixteen outputs of an NXP PCA9685.

**Scope.** Hobby servos commanded by pulse width, and the PCA9685, which
stays usable as a plain PWM or LED driver. Brushless ESCs are
[mcu-esc](https://github.com/JonasAtelier/mcu-esc)'s. Serial bus servos,
which talk a UART protocol, are not here.

This library replaces `esp-pca9685-pwm`, whose `pca9685.h` it keeps token
for token -- a project that calls `pca9685_init()` builds unchanged -- and
`esp-servo-pwm`, whose API it does not keep.

The repository is named `mcu-servo`; the C API, `CONFIG_SERVO_*` options and
forge package key are `servo`.

> **Hardware unverified.** Host tests use a fake LEDC, a fake TIM, a fake
> I2C bus and a register-level fake PCA9685. Nothing here has moved a servo.

[Supported hardware](#supported-hardware) · [Install](#install) ·
[Quick start](#quick-start) · [Examples](#examples) ·
[Documentation](#documentation) · [Tests](#tests) · [License](#license)

## Supported hardware

| Backend | Platform | Outputs | Pulse resolution | Option |
|---|---|---|---|---|
| **MCU PWM** | ESP-IDF: LEDC, low-speed mode | one servo per channel | 14-bit duty: 1.22 us at 50 Hz | `SERVO_PWM` |
| | STM32 HAL: TIM, PWM mode 1 | one servo per channel | one counter tick | `SERVO_PWM` |
| **NXP PCA9685** | both, over I2C | 16, at one shared rate | 12-bit: 4.88 us at 50 Hz | `SERVO_PCA9685` |

**No servo figures.** No published standard fixes which pulse is which
angle, so this library ships none: the app measures each servo and gives
its calibration, and a servo with no calibration refuses to move. Every
PCA9685 value is cited to NXP's datasheet in the
[backend reference](docs/chips.md); wiring and power are in the
[hardware guide](docs/hardware.md).

## Install

### ESP-IDF

```sh
git clone https://github.com/JonasAtelier/mcu-servo components/servo
```

```ini
CONFIG_SERVO=y
CONFIG_SERVO_PWM=y
```

Other components declare `REQUIRES servo driver`.

### esp-forge / stm-forge

```make
servo_GIT=https://github.com/JonasAtelier/mcu-servo@stable
```

```make
PKG_servo           = y
SERVO_PCA9685       = y
```

**No backend is enabled by default.**

## Quick start

A servo on an LEDC pin:

```c
#include "servo.h"
#include "servo_pwm.h"

static struct servo_pwm pwm;
struct servo_pwm_config cfg = {
	.gpio = 18, .timer = LEDC_TIMER_0, .channel = LEDC_CHANNEL_0,
	.rate_hz = RATE_HZ,                 /* your servo's frame rate */
};
struct servo_calibration cal = {
	.min_us = MIN_US, .max_us = MAX_US, /* measured, see docs/usage.md */
	.range_deg = RANGE_DEG,
};
struct servo_dev *servo;

servo_pwm_init(&pwm, &cfg);             /* drives nothing yet */
servo = servo_pwm_servo(&pwm);
servo_set_calibration(servo, &cal);
servo_set_angle(servo, RANGE_DEG / 2);  /* the first move starts the pulses */
```

and the same calls for a PCA9685 output:

```c
struct servo_dev *hip = pca9685_servo(chip, 3);

servo_set_calibration(hip, &cal);
servo_set_angle(hip, 30.0f);
servo_stop(hip);                        /* no pulses: the servo goes slack */
```

- **0 or a negative errno** from every `servo_*` call, on both platforms.
  `pca9685.h` returns `esp_err_t` on ESP-IDF, as before, and errno on the
  HAL.
- **Clamped, never past the limits.** Angles clamp to `0..range_deg`, and
  every pulse to `min_us..max_us`, trim included.
- **No ramping and no feedback.** Every call is a step; a servo never says
  where it is.

## Examples

| Backend | ESP-IDF | STM32 HAL |
|---|---|---|
| MCU PWM | [pwm](examples/esp/pwm/), one servo swept on LEDC | [pwm](examples/stm/pwm/main.c), two on TIM3 |
| PCA9685 | [pca9685](examples/esp/pca9685/), four servos and an LED | [pca9685](examples/stm/pca9685/main.c), twelve servos and an LED |

The calibration in every example is a placeholder, not any servo's figures:
replace it with yours before the first run. ESP-IDF examples find this
library through `EXTRA_COMPONENT_DIRS`, which assumes it is checked out as
`servo`:

```sh
cd examples/esp/pwm
idf.py set-target esp32s3 # Replace with your board's target.
idf.py build
```

STM32 examples are snippets to integrate into your application.

## Scope and limitations

- **Servos on one LEDC timer share one rate.** The first servo on a timer
  configures it; later ones at that rate leave it running, and one at
  another rate is `-EBUSY` -- also once every servo on it is deleted, as
  the timer keeps running. See [usage](docs/usage.md#the-pwm-backend).
- **The PCA9685's sixteen outputs share one rate.** After
  `pca9685_set_frequency()`, move every servo again.
- **EXTCLK is never selected.** `ext_clock_hz` is the oscillator frequency
  used in the arithmetic (for instance a measured internal oscillator), as
  in esp-pca9685-pwm; see [the backend reference](docs/chips.md#nxp-pca9685).
- **No locks.** One task per servo, or your own mutex; one task per
  PCA9685.

## Documentation

| Guide | Contents |
|---|---|
| [Installation](docs/installation.md) | Forge, ESP-IDF, STM32 HAL, build options, logging |
| [Hardware](docs/hardware.md) | Signal and power wiring, the PCA9685's pins and address |
| [API usage](docs/usage.md) | The `servo_dev`, calibrating, both backends, errors, changes from esp-pca9685-pwm and esp-servo-pwm |
| [Backend reference](docs/chips.md) | Every PCA9685 register and timing, each cited; the LEDC and TIM arithmetic |
| [Development](docs/development.md) | Layout, ports, bus abstraction, adding a backend and test coverage |

## Tests

```sh
make -C test test
```

Host-only. See the [test reference](docs/development.md#tests). Log lines
during the suite are expected.

## License

[MIT](LICENSE).
