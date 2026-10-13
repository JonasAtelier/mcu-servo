# API usage

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

## The servo_dev

Every servo is a `struct servo_dev` (`include/servo.h`), handed out by its
backend: `servo_pwm_servo()` or `pca9685_servo()`. The same four calls work
on any of them:

| Call | Does |
|---|---|
| `servo_set_calibration(servo, &cal)` | Check and keep the servo's calibration |
| `servo_set_angle(servo, deg)` | Move: clamp, invert, map, trim, clamp again |
| `servo_set_pulse_us(servo, us)` | The pulse itself, clamped to the limits; no trim, no invert |
| `servo_stop(servo)` | No more pulses; the servo goes slack |

`servo->pulse_us` is the pulse on the pin now, 0 when stopped. A call
returns once the pulse is set, not once the servo is there: nothing comes
back from a hobby servo.

### The calibration

```c
struct servo_calibration {
	uint16_t min_us;	/* pulse at 0 degrees, before trim */
	uint16_t max_us;	/* pulse at range_deg, before trim */
	float range_deg;	/* travel between them, above 0 */
	int16_t trim_us;	/* added to every angle's pulse */
	bool invert;
};
```

An angle maps onto a pulse in this order:

1. clamp to `0..range_deg`; a NaN or infinity is `-EINVAL`;
2. if `invert`, take `range_deg - angle`;
3. linear from `min_us` at 0 to `max_us` at `range_deg`, rounded to the
   nearest microsecond;
4. add `trim_us`;
5. clamp to `min_us..max_us`.

So `min_us` and `max_us` are hard limits: no call ever puts a pulse
outside them, trim included. Trim shifts the middle of the travel and, near
the end it pushes towards, gives up a little travel to the limit.

`servo_set_calibration()` refuses (`-EINVAL`) `min_us` not below `max_us`, a
`range_deg` that is not above 0 or not finite, a trim as wide as the span
or wider, and a `max_us` that does not fit in one period of the backend's
rate. Nothing is kept on refusal.

**A servo with no calibration refuses to move**: `servo_set_angle()` and
`servo_set_pulse_us()` are `-EINVAL` until `servo_set_calibration()` has
succeeded once. There is no default, because no servo figure is right for
every servo. `servo_stop()` needs none.

### Calibrating

1. Give the servo a first calibration whose limits you trust to be inside
   its travel: a narrow span, from its documentation or from a servo of
   the same model already measured.
2. Step `servo_set_pulse_us()` from the middle towards one end, a few
   microseconds at a time, watching the horn. Where it stops following, it
   has reached its end; back off until it follows again. Widen the
   calibration as you go, since `servo_set_pulse_us()` clamps to it.
3. Do the same towards the other end.
4. Those two pulses are `min_us` and `max_us`. Measure the angle the horn
   turns between them: that is `range_deg`.
5. Set `trim_us` so that the angle you call the middle lands where the
   mechanism wants it, and `invert` if the servo is mounted mirrored.

**Continuous-rotation servos** take the pulse as a speed, not a position:
use `servo_set_pulse_us()`, with the calibration around the pulses that
servo documents.

## The PWM backend

`include/servo_pwm.h`. One servo per pin; the app places the struct:

```c
static struct servo_pwm pwm;

struct servo_pwm_config cfg = {
	.gpio = 18,			/* ESP-IDF */
	.timer = LEDC_TIMER_0,
	.channel = LEDC_CHANNEL_0,
	.rate_hz = RATE_HZ,
};
int err = servo_pwm_init(&pwm, &cfg);
struct servo_dev *servo = servo_pwm_servo(&pwm);
```

| Call | Does |
|---|---|
| `servo_pwm_init(&pwm, &cfg)` | Check the config and bind the pin. Drives nothing |
| `servo_pwm_servo(&pwm)` | The `servo_dev`; NULL before init or after del |
| `servo_pwm_del(&pwm)` | Stop the pulses and unbind. The timer keeps running |

The first move starts the pulses; `servo_stop()` ends them, and the next
move starts them again.

**ESP-IDF.** An LEDC channel in low-speed mode at 14-bit duty. Servos may
share an LEDC timer at one rate:

- the first `servo_pwm_init()` on a timer configures it at `rate_hz`;
- a later one at the same rate binds its channel and leaves the timer
  running untouched -- configuring it again would reset its counter and cut
  short the pulse in flight on every servo already on it;
- a later one at another rate is `-EBUSY`, and nothing is touched.

`servo_pwm_del()` leaves the timer running, so this holds after the last
servo on a timer is deleted too: a new `servo_pwm_init()` on that timer at
another rate is still `-EBUSY`. Use another timer. A stopped or deleted
servo's pin is held low (`ledc_stop()` with idle level 0). A channel from
`LEDC_CHANNEL_MAX` up is `-EINVAL`.

