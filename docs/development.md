# Architecture, driver development and tests

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

## Repository layout

```
include/                    public API, the only directory apps include
  servo.h                   struct servo_dev, its ops and calibration, servo_set_*()
  servo_log.h               servo_err/warn/info/dbg
  servo_pwm.h               struct servo_pwm and its config, struct servo_output, servo_pwm_*()
  pca9685.h                 esp-pca9685-pwm's API, the HAL's, pca9685_servo()
src/                        private to the library
  servo.c                   the core: calibration checks, moves, stop
  servo_math.h              the angle-to-pulse mapping, pure
  servo_priv.h              bus ops, attach and probe, errno maps both ways, the output port, the delay
  servo_port_esp.c          all of that on ESP-IDF (I2C, SPI, UART, LEDC); empty elsewhere
  servo_port_stm.c          all of that on the STM32 HAL (I2C, SPI, UART, TIM); empty under ESP-IDF
  pwm/                      servo_pwm.c
  pca9685/                  pca9685.c  pca9685_regs.h  pca9685_math.h  pca9685_priv.h
test/                       host tests, `make -C test test`
  test_core.c               the core on a fake backend
  test_pca9685_math.c       esp-pca9685-pwm's test_math.c: the datasheet's arithmetic
  test_names.c              every public header in one file
  pwm_cases.h               PWM backend cases, shared by esp/ and stm/
  pca9685_cases.h           a register-level fake PCA9685 and its cases, shared
  esp/                      suites on idf_stubs/, through servo_port_esp.c
  stm/                      suites on hal_stubs/, through servo_port_stm.c
  idf_stubs/  hal_stubs/    the slice of ESP-IDF and of the HAL the code calls
examples/
  esp/<name>/               ESP-IDF projects: pwm, pca9685
  stm/<name>/               STM32 snippets: pwm, pca9685
docs/                       the five guides; the datasheet here is gitignored
CMakeLists.txt              ESP-IDF component; elsewhere a static library for add_subdirectory(); stm-forge globs src/**/*.c instead
                            the static library's options are Kconfig's bool entries, found by the type line directly under each `config`
Kconfig                     SERVO and SERVO_<BACKEND>, for ESP-IDF and both forges' menus
idf_component.yml           ESP-IDF component manifest
LICENSE                     MIT
```

Naming inside `src/<backend>/`, the same in every category library:

| File | Holds |
|---|---|
| `<chip>.c` | the driver: init, del, the ops, the chip's calls |
| `<chip>_regs.h` | register map, each value with its source citation |
| `<chip>_proto.h` | serial protocol, for parts without registers |
| `<chip>_math.h` | pure conversions, compiled and asserted on the host |
| `<chip>_priv.h` | driver state, only if needed |

The MCU's own PWM has no chip and no registers: its backend is
`src/pwm/servo_pwm.c`, and every value it uses is ESP-IDF's or the HAL's own
name. The PCA9685 driver keeps esp-pca9685-pwm's four-space style; the
library's own files use tabs.

**Names next to the other category libraries.** Apps link several, so
every global symbol here is `servo_*` or `pca9685_*`, the bus layer copied
from mcu-esc included. The `names` step of `make -C test test` compiles
every source for both platforms and fails on any other global.

## Handles first

As in the other category libraries: each servo is a `struct servo_dev`,
held inside its backend's handle, `priv` pointing back at it. A PWM servo
is a `struct servo_pwm` the app places, as for mcu-esc's C615; a PCA9685
keeps esp-pca9685-pwm's heap handle and holds sixteen `servo_dev`s in it,
one per output, `pca9685_servo()` handing them out.

`write_us` and `stop` are required; `check_us`, whether a pulse fits in one
period, is optional. The core does what every backend would: it checks the
calibration, maps and clamps, refuses a move until a calibration is set,
and keeps `pulse_us`. A backend only puts a pulse it is given on a pin.

## Platform ports

The drivers in `src/` are the same C on both platforms; what differs is in
`src/servo_port_esp.c` and `src/servo_port_stm.c`: the bus ops, attach,
`servo_i2c_probe()`, the output port and `servo_delay_us()`.

