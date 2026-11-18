# mcu-servo

Move a hobby servo to a calibrated angle using an ESP32 LEDC or STM32 timer output. A PCA9685 on I2C can drive up to 16 channels.

**Platforms** · ESP-IDF 5.2+ and STM32 HAL. Hardware testing is pending. The host tests use simulated devices.

## Contents

- [Quick start](#quick-start)
- [Drivers and examples](#drivers-and-examples)
- [Using the library in your project](#using-the-library-in-your-project)
- [License](#license)

## Quick start

Use a separate servo supply with a shared ground. Before running the sweep, replace the example’s frame rate, pulse endpoints and travel angle with values for your servo. [Calibration guide](docs/usage.md#calibrating)

In a terminal with ESP-IDF activated, get the example and select the target for an ESP32. Change the target if your board uses another ESP32 chip.

```sh
git clone https://github.com/JonasAtelier/mcu-servo.git servo
cd servo/examples/esp/pwm
idf.py set-target esp32
```

Check the [wiring guide](docs/hardware.md) and edit the settings in [main.c](examples/esp/pwm/main/main.c) to match your board. Connect the board by USB, then build, upload and open the serial monitor.

```sh
idf.py build flash monitor
```

The example centres the servo, sweeps its calibrated range, then stops the pulses.

## Drivers and examples

Both outputs accept the same calibrated angle or pulse width. Use the PCA9685 header when you also need a channel for a general-purpose PWM output. [Angle and pulse API](include/servo.h)

| Output | Driver header | ESP-IDF project | STM32 HAL example |
|---|---|---|---|
| [Direct PWM](docs/usage.md#the-pwm-backend) | [servo_pwm.h](include/servo_pwm.h) | [LEDC sweep](examples/esp/pwm/) | [Two timer channels](examples/stm/pwm/main.c) |
| [PCA9685](docs/chips.md#nxp-pca9685) | [pca9685.h](include/pca9685.h) | [Four servos and an LED](examples/esp/pca9685/) | [Twelve servos and an LED](examples/stm/pca9685/main.c) |

## Using the library in your project

- Add the component and enable the driver for your hardware. [Installation and build options](docs/installation.md)
- Open the ESP-IDF project for your hardware, or copy the STM32 example into a project with board setup already in place. [All ESP-IDF examples](examples/esp/) · [All STM32 examples](examples/stm/)
- Look up calls and chip-specific settings. [API usage](docs/usage.md) · [Hardware reference](docs/chips.md)

To run the simulated tests on your computer, run `make -C test test` from the library root. [Test requirements and coverage](docs/development.md)

## License

[MIT](LICENSE).

[Browse the workshop catalog · ESP32 and STM32 drivers](https://github.com/JonasAtelier/jonas-atelier-libraries#esp32-and-stm32-drivers)
