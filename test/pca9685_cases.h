/*
 * The PCA9685's cases, shared by esp/test_pca9685.c and stm/test_pca9685.c:
 * esp-pca9685-pwm's test_driver.c on the errno core, plus pca9685_servo().
 * The fake chip below is register-level and enforces the rules that catch
 * people: PRE_SCALE writes are ignored unless SLEEP is set (Table 4,
 * note 1), and RESTART may only be written once SLEEP has been 0 for
 * 500 us (section 7.3.1.1). Each platform file includes this after
 * providing its I2C fake, which hands every transfer to chip_write() and
 * chip_read(), its delay, which hands the time it waits at least to
 * chip_wait_us(), and:
 *
 *   BUS            the bus argument of pca9685_init()
 *   RET_OK         what a call returns on success
 *   RET_INVAL      ... for a bad argument
 *   RET_NOT_CHIP   ... when what answers does not store what is written
 *   RET_ABSENT     ... when nothing answers at the address
 *   chip_address   the 7-bit address the driver attached to
 */

static uint8_t regs[256];
static int prescale_writes_blocked;
static bool dead_bus;		/* ACKs, stores nothing, reads 0xFF */
static bool absent;		/* nothing ACKs at all */
static int writes;		/* transfers that wrote */
static uint32_t awake_us;	/* waited since SLEEP went to 0 */

static void chip_reset(void)
{
	memset(regs, 0, sizeof(regs));
	regs[PCA9685_MODE1] = PCA9685_SLEEP | PCA9685_ALLCALL; /* Table 5 */
	regs[PCA9685_MODE2] = PCA9685_OUTDRV;			/* Table 6 */
	regs[PCA9685_PRE_SCALE] = PCA9685_PRESCALE_DEFAULT;
	regs[PCA9685_SUBADR1] = 0xE2;
	for (int ch = 0; ch < PCA9685_CHANNELS; ch++)	/* full OFF, Table 7 */
		regs[PCA9685_LED0_ON_L + ch * PCA9685_LED_STRIDE + 3] =
			PCA9685_LED_FULL;
	prescale_writes_blocked = 0;
	dead_bus = false;
	absent = false;
	writes = 0;
	awake_us = 0;
}

static void chip_wait_us(uint32_t us)
{
	awake_us += us;
}

/* One write: the register, then one byte, or four with auto-increment. */
static void chip_write(uint8_t reg, const uint8_t *data, size_t len)
{
	assert(len == 1 || len == 4);
	writes++;
	if (dead_bus)
		return;

	if (reg == PCA9685_PRE_SCALE) {
		assert(len == 1);
		if (!(regs[PCA9685_MODE1] & PCA9685_SLEEP)) {
			prescale_writes_blocked++;
			return;
		}
		regs[PCA9685_PRE_SCALE] = data[0];
		return;
	}
	if (len == 1) {
		uint8_t v = data[0];

		if (reg == PCA9685_MODE1) {
			bool asleep = regs[reg] & PCA9685_SLEEP;
			uint8_t restart = regs[reg] & PCA9685_RESTART;

			if (asleep && !(v & PCA9685_SLEEP))
				awake_us = 0;	/* the oscillator starts */
			/* "SLEEP must be logic 0 for at least 500 us before a
			 * logic 1 is written into the RESTART bit" */
			if (v & PCA9685_RESTART) {
				assert(!(v & PCA9685_SLEEP) && awake_us >= 500);
				restart = 0;	/* write 1 to clear */
			}
			/* Put to sleep while running, RESTART reads 1
			 * (section 7.3.1.1); a 0 written to it does nothing. */
			if (!asleep && (v & PCA9685_SLEEP))
				restart = PCA9685_RESTART;
			v = (uint8_t)((v & ~PCA9685_RESTART) | restart);
		}
		regs[reg] = v;
		return;
	}
	/* Four-byte channel write, relying on auto-increment. */
	assert(regs[PCA9685_MODE1] & PCA9685_AI);
	for (size_t i = 0; i < 4; i++)
		regs[(reg + i) & 0xFF] = data[i];

	/* ALL_LED_* is not storage: writing it loads every channel at once. */
	if (reg == PCA9685_ALL_LED_ON_L) {
		for (int ch = 0; ch < PCA9685_CHANNELS; ch++) {
			int r = PCA9685_LED0_ON_L + ch * PCA9685_LED_STRIDE;

			for (int i = 0; i < 4; i++)
				regs[r + i] = data[i];
		}
	}
}