A timer the app configured itself is shared the same way, and must be at
14-bit duty, which the backend assumes but cannot read back. Rates of 258
and 259 Hz cannot be told from the LEDC driver's own error codes and are
always configured afresh; see [the backend reference](chips.md#esp-idf-ledc).

**STM32 HAL.** A TIM channel the app set up in PWM mode 1 with
output-compare preload (CCMRx OCxPE, which `HAL_TIM_PWM_ConfigChannel()`
sets); a channel without it is `-EINVAL`. The config gives the handle, the
channel and `tick_hz`, the counter clock after the prescaler; the rate is
`tick_hz / (ARR + 1)`, read back at init. A `tick_hz` that makes the rate 0
is `-EINVAL`. What the pin does before the first move and after a stop is
the app's timer and GPIO setup: the library starts and stops the channel,
and does not drive the pin otherwise.

## The PCA9685

`include/pca9685.h`: esp-pca9685-pwm's calls, all of them, plus one.

| Function | Does |
|---|---|
| `pca9685_init(bus, &cfg, &h)` | Probe, check it is a PCA9685, set the frequency, wake, every output off |
| `pca9685_servo(h, ch)` | Output `ch` as a `struct servo_dev`, uncalibrated; NULL outside 0..15 |
| `pca9685_set_raw(h, ch, on, off)` | The two 12-bit counts directly |
| `pca9685_set_duty(h, ch, 0..1)` | Duty cycle; exactly 0 and 1 use the full-OFF and full-ON bits |
| `pca9685_set_pulse_us(h, ch, us)` | A pulse of a given width |
| `pca9685_set_angle(h, ch, deg)` | esp-pca9685-pwm's angle, through `pca9685_set_servo_range()` |
| `pca9685_off` / `pca9685_all_off` | Full off, one output or all sixteen in one write |
| `pca9685_set_frequency(h, hz)` | Moves **all sixteen** |
| `pca9685_frequency(h)` | What the integer prescaler produced |
| `pca9685_sleep(h, sleep)` | Oscillator off, outputs held; or back on |
| `pca9685_reg_read/write` | One register |
| `pca9685_del(h)` | All off, sleep, free. Every `servo_dev` of it goes with it |

On ESP-IDF `bus` is an `i2c_master_bus_handle_t` and the calls return
`esp_err_t`; on the HAL `bus` is an `I2C_HandleTypeDef *` the app has
`HAL_I2C_Init()`ed, `cfg.scl_hz` is ignored, and the calls return 0 or a
negative errno.

**Two ways to drive a servo on it.** `pca9685_servo()` gives a `servo_dev`
with its own calibration, trim and invert per output. `pca9685_set_angle()`
is esp-pca9685-pwm's: one 500-2500 us over 180 degrees range for the whole
chip until `pca9685_set_servo_range()` changes it. Its pulses are the same
as before to the count. The two do not see each other: a `servo_dev`'s
`pulse_us` does not follow `pca9685_off()` or `pca9685_set_angle()` on its
output.

**One rate for sixteen outputs.** A `servo_dev`'s pulses are computed at the
frequency in force when it moves. After `pca9685_set_frequency()`, move
every servo again; a calibration whose `max_us` no longer fits in a period
then refuses its moves with `-EINVAL`.

**A plain PWM or LED driver** still: `pca9685_set_duty()` and
`pca9685_set_raw()` on any output not used as a servo.

## Errors

| errno | ESP-IDF, from `pca9685.h` | When |
|---|---|---|
| `-EINVAL` | `ESP_ERR_INVALID_ARG` | a bad argument, an uncalibrated servo, a pulse longer than the period |
| `-ENXIO` | `ESP_ERR_NOT_FOUND` | nothing answers at the address |
| `-ENODEV` | `ESP_ERR_NOT_FOUND` | something answers, but it is not a PCA9685 |
| `-EBUSY` | `ESP_ERR_INVALID_STATE` | an LEDC timer at another rate; the I2C driver's INVALID_STATE |
| `-ETIMEDOUT` | `ESP_ERR_TIMEOUT` | the bus timed out |
| `-ENOMEM` | `ESP_ERR_NO_MEM` | allocation |
| `-EMSGSIZE` | `ESP_ERR_INVALID_SIZE` | the I2C driver's INVALID_SIZE |
| `-EOPNOTSUPP` | `ESP_ERR_NOT_SUPPORTED` | the I2C driver's NOT_SUPPORTED |
| `-EAGAIN` | -- | HAL: the bus or handle busy |
| `-EIO` | `ESP_FAIL` | the bus failed; any other IDF code |

An IDF code from the I2C driver in that table reaches the app as itself
through `pca9685.h`: it goes to errno and back one to one.

## Changes from esp-pca9685-pwm

`pca9685.h` keeps every type and call: a token-level diff of the
preprocessed header against esp-pca9685-pwm bee3d40 (header and driver as
at b3583c3, the commit esp-forge pins) removes nothing. The I2C traffic of
every call is the same, transaction for transaction, apart from the probe
below. What
changed:

- **`pca9685_init()` probes the address first.** Nothing there is
  `ESP_ERR_NOT_FOUND`, as the header always promised, with nothing
  allocated or added to the bus.
- **I2C errors round-trip.** The codes in the table above come back as they
  were; one with no errno of its own, such as `ESP_ERR_INVALID_CRC`, comes
  back as `ESP_FAIL`.
- **Log tag `"servo"`**, prefix `pca9685:`, instead of the tag `"pca9685"`.
- New: the STM32 HAL, and `pca9685_servo()`.

## Changes from esp-servo-pwm

Its API is not kept; it was in no forge.

| esp-servo-pwm | mcu-servo |
|---|---|
| `servo_init(gpio, cfg, &h)`, heap handle | `servo_pwm_init(&pwm, &cfg)`, the app's struct |
| a free LEDC channel picked for you, timer 0 owned | the app picks timer and channel; timers shared by rate |
| defaults 50 Hz, 500-2500 us, 180 degrees | no defaults: rate and calibration are the app's |
| `servo_set_deg()`, `servo_set_us()` | `servo_set_angle()`, `servo_set_pulse_us()` on the `servo_dev` |
| `servo_del()` | `servo_pwm_del()`; `servo_stop()` to let go and keep the servo |
| duty resolution up to 16 bits by target | 14 bits on every target |
| `esp_err_t` | negative errno |
