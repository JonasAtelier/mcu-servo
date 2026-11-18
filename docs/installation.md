# Installation

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

The repository is `mcu-servo`; the forge package key, C API and
configuration options are `servo`. The clone command below names the
ESP-IDF component directory `servo`. It replaces `esp-pca9685-pwm` (forge
key `pca9685`) and `esp-servo-pwm`: a project that calls `pca9685_*()` keeps
its code and only swaps the component, with `SERVO_PCA9685` on (see
[changes from esp-pca9685-pwm](usage.md#changes-from-esp-pca9685-pwm)).

Three step-by-step guides come first: [ESP-IDF](#esp-idf-step-by-step),
[STM32 with CMake](#with-cmake-a-cubemx-cmake-project) and
[STM32 without CMake](#without-cmake-stm32cubeide-a-makefile). None
needs a forge; if you build with esp-forge or stm-forge, see
[With forge](#with-forge). The steps use a PCA9685 board, 16 servo outputs
over I2C, which says what it found when it starts; for servos on the MCU's
own PWM, turn on `SERVO_PWM` instead and start from its example in
`examples/esp/pwm` or `examples/stm/pwm`. The reference follows the guides.

## ESP-IDF, step by step

**You need** ESP-IDF 5.2 or newer, with `idf.py` working in your terminal, and
an ESP-IDF project: a folder with a `CMakeLists.txt` and a `main/` folder.
`idf.py create-project my-project` makes one. On the wire: the PCA9685's SDA
and SCL to two GPIOs, the board at its default address 0x40, and the
servos' own supply on its V+ terminal.

ESP-IDF calls a library a **component**: a folder with its own
`CMakeLists.txt` that the build finds and compiles. This library is the
component `servo`.

1. **Get the library.** Pick one way.

   - **Component manager**, the simplest: the first build downloads the
     library into `managed_components/servo`. Create
     `main/idf_component.yml`:

     ```yaml
     dependencies:
       servo:
         git: https://github.com/JonasAtelier/mcu-servo.git
         version: stable
     ```

   - **Clone**, if you want to edit the library or build offline. In the
     project folder:

     ```sh
     git clone https://github.com/JonasAtelier/mcu-servo components/servo
     ```

     Keep the folder name `servo`: ESP-IDF names a component after its
     folder.

   ```
   my-project/
   ├── CMakeLists.txt
   ├── sdkconfig.defaults       step 2
   ├── main/
   │   ├── CMakeLists.txt       step 3
   │   ├── idf_component.yml    step 1, component manager
   │   └── my-project.c         step 4
   └── components/
       └── servo/               step 1, clone
   ```

2. **Turn your backend on.** No backend is built until you do. Create
   `sdkconfig.defaults` in the project folder, a text file of settings you
   choose, with one line per backend:

   ```ini
   CONFIG_SERVO_PCA9685=y
   ```

   The other backend is `CONFIG_SERVO_PWM`, the MCU's own PWM. ESP-IDF copies
   these lines into `sdkconfig`, the project's full settings file, only when
   it creates that file: **if `sdkconfig` already exists, delete it** so your
   line is read. `idf.py menuconfig` → **Component config → Hobby RC servo
   subsystem** does the same from a menu.

3. **Let your code see the library.** A new project's `main/CMakeLists.txt`
   has no `REQUIRES` and already sees every component. If yours has a
   `REQUIRES` list, add `servo` and `driver` to it:

   ```cmake
   idf_component_register(SRCS "main.c" REQUIRES servo driver)
   ```

4. **Write the smallest test.** Copy
   [examples/esp/pca9685/main/main.c](../examples/esp/pca9685/main/main.c)
   over your main source file and set `PIN_SDA` and `PIN_SCL` to your
   wiring. `MIN_US`, `MAX_US` and `RANGE_DEG` there are placeholders, not
   your servo's figures: [Calibrating](usage.md#calibrating) says how to
   measure them. The test swings four servos from end to end once a second.
   [API usage](usage.md) explains the calls.

5. **Build.**

   ```sh
   idf.py set-target esp32    # your chip: esp32s3, esp32c3, ...
   idf.py build
   ```

   A good build ends with `Project build complete.` Flash it as any ESP-IDF
   app and open the serial monitor: the driver's first line ends in
   `servo: pca9685: PCA9685 at 0x40: 50.03 Hz (prescale 121), totem pole`.

**If it fails**

| The build prints | Why | Fix |
|---|---|---|
| `Failed to resolve component 'servo' required by component 'main': unknown name.` | The clone is not in a folder named `servo` | Rename it to `components/servo` |
| `fatal error: pca9685.h: No such file or directory` | `main/CMakeLists.txt` has a `REQUIRES` list without `servo` | Step 3 |
| `servo: CONFIG_SERVO is on but no backend is selected; set CONFIG_SERVO_<BACKEND>=y (esp-forge: SERVO_<BACKEND> = y)` (a CMake warning) | No backend turned on, often because `sdkconfig` existed before you edited `sdkconfig.defaults` | Step 2: delete `sdkconfig`, build again |

## STM32, step by step

**You need** a project for an STM32F4 that uses ST's **HAL**, the vendor
library behind `HAL_Init()`, `HAL_I2C_Init()` and friends, with the I2C the
PCA9685 is wired to already set up. STM32CubeMX makes such a project; with
**Toolchain / IDE: CMake** it makes a CMake one, the main case below.

Two terms. A **HAL module** is the part of the HAL for one peripheral: I2C,
TIM. It is compiled only when it is switched on in **hal_conf**, the file
`Core/Inc/stm32f4xx_hal_conf.h`. CubeMX switches a module on when you enable
that peripheral.

### With CMake (a CubeMX CMake project)

1. **Get the library.** In the project folder, next to the top-level
   `CMakeLists.txt`:

   ```sh
   git clone https://github.com/JonasAtelier/mcu-servo
   ```

   ```
   my-board/
   ├── CMakeLists.txt                  step 2
   ├── cmake/stm32cubemx/              made by CubeMX
   ├── Core/
   │   ├── Inc/stm32f4xx_hal_conf.h    hal_conf, step 3
   │   └── Src/main.c                  step 4
   ├── Drivers/                        the HAL, copied in by CubeMX
   └── mcu-servo/                      step 1
   ```

2. **Add it to the build.** In the top `CMakeLists.txt`, after the line
   `add_subdirectory(cmake/stm32cubemx)`, add:

   ```cmake
   set(CONFIG_SERVO_PCA9685 ON)        # your backend; one line per backend
   set(MCU_HAL_TARGET stm32cubemx)
   set(MCU_DEVICE_HEADER stm32f4xx.h)
   add_subdirectory(mcu-servo)
   ```

   and add `servo` to the link line CubeMX already wrote, so it reads:

   ```cmake
   target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx servo)
   ```

   Do not add `PRIVATE`: CubeMX's line has no such keyword, and CMake refuses
   to mix the two forms on one target.

   `stm32cubemx` is a CMake *target*, a named bundle of settings, that CubeMX
   makes to carry the HAL's include folders and defines; `MCU_HAL_TARGET`
   lends those to the library, which works with any CubeMX version.
   `MCU_DEVICE_HEADER` is your chip family's main header.

3. **Check the HAL module.** The PCA9685 talks I2C, so hal_conf must have
   `#define HAL_I2C_MODULE_ENABLED`; enabling I2C1 (or any I2C) in CubeMX
   does that. The [HAL module table](#stm32-hal) lists what the PWM backend
   needs.

4. **Write the smallest test.** It is
   [examples/stm/pca9685/main.c](../examples/stm/pca9685/main.c) fitted into
   CubeMX's `Core/Src/main.c`, with four servos swinging from end to end
   once a second. Paste each part between the `USER CODE BEGIN` and `END`
   comments named, so a regenerate keeps it.

   After `/* USER CODE BEGIN Includes */`:

   ```c
   #include <stdio.h>

   #include "pca9685.h"
   #include "servo.h"
   ```

   After `/* USER CODE BEGIN 0 */`, just above `int main(void)`:

   ```c
   /* Placeholders, not any servo's figures: docs/usage.md, "Calibrating". */
   #define SERVOS    4
   #define MIN_US    1250  /* the pulse at 0 deg */
   #define MAX_US    1750  /* the pulse at RANGE_DEG */
   #define RANGE_DEG 60.0f /* the travel */

   static pca9685_handle_t chip;
   ```

   After `/* USER CODE BEGIN 2 */`, which comes after `MX_I2C1_Init()`:

   ```c
     const pca9685_config_t cfg = {
       .frequency_hz = 50.0f, /* placeholder: the servos' frame rate */
       .totem_pole = true,
     };
     const struct servo_calibration calibration = {
       .min_us = MIN_US, .max_us = MAX_US, .range_deg = RANGE_DEG,
     };

     if (pca9685_init(&hi2c1, &cfg, &chip))
       Error_Handler(); /* the init has logged why */
     for (int ch = 0; ch < SERVOS; ch++)
       servo_set_calibration(pca9685_servo(chip, ch), &calibration);
   ```

   After `/* USER CODE BEGIN 3 */`, inside the `while (1)` loop:

   ```c
       static bool at_end;

       at_end = !at_end;
       for (int ch = 0; ch < SERVOS; ch++)
         servo_set_angle(pca9685_servo(chip, ch), at_end ? RANGE_DEG : 0.0f);
       HAL_Delay(1000);
   ```

   The lines go out through `printf()`: send that to a UART by writing
   newlib's `_write()`, and add this to `CMakeLists.txt` so numbers are
   printed:

   ```cmake
   target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)
   ```

   [API usage](usage.md) explains the calls.

5. **Build** as before, for example `cmake --build` with your build folder.
   A good build shows a `Linking C executable` line and no error. On the
   UART, the driver's first line reads
   `servo: pca9685: PCA9685 at 0x40: 50.03 Hz (prescale 121), totem pole`.

**If it fails**

| You see | Why | Fix |
|---|---|---|
| `servo: no backend is selected; set(CONFIG_SERVO_<BACKEND> ON) before add_subdirectory()` (a CMake warning), then ``undefined reference to `pca9685_init'`` | No backend turned on, or turned on after `add_subdirectory(mcu-servo)` | Step 2 |
| `#error "the PCA9685 driver needs the HAL I2C module: ...`, and in `main.c` `#error "pca9685.h needs the HAL I2C module: ...` | The HAL module is off | Enable I2C1 in CubeMX and regenerate |
| `#error "servo: CMSIS_device_header is not defined; ...` | `MCU_DEVICE_HEADER` is missing | Step 2 |
| `fatal error: stm32f4xx.h: No such file or directory` | `MCU_HAL_TARGET` is missing | Step 2 |
| `Target "stm32cubmx" not found.` (a CMake error) | `MCU_HAL_TARGET` is misspelt | Step 2 |
| `The plain signature for target_link_libraries has already been used with the target` | `PRIVATE` added to the link line | Step 2 |

### Without CMake (STM32CubeIDE, a Makefile)

1. Copy `include/` and `src/` of this repository into your project, for
   example to `Middlewares/mcu-servo/`, keeping the backend folders inside
   `src/`.
2. Add every `.c` file under `src/` to the build, backend folders included.
3. Add two include paths, `Middlewares/mcu-servo/include` and
   `Middlewares/mcu-servo/src`, and two defines:
   `CMSIS_device_header="stm32f4xx.h"`, quotes included, and
   `CONFIG_SERVO_PCA9685=1`, one per backend. CubeMX already defines
   `USE_HAL_DRIVER` and your device, such as `STM32F405xx`.
4. Steps 3 to 5 of the CMake guide apply as they are; `-u _printf_float`
   goes in the linker flags.

Where these settings usually live: in STM32CubeIDE, **Project → Properties →
C/C++ Build → Settings → Tool Settings → MCU GCC Compiler** (Include paths,
Preprocessor); in a CubeMX Makefile, `C_SOURCES`, `C_INCLUDES` and `C_DEFS`.
The build errors are the ones in the table above; the fix is the matching
define or include path instead of a CMake line. One more:
`#include expects "FILENAME" or <FILENAME>` means `CMSIS_device_header` lost
its quotes.

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
`SERVO_PCA9685 = y`.

## ESP-IDF

Setup: [ESP-IDF, step by step](#esp-idf-step-by-step). For reference:

- In `main/idf_component.yml`, `version` is a git ref; `stable` is the
  released one.
- A clone in `components/` is a local component, named after its folder;
  that name is what other components `REQUIRES`. The examples in
  `examples/esp/` need no name: their `main` lists no `REQUIRES`, and ESP-IDF
  gives `main` every component, so they build from any checkout name.
- With the subsystem on and no backend picked, the build warns.

## STM32 HAL

Setup: [With CMake](#with-cmake-a-cubemx-cmake-project) or
[Without CMake](#without-cmake-stm32cubeide-a-makefile). The
`add_subdirectory()` route builds a static library `servo`. For reference:

- The family is the F4: `CMSIS_device_header` is `"stm32f4xx.h"`.
- Each `CONFIG_SERVO_<BACKEND>` (and `CONFIG_SERVO_DEBUG`) that is true
  reaches the compiler as `CONFIG_SERVO_<BACKEND>=1`, for the library and
  your app.
- `MCU_HAL_TARGET` is a target that carries your `USE_HAL_DRIVER`, device
  define (`STM32F405xx`, ...) and the include dirs of the HAL, CMSIS and your
  `stm32f4xx_hal_conf.h`. The library copies only those include dirs and
  defines, never the target's sources, so this works with any CubeMX
  version. `MCU_DEVICE_HEADER` becomes `CMSIS_device_header`, which CubeMX
  does not define. Every `mcu-*` library reads these two, so set them once.
  Leave them unset if your project already provides both.
- The CPU flags (`-mcpu`, `-mfloat-abi`, ...) must reach the library too;
  CubeMX's toolchain file sets them for every target.
- Without CMake, `src/` only needs to be on the include path of this
  library's own sources; the defines are what stm-forge passes.

**No backend is built by default.** The core is always built and each
backend is opt-in: one whose option is absent or 0 compiles to nothing.
With the library on and no backend picked, the CMakeLists warns (ESP-IDF
and `add_subdirectory()`) and `src/servo_port_stm.c` emits a `#warning`.

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
