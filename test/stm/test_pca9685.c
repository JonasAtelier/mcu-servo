/*
 * The PCA9685 on the STM32 HAL: pca9685.c through src/servo_port_stm.c
 * against fake HAL I2C calls in front of the fake chip in
 * ../pca9685_cases.h. Here also: the HAL's status as errno.
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "stm32_hal_stub.h"

#include "pca9685.h"
#include "pca9685_math.h"
#include "pca9685_regs.h"
#include "servo.h"

static I2C_HandleTypeDef hi2c;

#define BUS		(&hi2c)
#define RET_OK		0
#define RET_INVAL	(-EINVAL)
#define RET_NOT_CHIP	(-ENODEV)
#define RET_ABSENT	(-ENXIO)

static uint8_t chip_address;
static HAL_StatusTypeDef next_status;	/* the next transfer returns it */

#include "../pca9685_cases.h"

static HAL_StatusTypeDef take_status(void)
{
	HAL_StatusTypeDef status = next_status;

	next_status = HAL_OK;
	return status;
}

/*
 * HAL_Delay(n) waits at least n ms -- the real one adds a tick for the
 * partial first one -- so n whole milliseconds are what the chip is owed.
 */
void HAL_Delay(uint32_t Delay)
{
	chip_wait_us(Delay * 1000u);
}

/* HAL_I2C_IsDeviceReady(): HAL_ERROR when no trial was ACKed. */
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t addr,
					uint32_t trials, uint32_t timeout)
{
	assert(h == &hi2c && trials >= 1 && timeout == 100);
	chip_address = (uint8_t)(addr >> 1);
	return absent ? HAL_ERROR : HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t addr,
				    uint16_t reg, uint16_t reg_size,
				    uint8_t *data, uint16_t size,
				    uint32_t timeout)
{
	assert(h == &hi2c && reg_size == I2C_MEMADD_SIZE_8BIT);
	assert((addr >> 1) == chip_address && timeout == 100);
	if (next_status)
		return take_status();
	chip_write((uint8_t)reg, data, size);
	return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t addr,
				   uint16_t reg, uint16_t reg_size,
				   uint8_t *data, uint16_t size,
				   uint32_t timeout)
{
	assert(h == &hi2c && reg_size == I2C_MEMADD_SIZE_8BIT);
	assert((addr >> 1) == chip_address && size == 1 && timeout == 100);
	if (next_status)
		return take_status();
	data[0] = chip_read((uint8_t)reg);
	return HAL_OK;
}

static void test_hal_status(void)
{
	pca9685_handle_t d = open_ok();
	pca9685_config_t c = base_cfg();
	uint8_t v;

	next_status = HAL_TIMEOUT;
	assert(pca9685_reg_read(d, PCA9685_MODE1, &v) == -ETIMEDOUT);
	next_status = HAL_BUSY;
	assert(pca9685_set_raw(d, 0, 0, 100) == -EAGAIN);
	next_status = HAL_ERROR;
	assert(pca9685_set_frequency(d, 50.0f) == -EIO);
	pca9685_del(d);

	chip_reset();
	next_status = HAL_ERROR;
	assert(pca9685_init(BUS, &c, &d) == -EIO);
}

int main(void)
{
	run_pca9685_cases();
	test_hal_status();
	printf("test_pca9685 (stm): all passed\n");
	return 0;
}
