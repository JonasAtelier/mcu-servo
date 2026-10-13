# Installation

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

The repository is `mcu-servo`; the forge package key, C API and
configuration options are `servo`. The clone command below names the
ESP-IDF component directory `servo`. It replaces `esp-pca9685-pwm` (forge
key `pca9685`) and `esp-servo-pwm`: a project that calls `pca9685_*()` keeps
its code and only swaps the component, with `SERVO_PCA9685` on (see
[changes from esp-pca9685-pwm](usage.md#changes-from-esp-pca9685-pwm)).

## With forge

With [esp-forge](https://github.com/JonasAtelier/esp-forge) or
[stm-forge](https://github.com/JonasAtelier/stm-forge). The catalog line in
`profiles/pkgversion` is the same for both:

```make
servo_GIT=https://github.com/JonasAtelier/mcu-servo@stable
```

and a target turns the library and its backends on in
`configs/<target>_defconfig`:

```make
PKG_servo           = y
SERVO_PCA9685       = y
# SERVO_PWM is not set
# SERVO_DEBUG is not set
```

or with `make menu`. esp-forge turns each line into `CONFIG_SERVO_<BACKEND>=y`
in `sdkconfig.defaults`; stm-forge passes it as
`-DCONFIG_SERVO_<BACKEND>=1`. `SERVO_DEBUG = y` turns on `servo_dbg()` lines
on the HAL; on ESP-IDF the log level decides.

A target that had `PKG_pca9685 = y` moves to `PKG_servo = y` plus
`SERVO_PCA9685 = y`. Neither forge's catalog has the `servo` line yet: it
goes in once the repository is on GitHub.

## ESP-IDF

Clone into your project's `components/` directory. A local component is
named after its directory, and that name is what you `REQUIRES`:

```sh
git clone https://github.com/JonasAtelier/mcu-servo components/servo
```

```cmake
idf_component_register(SRCS "main.c" REQUIRES servo driver)
```

A component with no `REQUIRES` at all, like `main` in many projects, sees
every component in the build, this one included.

Enable the backends under `idf.py menuconfig` → **Component config → Hobby
RC servo subsystem**, or put `CONFIG_SERVO_<BACKEND>=y` in your
`sdkconfig.defaults` as each example in `examples/esp/` does. With the
subsystem on and no backend picked, the build warns.

## STM32 HAL

Copy `include/` and `src/` into an STM32 project, compile every `.c` under
`src/`, backend folders included, add `include/` and `src/` to the include
path (`src/` only needs to reach this library's own sources), and define
what the forge would: `CMSIS_device_header` as your family header
(`"stm32f4xx.h"`), `USE_HAL_DRIVER`, and `CONFIG_SERVO_<BACKEND>=1`.

**No backend is built by default.** The core is always built and each
backend is opt-in: one whose option is absent or 0 compiles to nothing.
With the library on and no backend picked, `src/servo_port_stm.c` emits a
`#warning`.

**The HAL modules are yours to enable:**

| Backend | Needs in hal_conf | HAL source | Gives |
|---|---|---|---|
| MCU PWM | `HAL_TIM_MODULE_ENABLED` | `stm32<family>xx_hal_tim.c` | `servo_pwm_init()` |
| PCA9685 | `HAL_I2C_MODULE_ENABLED` | `stm32<family>xx_hal_i2c.c` | `pca9685_init()` |

Without the module a backend needs, its build stops with an `#error` in
`src/servo_priv.h` (and in `servo_pwm.h` or `pca9685.h`).

**printf() is the log.** Lines go out through `printf()`, so retarget
newlib's `_write()` to a UART.

## Logging

`servo_err()`, `servo_warn()`, `servo_info()` and `servo_dbg()` prefix every
line with a name: the servo's backend, or the chip's. On ESP-IDF they are
`ESP_LOGx` under the one tag `"servo"`, so `esp_log_level_set("servo", ...)`
controls them all. On the HAL they are `printf("servo: <name>: ...")`, and
`servo_dbg()` is compiled in only with `CONFIG_SERVO_DEBUG`. The PCA9685 says
what it found when it starts:

```
servo: pca9685: PCA9685 at 0x40: 50.03 Hz (prescale 121), totem pole
```

esp-pca9685-pwm logged under the tag `"pca9685"`; an app that set that tag's
level sets `"servo"` now.

On ESP-IDF, the first `servo_pwm_init()` in a program also makes the LEDC
driver log `E (...) ledc: ledc_get_freq(...): LEDC is not initialized`: the
init asks the timer's rate before anything has set the LEDC up. It is
expected and harmless; see [the backend reference](chips.md#esp-idf-ledc).
