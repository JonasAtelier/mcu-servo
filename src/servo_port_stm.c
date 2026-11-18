/*
 * servo_priv.h on the STM32 HAL: the bus layer, the output port and the
 * delay. stm-forge compiles every .c in src/, so under ESP-IDF this file is
 * empty; the ESP-IDF CMakeLists never lists it.
 */
#ifndef ESP_PLATFORM

#include <string.h>

#include "servo.h"
#include "servo_priv.h"

/*
 * The core is always built; each backend only under its own option. With
 * the library built and no backend picked, say so: this file is compiled
 * only when the library is, so this is the CMakeLists' "no backend" warning
 * for every build that never reads that file (stm-forge, a hand-made
 * project).
 */
#if !(CONFIG_SERVO_PWM || CONFIG_SERVO_PCA9685)
#warning "servo: no backend is selected; define CONFIG_SERVO_<BACKEND>=1 (stm-forge: SERVO_<BACKEND> = y)"
#endif

#if defined(HAL_I2C_MODULE_ENABLED) || defined(HAL_SPI_MODULE_ENABLED) || \
    defined(HAL_UART_MODULE_ENABLED)
/* Nothing to undo: the handle is the app's, and nothing was added to it. */
static void servo_hal_detach(struct servo_bus *bus)
{
	(void)bus;
}
#endif

#ifdef HAL_I2C_MODULE_ENABLED
/*
 * HAL_I2C_IsDeviceReady() returns HAL_ERROR when no trial was ACKed, which is
 * -ENXIO. A busy or stuck bus keeps its own errno. On the F4, no pull-ups or
 * SDA held low sets the peripheral's BUSY flag, so the HAL gives up after
 * 25 ms with HAL_BUSY, -EAGAIN; HAL_TIMEOUT, -ETIMEDOUT, means a START never
 * completed.
 */
int servo_i2c_probe(servo_i2c_bus handle, uint8_t address,
		    uint32_t timeout_ms)
{
	HAL_StatusTypeDef status;

	status = HAL_I2C_IsDeviceReady(handle, (uint16_t)(address << 1), 1,
				       timeout_ms);
	return status == HAL_ERROR ? -ENXIO : servo_hal_errno(status);
}

/* HAL_I2C_Mem_Read(): cmd, repeated start, burst. Mem_Write(): cmd, data. */
static int servo_i2c_transfer(struct servo_bus *bus,
			      uint8_t cmd,
			      const uint8_t *tx, size_t tx_len, uint8_t *rx,
			      size_t rx_len)
{
	uint16_t address = (uint16_t)(bus->i2c.address << 1);

	if ((tx_len && rx_len) || tx_len > SERVO_BUS_MAX_LEN ||
	    rx_len > UINT16_MAX)
		return -EINVAL;
	if (rx_len)
		return servo_hal_errno(HAL_I2C_Mem_Read(bus->i2c.handle,
							address, cmd,
							I2C_MEMADD_SIZE_8BIT,
							rx, (uint16_t)rx_len,
							bus->timeout_ms));
	if (tx_len)
		return servo_hal_errno(HAL_I2C_Mem_Write(bus->i2c.handle,
							 address, cmd,
							 I2C_MEMADD_SIZE_8BIT,
							 (uint8_t *)tx,
							 (uint16_t)tx_len,
							 bus->timeout_ms));
	/* A bare command, for a part with no registers: its own START, STOP. */
	return servo_hal_errno(HAL_I2C_Master_Transmit(bus->i2c.handle,
						       address, &cmd, 1,
						       bus->timeout_ms));
}

static const struct servo_bus_ops servo_i2c_ops = {
	.kind = SERVO_BUS_I2C,
	.transfer = servo_i2c_transfer,
	.detach = servo_hal_detach,
};

int servo_i2c_attach(struct servo_bus *bus,
		     servo_i2c_bus handle,
		     uint8_t address, uint32_t scl_hz, uint32_t timeout_ms)
{
	(void)scl_hz;
	bus->ops = &servo_i2c_ops;
	bus->timeout_ms = timeout_ms;
	bus->i2c.handle = handle;
	bus->i2c.address = address;
	return 0;
}
#endif /* HAL_I2C_MODULE_ENABLED */