**The output port** is `struct servo_output` and
`servo_output_attach/check/start/write_us/stop()`, mcu-esc's PWM port. On
ESP-IDF it is one LEDC channel in low-speed mode at 14-bit duty; the attach
asks `ledc_get_freq()` before it configures a timer, so a timer is shared
by rate and never reset under running servos (see
[the backend reference](chips.md#esp-idf-ledc)). On the HAL it is a TIM
channel the app set up; the port reads ARR, works out the rate from the
app's `tick_hz`, and writes the compare. The struct lives in `servo_pwm.h`,
its only user, so the app can place a `struct servo_pwm`.

**The errno boundary.** `servo_errno()` turns an `esp_err_t` into errno at
the bus; `servo_esp_err()` turns it back where `pca9685.h` returns, the
inverse for every code the first one lists, so the I2C driver's codes reach
the app unchanged. The PCA9685 driver works in errno and converts once,
through `SERVO_HANDLE_RET()`.

## Bus abstraction

The bus layer is mcu-esc's: `struct servo_bus_ops` with `transfer()`,
`exchange()`, `receive()` and `detach()`. Every category library offers
I2C, SPI and UART, used or not, and `test/{esp,stm}/test_port_*` check all
three. The PCA9685 is on I2C: `transfer()` with its register as `cmd`. The
MCU's PWM is outside the bus layer, on the output port.

## Adding a backend

1. `include/foo.h`: the backend's API, platform types behind
   `#ifdef ESP_PLATFORM`, and a call that hands out each `servo_dev`.
2. `src/foo/foo.c`: include `servo_priv.h` first, then wrap in
   `#if CONFIG_SERVO_FOO`.
   - Embed a `struct servo_dev` per servo; set `name`, `ops` and `priv`
     once it is ready. Leave the calibration to the app.
   - Give it `write_us` and `stop`, and `check_us` if a pulse can be too
     long for the backend's period.
3. Cite every register and timing to its datasheet in `src/foo/foo_regs.h`,
   and hold the arithmetic in a `test_foo_math.c`.
4. Add `CONFIG_SERVO_FOO` to:
   - the no-backend checks in `CMakeLists.txt` (its ESP-IDF part; the
     `add_subdirectory()` part reads `Kconfig`) and `src/servo_port_stm.c`;
   - a HAL module `#error` in `src/servo_priv.h`, if it needs one;
   - its sources in `CMakeLists.txt`;
   - `Kconfig`;
   - both forges' defconfigs;
   - `test/Makefile`, `BACKENDS` and `LIB_SRCS`.

## Tests

```sh
make -C test test
```

Host-only; no toolchain, HAL, ESP-IDF or servo needed. Nothing is checked
against a real servo or chip.

| Binary | Checks |
|---|---|
| `names` | **every global symbol of every source, on both platforms, is `servo_*` or `pca9685_*`** |
| `test_core` | refused calibrations: order, range, NaN and infinity, trim at the span either way, a `max_us` the backend refuses, nothing kept; **an uncalibrated servo moving nothing**; the mapping at both ends, the middle and rounding both ways; clamps; NaN and infinities refused; **invert, trim, both, and the limits after trim**; the raw pulse without trim or invert; a backend's error passed through with `pulse_us` kept; stop |
| `test_pca9685_math` | esp-pca9685-pwm's: section 7.3.5's example, the span, round trips, counts, duty, angles, the register constants |
| `test_names` | every public header in one file under `-Werror` |
| `esp/test_pwm`, `stm/test_pwm` | nothing on the pin until the first move; stop and restart; the period limit at 500 Hz; a failed start keeping the record; NULLs. ESP-IDF: the LEDC timer and channel settings, channels from `LEDC_CHANNEL_MAX` up refused with nothing configured, the duty at 50 Hz, the stop level, **the shared timer: configured once from an uninitialised LEDC, untouched for a second servo at its rate, `-EBUSY` at another rate, configured when it reads 0**, a failing timer config. HAL: the rate from ARR at 1 and 84 MHz, the compare, refused handles, channels and clocks, **each channel refused without its own OCxPE bit and taken with it**, a 32-bit ARR that wraps |
| `esp/test_pca9685`, `stm/test_pca9685` | esp-pca9685-pwm's test_driver.c on a register-level chip that **drops PRE_SCALE writes while awake, reads RESTART 1 after sleeping while running, and asserts 500 us awake before RESTART is written 1**: init, address range, open drain and INVRT, dead bus, **nothing at the address with nothing written**, frequency change, **sleep and wake, and a frequency change on a sleeping chip, with RESTART reading 1**, raw, duty, pulse, off, all off, the legacy angle to the count, NULLs; `pca9685_servo()`: channels, uncalibrated, the pulse in counts, stop on one output only, a period too short after a frequency change. ESP-IDF: **every common `ESP_ERR_*` back as itself from four calls and from add_device**, the device removed after a failed init, the I2C clock passed through. HAL: the HAL's status as errno |
| `esp/test_port_esp`, `stm/test_port_stm` | the bus layer, as in every category library |

All build under `-std=gnu11 -Wall -Wextra -Werror -pedantic` with UBSan, and
`-Wl,--wrap` (GNU ld) to capture `printf()` on the HAL side.

**Builds.** Both ESP-IDF examples build for esp32s3 on ESP-IDF v5.5.5 with
no warning. dingo_s3, its sources unchanged, builds the same way against
esp-pca9685-pwm and against this library with `SERVO_PCA9685` on, with no
warning; `nm -g --defined-only` over the archives of this library, mcu-imu,
max17048, fsm and f_complementary finds no symbol defined twice. Run against
the same fake I2C bus with dingo_s3's calls, esp-pca9685-pwm's driver and
this one send the same transactions, delays and return codes, apart from
the probe `pca9685_init()` now sends first.
