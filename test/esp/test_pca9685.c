/*
 * The PCA9685 on ESP-IDF: pca9685.c through src/servo_port_esp.c against a
 * fake I2C master driver in front of the fake chip in ../pca9685_cases.h.
 * Here also: the esp_err_t round trip, I2C traffic exactly as
 * esp-pca9685-pwm's (a two-byte write, a five-byte write, a one-byte read).
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "pca9685.h"
#include "pca9685_math.h"
#include "pca9685_regs.h"
#include "servo.h"

#define BUS		NULL
#define RET_OK		ESP_OK
#define RET_INVAL	ESP_ERR_INVALID_ARG
#define RET_NOT_CHIP	ESP_ERR_NOT_FOUND
#define RET_ABSENT	ESP_ERR_NOT_FOUND

static uint8_t chip_address;
static uint32_t chip_scl_hz;
static int devices;			/* added and not removed */
static esp_err_t next_err;		/* the next transfer fails with it */
static esp_err_t add_err;		/* the next add_device fails with it */

#include "../pca9685_cases.h"

/* esp-pca9685-pwm waited with esp_rom_delay_us(); so does the port. */
void esp_rom_delay_us(uint32_t us)
{
	chip_wait_us(us);
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address,
			   int timeout_ms)
{
	(void)bus;
	(void)timeout_ms;
	chip_address = (uint8_t)address;
	return absent ? ESP_ERR_NOT_FOUND : ESP_OK;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
				    const i2c_device_config_t *config,
				    i2c_master_dev_handle_t *out)
{
	static int device;

	(void)bus;
	if (add_err) {
		esp_err_t err = add_err;

		add_err = ESP_OK;
		return err;
	}
	assert(config->dev_addr_length == I2C_ADDR_BIT_LEN_7);
	chip_address = (uint8_t)config->device_address;
	chip_scl_hz = config->scl_speed_hz;
	*out = (i2c_master_dev_handle_t)&device;
	devices++;
	return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
	(void)device;
	devices--;
	return ESP_OK;
}

static esp_err_t take_err(void)
{
	esp_err_t err = next_err;

	next_err = ESP_OK;
	return err;
}

/* esp-pca9685-pwm wrote two bytes for a register, five for a channel. */
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
			      const uint8_t *data, size_t length,
			      int timeout_ms)
{
	(void)device;
	assert(timeout_ms == 100);
	assert(length == 2 || length == 5);
	if (next_err)
		return take_err();
	chip_write(data[0], data + 1, length - 1);
	return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
				      const uint8_t *tx, size_t tx_length,
				      uint8_t *rx, size_t rx_length,
				      int timeout_ms)
{
	(void)device;
	assert(timeout_ms == 100);
	assert(tx_length == 1 && rx_length == 1);
	if (next_err)
		return take_err();
	rx[0] = chip_read(tx[0]);
	return ESP_OK;
}


/*
 * ESP_ERR_* from the I2C driver reach the app as themselves: the common
 * codes go to errno and back 1:1, never folded into ESP_FAIL.
 */
static void test_esp_err_round_trip(void)
{
	static const esp_err_t codes[] = {
		ESP_ERR_TIMEOUT, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE,
		ESP_ERR_NO_MEM, ESP_ERR_NOT_FOUND, ESP_ERR_INVALID_SIZE,
		ESP_ERR_NOT_SUPPORTED,
	};
	pca9685_config_t c = base_cfg();
	pca9685_handle_t d = open_ok();
	uint8_t v;

	for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
		next_err = codes[i];
		assert(pca9685_reg_read(d, PCA9685_MODE1, &v) == codes[i]);
		next_err = codes[i];
		assert(pca9685_reg_write(d, PCA9685_MODE1, 0) == codes[i]);
		next_err = codes[i];
		assert(pca9685_set_raw(d, 0, 0, 100) == codes[i]);
		next_err = codes[i];
		assert(pca9685_set_frequency(d, 50.0f) == codes[i]);
	}
	/* One with no errno of its own comes back as ESP_FAIL */
	next_err = ESP_ERR_INVALID_CRC;
	assert(pca9685_reg_read(d, PCA9685_MODE1, &v) == ESP_FAIL);
	pca9685_del(d);
	assert(devices == 0);

	/* add_device's own errors, and nothing left added */
	for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
		chip_reset();
		add_err = codes[i];
		assert(pca9685_init(BUS, &c, &d) == codes[i]);
		assert(devices == 0);
	}

	/* A transfer failing during init: its code, the device removed */
	chip_reset();
	next_err = ESP_ERR_TIMEOUT;
	assert(pca9685_init(BUS, &c, &d) == ESP_ERR_TIMEOUT);
	assert(devices == 0);
}

/* What esp-pca9685-pwm passed to the I2C driver, still passed. */
static void test_device_config(void)
{
	pca9685_config_t c = base_cfg();
	pca9685_handle_t d;

	chip_reset();
	assert(pca9685_init(BUS, &c, &d) == ESP_OK);
	assert(chip_scl_hz == 400000 && devices == 1);
	pca9685_del(d);
	c.scl_hz = 1000000;
	chip_reset();
	assert(pca9685_init(BUS, &c, &d) == ESP_OK);
	assert(chip_scl_hz == 1000000);
	pca9685_del(d);
	assert(devices == 0);
}

int main(void)
{
	run_pca9685_cases();
	test_esp_err_round_trip();
	test_device_config();
	printf("test_pca9685 (esp): all passed\n");
	return 0;
}