#ifdef HAL_SPI_MODULE_ENABLED
/*
 * One transaction under a software chip select: CS low, cmd and tx out --
 * full duplex with zeros after them when there is something to read -- CS
 * high. CS goes high even when the transfer failed; see servo_bus_ops.
 */
static int servo_spi_transfer(struct servo_bus *bus,
			      uint8_t cmd,
			      const uint8_t *tx, size_t tx_len, uint8_t *rx,
			      size_t rx_len)
{
	uint8_t out[1 + SERVO_BUS_MAX_LEN] = { 0 };
	uint8_t in[1 + SERVO_BUS_MAX_LEN];
	uint16_t n = (uint16_t)(1 + tx_len + rx_len);
	HAL_StatusTypeDef status;

	if (tx_len + rx_len > SERVO_BUS_MAX_LEN)
		return -EINVAL;
	out[0] = cmd;
	if (tx_len)
		memcpy(out + 1, tx, tx_len);

	HAL_GPIO_WritePin(bus->spi.cs_port, bus->spi.cs_pin, GPIO_PIN_RESET);
	if (rx_len)
		status = HAL_SPI_TransmitReceive(bus->spi.handle, out, in, n,
						 bus->timeout_ms);
	else
		status = HAL_SPI_Transmit(bus->spi.handle, out, n,
					  bus->timeout_ms);
	HAL_GPIO_WritePin(bus->spi.cs_port, bus->spi.cs_pin, GPIO_PIN_SET);
	if (status == HAL_OK && rx_len)
		memcpy(rx, in + 1 + tx_len, rx_len);
	return servo_hal_errno(status);
}

/* One frame, full duplex: rx[i] is what came back while tx[i] went out. */
static int servo_spi_exchange(struct servo_bus *bus,
			      const uint8_t *tx,
			      uint8_t *rx, size_t len)
{
	uint8_t out[1 + SERVO_BUS_MAX_LEN];
	uint8_t in[1 + SERVO_BUS_MAX_LEN];
	HAL_StatusTypeDef status;

	if (!len || len > sizeof(out))
		return -EINVAL;
	memcpy(out, tx, len);

	HAL_GPIO_WritePin(bus->spi.cs_port, bus->spi.cs_pin, GPIO_PIN_RESET);
	status = HAL_SPI_TransmitReceive(bus->spi.handle, out, in,
					 (uint16_t)len, bus->timeout_ms);
	HAL_GPIO_WritePin(bus->spi.cs_port, bus->spi.cs_pin, GPIO_PIN_SET);
	if (status == HAL_OK)
		memcpy(rx, in, len);
	return servo_hal_errno(status);
}

static const struct servo_bus_ops servo_spi_ops = {
	.kind = SERVO_BUS_SPI,
	.transfer = servo_spi_transfer,
	.exchange = servo_spi_exchange,
	.detach = servo_hal_detach,
};

int servo_spi_attach(struct servo_bus *bus,
		     SPI_HandleTypeDef *handle,
		     GPIO_TypeDef *cs_port, uint16_t cs_pin,
		     uint32_t timeout_ms)
{
	bus->ops = &servo_spi_ops;
	bus->timeout_ms = timeout_ms;
	bus->spi.handle = handle;
	bus->spi.cs_port = cs_port;
	bus->spi.cs_pin = cs_pin;
	/* Deselected first, so the first transfer starts on a falling edge. */
	HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_SET);
	return 0;
}
#endif /* HAL_SPI_MODULE_ENABLED */

#ifdef HAL_UART_MODULE_ENABLED
/*
 * Blocking HAL_UART_Receive() hands back whatever already sits in the data
 * register, so a late byte from an earlier exchange would be read as the
 * start of this reply. Drop it first: clear an overrun, then read the data
 * register empty (on the F7 that macro is a flush request). Bounded by the
 * timeout, in case the line never goes quiet.
 */
