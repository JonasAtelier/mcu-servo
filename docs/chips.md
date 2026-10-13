# Backend reference

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

Every PCA9685 number is from NXP's PCA9685 product data sheet, Rev. 4,
16 April 2015, `docs/pca9685.pdf` (gitignored). The LEDC and TIM numbers are
ESP-IDF v5.5.5's and STM32CubeF4's sources, cited by file and line. No
number here is a servo's: see [calibrating](usage.md#calibrating).

## NXP PCA9685

Sixteen 12-bit PWM outputs at one shared rate, over I2C.
`src/pca9685/pca9685_regs.h` holds every value below; `pca9685_math.h` the
arithmetic.

### Registers

| Value | Name | Source |
|---|---|---|
| 0x00, 0x01 | MODE1, MODE2 | Table 4 |
| 0x02 | SUBADR1, reset value E2h: the presence check writes and reads it back | Table 4 |
| 0x06 + 4n | LEDn_ON_L, ON_H, OFF_L, OFF_H for n = 0..15, to 0x45 | Table 4 |
| 0xFA..0xFD | ALL_LED_ON_L..ALL_LED_OFF_H: one write loads every output | Table 4, section 7.3.4 |
| 0xFE | PRE_SCALE, reset value 1Eh | Table 4, section 7.3.5 |
| MODE1 bit 0 | ALLCALL, 1 at reset | Table 5 |
| MODE1 bits 1..3 | SUB3, SUB2, SUB1 | Table 5 |
| MODE1 bit 4 | SLEEP, 1 at reset: oscillator off | Table 5 |
| MODE1 bit 5 | AI, register auto-increment, 0 at reset | Table 5 |
| MODE1 bit 6 | EXTCLK, sticky until power cycle or software reset | Table 5 |
| MODE1 bit 7 | RESTART, write 1 to clear | Table 5, section 7.3.1.1 |
| MODE2 bits 0..1 | OUTNE: the outputs' level while OE is high | Table 6, Table 11 |
| MODE2 bit 2 | OUTDRV: 1 totem pole, 0 open drain | Table 6 |
| MODE2 bit 3 | OCH: outputs change on STOP (0) or ACK (1) | Table 6 |
| MODE2 bit 4 | INVRT | Table 6 |
| LEDn_ON_H, LEDn_OFF_H bit 4 | full ON, full OFF; full OFF is 1 at reset | Table 7 |
| 0x40..0x7F | the address: 0x40 plus A5..A0 | section 7.1.1, Fig 4 |
| 0x70 | LED All Call address, on at power-up | section 7.1.2 |
| 25 MHz | internal oscillator | section 7.3.5 |

### Timing and sequences

- **Frequency.** `prescale = round(osc / (4096 * rate)) - 1` (section 7.3.5,
  Equation 1). The datasheet's example, 200 Hz at 25 MHz, is 1Eh, the reset
  value; `test_pca9685_math` asserts it. PRE_SCALE has a hardware minimum of
  3; 3 to 255 spans 1525.9 to 23.84 Hz, which the datasheet rounds to 1526
  and 24.
- **PRE_SCALE only while asleep**: "Writes to PRE_SCALE register are blocked
  when SLEEP bit is logic 0" (Table 4, note 1). `pca9685_set_frequency()`
  sleeps, writes, wakes, waits, restarts; the test chip drops a write made
  awake.
- **500 us to wake**: "It takes 500 us max. for the oscillator to be up and
  running once SLEEP bit has been set to logic 0" (Table 5, note 2), and
  "The SLEEP bit must be logic 0 for at least 500 us, before a logic 1 is
  written into the RESTART bit" (section 7.3.1.1). `servo_delay_us(500)`
  sits between the two; the test chip asserts the gap.
- **Auto-increment** is off at reset (Table 5); the four-byte channel writes
  need it, so init sets AI (Fig 21, Fig 23).
- **Orderly shutdown**: full OFF on every output before SLEEP (section
  7.3.1.1, footnote 1). `pca9685_del()` writes ALL_LED full off, then
  sleeps.
- **I2C**: up to 1 MHz, Fast-mode Plus (section 1).

### A pulse in counts