static uint8_t chip_read(uint8_t reg)
{
	return dead_bus ? 0xFF : regs[reg];
}

static uint16_t chan_on(int ch)
{
	int r = pca9685_channel_reg(ch);

	return (uint16_t)(regs[r] | (regs[r + 1] << 8));
}

static uint16_t chan_off(int ch)
{
	int r = pca9685_channel_reg(ch);

	return (uint16_t)(regs[r + 2] | (regs[r + 3] << 8));
}

static pca9685_config_t base_cfg(void)
{
	pca9685_config_t c = { .frequency_hz = 50.0f, .totem_pole = true };

	return c;
}

static pca9685_handle_t open_ok(void)
{
	pca9685_config_t c = base_cfg();
	pca9685_handle_t d = NULL;

	chip_reset();
	assert(pca9685_init(BUS, &c, &d) == RET_OK);
	return d;
}

static void test_init(void)
{
	pca9685_config_t c = base_cfg();
	pca9685_handle_t d = NULL;

	chip_reset();
	assert(pca9685_init(BUS, &c, &d) == RET_OK);
	assert(chip_address == PCA9685_ADDR_DEFAULT);
	/* The prescaler must have actually landed -- never blocked by SLEEP. */
	assert(prescale_writes_blocked == 0);
	assert(regs[PCA9685_PRE_SCALE] ==
	       pca9685_prescale_for(50.0f, PCA9685_OSC_HZ));
	/* Out of reset SLEEP is 1; the driver must clear it or nothing runs. */
	assert(!(regs[PCA9685_MODE1] & PCA9685_SLEEP));
	/* Auto-increment on, or the four-byte channel writes do not work. */
	assert(regs[PCA9685_MODE1] & PCA9685_AI);
	assert(regs[PCA9685_MODE2] & PCA9685_OUTDRV);
	assert(!(regs[PCA9685_MODE2] & PCA9685_INVRT));
	/* Nothing should twitch at start-up: every output full off. */
	for (int ch = 0; ch < PCA9685_CHANNELS; ch++)
		assert(chan_off(ch) & (PCA9685_LED_FULL << 8));
	assert(fabs(pca9685_frequency(d) - 50.0f) < 1.0f);
	assert(pca9685_del(d) == RET_OK);
	/* Teardown leaves it asleep with the outputs off. */
	assert(regs[PCA9685_MODE1] & PCA9685_SLEEP);

	/* Any address in the pin-selectable range; open drain, inverted */
	chip_reset();
	c.address = 0x7F;
	c.totem_pole = false;
	c.invert = true;
	assert(pca9685_init(BUS, &c, &d) == RET_OK);
	assert(chip_address == 0x7F);
	assert(!(regs[PCA9685_MODE2] & PCA9685_OUTDRV));
	assert(regs[PCA9685_MODE2] & PCA9685_INVRT);
	pca9685_del(d);

	/* Frequency 0 is the 50 Hz default */
	chip_reset();
	c = base_cfg();
	c.frequency_hz = 0.0f;
	assert(pca9685_init(BUS, &c, &d) == RET_OK);
	assert(fabs(pca9685_frequency(d) - 50.0f) < 1.0f);
	pca9685_del(d);

	chip_reset();
	c = base_cfg();
	c.address = 0x20;
	assert(pca9685_init(BUS, &c, &d) == RET_INVAL);
	chip_reset();
	c = base_cfg();
	c.frequency_hz = 5000.0f;
	assert(pca9685_init(BUS, &c, &d) == RET_INVAL);

	/* A bus that acknowledges but stores nothing is not a PCA9685. */
	chip_reset();
	c = base_cfg();
	dead_bus = true;
	assert(pca9685_init(BUS, &c, &d) == RET_NOT_CHIP);

	/* Nothing at the address: refused before anything is written. */
	chip_reset();
	absent = true;
	assert(pca9685_init(BUS, &c, &d) == RET_ABSENT);
	assert(writes == 0);
}