static void servo_uart_drop_input(struct servo_bus *bus)
{
	UART_HandleTypeDef *handle = bus->uart;
	uint32_t start = HAL_GetTick();

	__HAL_UART_CLEAR_OREFLAG(handle);
	while (__HAL_UART_GET_FLAG(handle, UART_FLAG_RXNE) &&
	       (uint32_t)(HAL_GetTick() - start) <= bus->timeout_ms)
		__HAL_UART_FLUSH_DRREGISTER(handle);
}

static int servo_uart_transfer(struct servo_bus *bus,
			       uint8_t cmd,
			       const uint8_t *tx, size_t tx_len,
			       uint8_t *rx, size_t rx_len)
{
	uint8_t out[1 + SERVO_BUS_MAX_LEN];
	int ret;

	if (tx_len > SERVO_BUS_MAX_LEN || rx_len > UINT16_MAX)
		return -EINVAL;
	out[0] = cmd;
	if (tx_len)
		memcpy(out + 1, tx, tx_len);

	servo_uart_drop_input(bus);
	ret = servo_hal_errno(HAL_UART_Transmit(bus->uart, out,
						(uint16_t)(1 + tx_len),
						bus->timeout_ms));
	if (ret || !rx_len)
		return ret;
	/* A reply that stops short runs into the timeout: HAL_TIMEOUT. */
	return servo_hal_errno(HAL_UART_Receive(bus->uart, rx,
						(uint16_t)rx_len,
						bus->timeout_ms));
}

/*
 * The echo, a byte at a time. Blocking HAL_UART_Transmit() of a whole
 * datagram never reads the line while it sends, and the data register holds
 * one byte: on a single wire the second echoed byte overruns the first
 * (ORE), the rest are lost, and the reply that follows comes up misaligned.
 * So send one byte and take its echo before the next goes out; only one
 * byte is ever in flight, and nothing overruns.
 *
 * The other way, a half-duplex USART (HDSEL) that leaves the receiver deaf
 * while it sends, needs the app to set the handle up so and the driver to
 * switch direction around each datagram -- a different contract with the
 * app, and nothing this library can check from a handle.
 */
static int servo_uart_exchange(struct servo_bus *bus,
			       const uint8_t *tx,
			       uint8_t *rx, size_t len)
{
	size_t i;
	int ret;

	if (!len || len > 1 + SERVO_BUS_MAX_LEN)
		return -EINVAL;

	servo_uart_drop_input(bus);
	for (i = 0; i < len; i++) {
		uint8_t byte = tx[i];

		ret = servo_hal_errno(HAL_UART_Transmit(bus->uart,
							&byte, 1,
							bus->timeout_ms));
		if (ret)
			return ret;
		ret = servo_hal_errno(HAL_UART_Receive(bus->uart,
						       &rx[i], 1,
						       bus->timeout_ms));
		if (ret)
			return ret;
	}
	return 0;
}

static int servo_uart_receive(struct servo_bus *bus,
			      uint8_t *rx,
			      size_t rx_len)
{
	if (rx_len > UINT16_MAX)
		return -EINVAL;
	return servo_hal_errno(HAL_UART_Receive(bus->uart, rx,
						(uint16_t)rx_len,
						bus->timeout_ms));
}

static const struct servo_bus_ops servo_uart_ops = {
	.kind = SERVO_BUS_UART,
	.transfer = servo_uart_transfer,
	.exchange = servo_uart_exchange,
	.receive = servo_uart_receive,
	.detach = servo_hal_detach,
};

int servo_uart_attach(struct servo_bus *bus,
		      UART_HandleTypeDef *handle,
		      uint32_t timeout_ms)
{
	bus->ops = &servo_uart_ops;
	bus->timeout_ms = timeout_ms;
	bus->uart = handle;
	return 0;
}
#endif /* HAL_UART_MODULE_ENABLED */

#ifdef HAL_TIM_MODULE_ENABLED
/* ------------------------------------------------------------ output -- */

/*
 * pulse_us in timer counts, rounded to the nearest. A compare past ARR would
 * hold the pin high the whole period: -EINVAL, as is a pulse that leaves no
 * low time at all.
 */