A pulse of `us` at the rate in force is `round(us / period * 4096)` counts,
ON at count 0 and OFF at that count. At 50.03 Hz (prescale 121) one count is
4.88 us. A pulse longer than the period is refused (`-EINVAL`). Duty 0 and 1
in `pca9685_set_duty()` use the full-OFF and full-ON bits instead of a count
(Table 7).

### What the driver does not do

- **EXTCLK is never selected.** Switching to the EXTCLK pin takes the
  sequence in Table 5, bit 6; `pca9685_init()` does not run it.
  `ext_clock_hz` is the oscillator frequency used in the arithmetic -- for
  instance the internal oscillator's, measured -- in place of the nominal
  25 MHz, as in esp-pca9685-pwm. With an external clock fitted, the chip
  still runs on its own oscillator.
- **OE and the software reset** (section 7.6) are left to the app.

## ESP-IDF LEDC

The PWM backend's output port, `src/servo_port_esp.c`, from mcu-esc's.

- **Low-speed mode, 14-bit duty**: the mode every ESP32 has, and the widest
  duty the ESP32-S2, -S3 and -C3 take. A pulse is
  `round(us * rate * 2^14 / 10^6)` counts: 1.22 us per count at 50 Hz.
- **`ledc_timer_config()` resets the timer's counter**:
  `ledc_timer_rst()` after setting the divider
  (`components/esp_driver_ledc/src/ledc.c:778-806`). On a timer other
  channels run on, that cuts their pulse in flight short. So the attach
  asks `ledc_get_freq()` first and configures only a timer that runs at
  nothing; one at the same rate is shared as it is, one at another rate is
  `-EBUSY`.
- **`ledc_get_freq()`** (`ledc.c:1112-1133`) returns 0 for a timer never
  configured, logging `W ... ledc: LEDC timer not configured ...`; and
  before the LEDC has been set up at all, its error code cast to the rate:
  `ESP_ERR_INVALID_STATE` (259), logging
  `E ... ledc: ledc_get_freq(...): LEDC is not initialized`. The first
  servo of a program therefore logs that error line once, harmlessly (none
  with `CONFIG_COMPILER_OPTIMIZATION_CHECKS_SILENT`). 258 and 259 read as
  "not running", so a timer at exactly 258 or 259 Hz is configured afresh.
- **The read-back rate is exact on esp32, -s3 and -c3.**
  `ledc_timer_config()` rounds the divider to 1/256
  (`ledc_calculate_divisor()`, `ledc.c:459-487`), and `ledc_get_freq()`
  rounds the rate back from it. On those three `LEDC_AUTO_CLK` tries the
  80 MHz APB clock first (`LEDC_LL_GLOBAL_CLOCKS`, `hal/<target>/ledc_ll.h`).
  With that clock at 14 bits, the rate read back is the rate asked for at
  every rate from 1 to 1139 Hz, worked through both formulas for each.
  **Unverified on other targets** (ESP32-P4, -C6, ...), whose clock lists
  were not checked: there a second servo at the same rate could read a rate
  off by one and get `-EBUSY`.

## STM32 HAL TIM

The PWM backend's output port, `src/servo_port_stm.c`, from mcu-esc's.

- The app sets the channel up in PWM mode 1 with output-compare preload; the
  port writes the compare, `round(us * tick_hz / 10^6)` counts, and refuses
  one past ARR. The rate is `tick_hz / (ARR + 1)`.
- **Preload is required.** The attach checks the channel's OCxPE bit in
  CCMR1 or CCMR2 and refuses (`-EINVAL`) a channel without it: a compare
  written mid-period would otherwise take effect at once and could cut the
  pulse in flight short or double it. `HAL_TIM_PWM_ConfigChannel()` sets the
  bit (`stm32f4xx_hal_tim.c`), so a channel CubeMX set up for PWM has it.
- Stopping writes compare 0, then calls `HAL_TIM_PWM_Stop()`. With preload
  on, the 0 sits in the preload register until the next update event, by
  which time the channel is off, so it does not hold the pin low. What the
  pin does while the channel is off is the app's timer and GPIO setup.
- **The PCA9685's 500 us** is `HAL_Delay(1)`: `HAL_Delay()` waits at least
  its argument, adding a tick for the partial first one
  (`stm32f4xx_hal.c`, `HAL_Delay()`, STM32CubeF4).