static void test_frequency_change_sleeps_first(void)
{
	pca9685_handle_t d = open_ok();

	prescale_writes_blocked = 0;
	assert(pca9685_set_frequency(d, 1000.0f) == RET_OK);
	assert(prescale_writes_blocked == 0 &&
	       "must sleep before writing PRE_SCALE");
	assert(regs[PCA9685_PRE_SCALE] ==
	       pca9685_prescale_for(1000.0f, PCA9685_OSC_HZ));
	/* And it must be awake again afterwards. */
	assert(!(regs[PCA9685_MODE1] & PCA9685_SLEEP));
	assert(fabs(pca9685_frequency(d) - 1000.0f) < 20.0f);

	assert(pca9685_set_frequency(d, 10.0f) == RET_INVAL);
	assert(pca9685_set_frequency(d, 5000.0f) == RET_INVAL);

	pca9685_del(d);
}

/*
 * Asleep with outputs running, the chip reads RESTART 1. Waking must write
 * it 0 and only set it once SLEEP has been 0 for 500 us; the fake asserts
 * that, whether the wake is pca9685_sleep()'s or set_frequency()'s.
 */
static void test_sleep_and_wake(void)
{
	pca9685_handle_t d = open_ok();

	assert(pca9685_set_duty(d, 2, 0.5f) == RET_OK);
	assert(pca9685_sleep(d, true) == RET_OK);
	assert(regs[PCA9685_MODE1] & PCA9685_SLEEP);
	assert(regs[PCA9685_MODE1] & PCA9685_RESTART);
	assert(pca9685_sleep(d, false) == RET_OK);
	assert(!(regs[PCA9685_MODE1] & PCA9685_SLEEP));
	assert(!(regs[PCA9685_MODE1] & PCA9685_RESTART));
	assert(chan_off(2) == 2048);

	/* Asleep twice: the second sleep keeps the pending restart, and the
	 * wake resumes the outputs */
	assert(pca9685_sleep(d, true) == RET_OK);
	assert(pca9685_sleep(d, true) == RET_OK);
	assert(regs[PCA9685_MODE1] & PCA9685_RESTART);
	assert(pca9685_sleep(d, false) == RET_OK);
	assert(!(regs[PCA9685_MODE1] & (PCA9685_SLEEP | PCA9685_RESTART)));
	assert(chan_off(2) == 2048);

	/* A frequency change on a sleeping chip wakes it the same way */
	assert(pca9685_sleep(d, true) == RET_OK);
	assert(regs[PCA9685_MODE1] & PCA9685_RESTART);
	assert(pca9685_set_frequency(d, 60.0f) == RET_OK);
	assert(prescale_writes_blocked == 0);
	assert(regs[PCA9685_PRE_SCALE] ==
	       pca9685_prescale_for(60.0f, PCA9685_OSC_HZ));
	assert(!(regs[PCA9685_MODE1] & (PCA9685_SLEEP | PCA9685_RESTART)));
	pca9685_del(d);
}