static int servo_output_compare(const struct servo_output *output,
				uint32_t pulse_us, uint32_t *compare)
{
	uint64_t counts = ((uint64_t)pulse_us * output->tick_hz + 500000u) /
			  1000000u;

	if (counts > __HAL_TIM_GET_AUTORELOAD(output->tim))
		return -EINVAL;
	*compare = (uint32_t)counts;
	return 0;
}

int servo_output_check(const struct servo_output *output, uint32_t pulse_us)
{
	uint32_t compare;

	return servo_output_compare(output, pulse_us, &compare);
}

/*
 * Is output-compare preload on for the channel (CCMRx OCxPE)? Without it a
 * compare written mid-period takes effect at once and the pulse in flight
 * can come out short or doubled. HAL_TIM_PWM_ConfigChannel() sets it, so a
 * CubeMX PWM channel has it.
 */
static bool servo_output_preload(TIM_HandleTypeDef *tim, uint32_t channel)
{
	switch (channel) {
	case TIM_CHANNEL_1:
		return tim->Instance->CCMR1 & TIM_CCMR1_OC1PE;
	case TIM_CHANNEL_2:
		return tim->Instance->CCMR1 & TIM_CCMR1_OC2PE;
	case TIM_CHANNEL_3:
		return tim->Instance->CCMR2 & TIM_CCMR2_OC3PE;
	default:
		return tim->Instance->CCMR2 & TIM_CCMR2_OC4PE;
	}
}

int servo_output_attach(struct servo_output *output, TIM_HandleTypeDef *tim,
			uint32_t channel, uint32_t tick_hz)
{
	uint32_t period;

	if (!tim || !tick_hz || (channel != TIM_CHANNEL_1 &&
	    channel != TIM_CHANNEL_2 && channel != TIM_CHANNEL_3 &&
	    channel != TIM_CHANNEL_4))
		return -EINVAL;
	if (!servo_output_preload(tim, channel))
		return -EINVAL;
	period = __HAL_TIM_GET_AUTORELOAD(tim) + 1u;
	/* ARR 0xFFFFFFFF on a 32-bit timer wraps the period to 0. */
	if (!period)
		return -EINVAL;
	output->tim = tim;
	output->channel = channel;
	output->tick_hz = tick_hz;
	output->rate_hz = tick_hz / period;
	return 0;
}

int servo_output_start(struct servo_output *output, uint32_t pulse_us)
{
	uint32_t compare;
	int ret;

	ret = servo_output_compare(output, pulse_us, &compare);
	if (ret)
		return ret;
	__HAL_TIM_SET_COMPARE(output->tim, output->channel, compare);
	return servo_hal_errno(HAL_TIM_PWM_Start(output->tim,
						 output->channel));
}

int servo_output_write_us(struct servo_output *output, uint32_t pulse_us)
{
	uint32_t compare;
	int ret;

	ret = servo_output_compare(output, pulse_us, &compare);
	if (ret)
		return ret;
	__HAL_TIM_SET_COMPARE(output->tim, output->channel, compare);
	return 0;
}

/*
 * Compare 0, then the channel off. With preload on, which the attach
 * requires, the 0 lands in the preload register and reaches the active
 * compare only at the next update event, by which time the channel is
 * already off: it does not hold the pin low. What the pin does once the
 * channel is off is the app's timer and GPIO setup, not this library's.
 */
void servo_output_stop(struct servo_output *output)
{
	__HAL_TIM_SET_COMPARE(output->tim, output->channel, 0);
	HAL_TIM_PWM_Stop(output->tim, output->channel);
}
#endif /* HAL_TIM_MODULE_ENABLED */

/* ------------------------------------------------------------- delay -- */

/*
 * Whole milliseconds, rounded up. HAL_Delay() already adds one tick to what
 * it is asked, because its first tick may be about to fire (STM32CubeF4
 * stm32f4xx_hal.c, HAL_Delay()), so 500 us waits 1-2 ms: long, never short
 * -- as long as the app has not replaced the weak HAL_Delay() with one that
 * drops that tick. It busy-waits on SysTick, so never call it from an
 * interrupt at or above SysTick's priority.
 */
void servo_delay_us(uint32_t us)
{
	HAL_Delay((us + 999u) / 1000u);
}

#endif /* !ESP_PLATFORM */
