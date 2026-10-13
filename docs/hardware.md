# Hardware

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

## No servo figures here

A hobby servo has three wires -- signal, supply, ground -- and no published
standard behind its pulse widths, frame rate, supply voltage or wire
colours. This guide gives none of them: take them from your servo's own
documentation, and its pulse limits from measuring it
([calibrating](usage.md#calibrating)).

## Power

**Give the servos a supply of their own**, not the MCU's regulator, and
**common the grounds**: the signal is measured against ground, so a servo
without the MCU's ground sees no pulse at all. Size the supply and its
bulk capacitance from your servos' documented stall current; several servos
starting at once draw it together. A supply that sags under that load
browns out the MCU, which looks like a software fault and is not one.

## The MCU's own PWM

| Servo wire | Goes to |
|---|---|
| signal | the pin: an LEDC-capable GPIO on ESP-IDF, a TIM channel's pin on the STM32 |
| supply | the servo supply |
| ground | the servo supply's ground, joined to the MCU's |

The pin is a 3.3 V output on both ESP32 and STM32. Whether your servo reads
a 3.3 V pulse depends on its input; if it does not, it needs a level
shifter, not a different pulse.

On ESP-IDF any output GPIO can carry an LEDC channel. Servos may share an
LEDC timer when they run at one rate; each needs a channel of its own.
On the STM32, CubeMX sets up the timer and its pins; see
[`examples/stm/pwm/main.c`](../examples/stm/pwm/main.c) for the settings
the backend expects.

**No pulses until the first move**, and none after `servo_stop()`, so a
servo powered up with the board stays where it was parked. On ESP-IDF the
pin is held low after a stop; on the STM32 the pin's level with the channel
off is your timer and GPIO setup.

## NXP PCA9685

Pin names are the datasheet's, `docs/pca9685.pdf` Table 3; the breakout
boards' extra `V+` rail is not a pin of the chip.

| Pin | Goes to | Note |
|---|---|---|
| VDD | the MCU's 3.3 V | the chip's own supply, 2.3 V to 5.5 V (section 1) |
| VSS | ground | shared with the MCU and the servo supply |
| SDA, SCL | the MCU's I2C pins | up to 1 MHz, Fast-mode Plus (section 1); pull-ups as the bus needs |
| A0..A5 | ground or VDD, each | the address, 0x40 plus the six bits strapped (section 7.1.1, Fig 4). No internal pull-ups: none may float |
| OE | ground, or an MCU output | active low (section 7.4): high puts every output at the level OUTNE in MODE2 sets (Table 11). This library never drives it |
| EXTCLK | ground, unless an external clock is fitted | never selected: `ext_clock_hz` is only the oscillator frequency used in the arithmetic; see [the backend reference](chips.md#nxp-pca9685) |
| LED0..LED15 | servo signal wires, or LEDs | totem pole with `totem_pole = true`, open drain otherwise (Table 6, OUTDRV) |

**Address 0x70 is taken** by the LED All Call address, which this driver
leaves on (Table 5, ALLCALL; section 7.1.2): strap any other.

**Outputs start full off.** Out of reset every LEDn_OFF_H has its full-OFF
bit set (Table 7), and `pca9685_init()` writes full off to all sixteen again
before it returns. Nothing moves until a servo or an output is set.