/* The PCA9685 as a plain PWM or LED driver. */
static void test_outputs(void)
{
	pca9685_handle_t d = open_ok();
	uint8_t v = 0;

	assert(pca9685_set_raw(d, 3, 100, 500) == RET_OK);
	assert(chan_on(3) == 100 && chan_off(3) == 500);

	/* Duty 0 and 1 use the full-OFF and full-ON bits, not a count of
	 * 0 or 4095 -- otherwise there is a one-tick glitch every period. */
	assert(pca9685_set_duty(d, 4, 0.0f) == RET_OK);
	assert(chan_off(4) & (PCA9685_LED_FULL << 8));
	assert(pca9685_set_duty(d, 4, 1.0f) == RET_OK);
	assert(chan_on(4) & (PCA9685_LED_FULL << 8));
	assert(!(chan_off(4) & (PCA9685_LED_FULL << 8)));

	assert(pca9685_set_duty(d, 5, 0.5f) == RET_OK);
	assert(chan_on(5) == 0 && chan_off(5) == 2048);

	/* A 1.5 ms pulse at 50 Hz. */
	assert(pca9685_set_pulse_us(d, 6, 1500.0f) == RET_OK);
	assert(chan_off(6) == (uint16_t)pca9685_us_to_counts(
					1500.0f, pca9685_frequency(d)));
	/* Longer than a period is refused. */
	assert(pca9685_set_pulse_us(d, 6, 25000.0f) == RET_INVAL);

	assert(pca9685_off(d, 6) == RET_OK);
	assert(chan_off(6) & (PCA9685_LED_FULL << 8));

	/* Every channel at once. */
	assert(pca9685_set_duty(d, 0, 0.5f) == RET_OK);
	assert(pca9685_all_off(d) == RET_OK);
	for (int ch = 0; ch < PCA9685_CHANNELS; ch++)
		assert(chan_off(ch) & (PCA9685_LED_FULL << 8));

	assert(pca9685_set_raw(d, 16, 0, 100) == RET_INVAL);
	assert(pca9685_set_raw(d, -1, 0, 100) == RET_INVAL);
	assert(pca9685_set_raw(d, 0, 5000, 100) == RET_INVAL);
	assert(pca9685_off(d, 99) == RET_INVAL);

	assert(pca9685_reg_write(d, PCA9685_SUBADR2, 0x5A) == RET_OK);
	assert(pca9685_reg_read(d, PCA9685_SUBADR2, &v) == RET_OK && v == 0x5A);
	pca9685_del(d);
}

/* esp-pca9685-pwm's own servo calls, unchanged. */
static void test_legacy_angle(void)
{
	pca9685_handle_t d = open_ok();
	uint16_t mid, lo, hi;

	assert(pca9685_set_angle(d, 0, 90.0f) == RET_OK);
	mid = chan_off(0);
	assert(pca9685_set_angle(d, 0, 0.0f) == RET_OK);
	lo = chan_off(0);
	assert(pca9685_set_angle(d, 0, 180.0f) == RET_OK);
	hi = chan_off(0);
	assert(lo < mid && mid < hi);
	/* The 500-2500 us default: 500, 1500 and 2500 us of a 19988.8 us
	 * period (prescale 121, 50.03 Hz) are 102.5, 307.4 and 512.3 counts */
	assert(lo == 102 && mid == 307 && hi == 512);

	/* Beyond the ends clamps rather than driving past the stop. */
	assert(pca9685_set_angle(d, 0, 999.0f) == RET_OK);
	assert(chan_off(0) == hi);
	assert(pca9685_set_angle(d, 0, -999.0f) == RET_OK);
	assert(chan_off(0) == lo);

	/* A narrower range gives narrower pulses for the same angles. */
	pca9685_set_servo_range(d, 1000.0f, 2000.0f, 180.0f);
	assert(pca9685_set_angle(d, 0, 0.0f) == RET_OK);
	assert(chan_off(0) > lo);
	/* Nonsense ranges are ignored, not stored. */
	pca9685_set_servo_range(d, 2000.0f, 1000.0f, 180.0f);
	assert(pca9685_set_angle(d, 0, 0.0f) == RET_OK);
	assert(chan_off(0) > lo);

	pca9685_del(d);
}

/* Each output as a struct servo_dev. */
static void test_servo_dev(void)
{
	static const struct servo_calibration c = {
		.min_us = 1000, .max_us = 2000, .range_deg = 180.0f,
	};
	pca9685_handle_t d = open_ok();
	struct servo_dev *servo, *other;
	float hz = pca9685_frequency(d);

	assert(pca9685_servo(NULL, 0) == NULL);
	assert(pca9685_servo(d, -1) == NULL);
	assert(pca9685_servo(d, 16) == NULL);
	servo = pca9685_servo(d, 7);
	other = pca9685_servo(d, 15);
	assert(servo && other && servo != other);
	assert(strcmp(servo->name, "pca9685") == 0);

	/* Uncalibrated: refused, the output still full off */
	assert(servo_set_angle(servo, 90.0f) == -EINVAL);
	assert(chan_off(7) & (PCA9685_LED_FULL << 8));

	assert(servo_set_calibration(servo, &c) == 0);
	assert(servo_set_angle(servo, 90.0f) == 0);
	assert(chan_on(7) == 0 && chan_off(7) ==
	       (uint16_t)pca9685_us_to_counts(1500.0f, hz));
	/* 1500 us of 20 ms at 50 Hz (prescale 121, 50.03 Hz): 307 counts */
	assert(chan_off(7) == 307 && servo->pulse_us == 1500);
	assert(servo_set_angle(servo, 180.0f) == 0);
	assert(chan_off(7) == (uint16_t)pca9685_us_to_counts(2000.0f, hz));
	/* the others untouched */
	assert(chan_off(15) & (PCA9685_LED_FULL << 8));

	/* stop is full OFF on that channel only */
	assert(servo_set_calibration(other, &c) == 0);
	assert(servo_set_angle(other, 0.0f) == 0);
	assert(servo_stop(servo) == 0 && servo->pulse_us == 0);
	assert(chan_off(7) & (PCA9685_LED_FULL << 8));
	assert(!(chan_off(15) & (PCA9685_LED_FULL << 8)));

	/* At 1 kHz (prescale 5, 1017 Hz) the period is 983 us: neither 2000
	 * nor 1000 us fits */
	assert(pca9685_set_frequency(d, 1000.0f) == RET_OK);
	assert(servo_set_calibration(servo, &c) == -EINVAL);
	assert(servo_set_angle(other, 180.0f) == -EINVAL);
	assert(servo_set_angle(other, 0.0f) == -EINVAL);
	pca9685_del(d);
}

static void test_null_arguments(void)
{
	pca9685_config_t c = base_cfg();
	pca9685_handle_t d = NULL;
	uint8_t v;

	assert(pca9685_init(BUS, NULL, &d) == RET_INVAL);
	assert(pca9685_init(BUS, &c, NULL) == RET_INVAL);
	assert(pca9685_set_raw(NULL, 0, 0, 0) == RET_INVAL);
	assert(pca9685_set_duty(NULL, 0, 0.5f) == RET_INVAL);
	assert(pca9685_set_pulse_us(NULL, 0, 1500.0f) == RET_INVAL);
	assert(pca9685_set_angle(NULL, 0, 90.0f) == RET_INVAL);
	assert(pca9685_off(NULL, 0) == RET_INVAL);
	assert(pca9685_all_off(NULL) == RET_INVAL);
	assert(pca9685_set_frequency(NULL, 50.0f) == RET_INVAL);
	assert(pca9685_sleep(NULL, true) == RET_INVAL);
	assert(pca9685_del(NULL) == RET_INVAL);
	assert(pca9685_reg_read(NULL, 0, &v) == RET_INVAL);
	assert(pca9685_reg_write(NULL, 0, 0) == RET_INVAL);
	assert(pca9685_frequency(NULL) == 0.0f);
	pca9685_set_servo_range(NULL, 1.0f, 2.0f, 3.0f); /* must not crash */

	d = open_ok();
	assert(pca9685_reg_read(d, 0, NULL) == RET_INVAL);
	pca9685_del(d);
}

static void run_pca9685_cases(void)
{
	test_init();
	test_frequency_change_sleeps_first();
	test_sleep_and_wake();
	test_outputs();
	test_legacy_angle();
	test_servo_dev();
	test_null_arguments();
}
