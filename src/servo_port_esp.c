/*
 * servo_priv.h on ESP-IDF: the bus layer, the output port and the delay.
 * The CMakeLists lists this file; stm-forge compiles every .c in src/, so
 * outside ESP-IDF it is empty.
 */
#ifdef ESP_PLATFORM

#include <string.h>

#include "esp_err.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "servo.h"
#include "servo_priv.h"

/* 400 kHz when a caller says 0: fast mode. */
#define DEFAULT_SCL_HZ		400000

/*
 * Ticks for at least ms milliseconds, for the bus timeouts. Two things make
 * a plain pdMS_TO_TICKS(ms) wait less than asked:
 *
 *   - pdMS_TO_TICKS() truncates. At IDF's default 100 Hz tick, 2 ms is 0
 *     ticks and 15 ms is 1, so round up instead.
 *   - A wait of n ticks counts tick interrupts, and the first one may be
 *     about to fire, so n ticks can be as little as n - 1 full periods.
 *     Add one.
 *
 * So 20 ms at 100 Hz is 3 ticks, 20-30 ms: long, never short.
 */
static TickType_t servo_ticks(uint32_t ms)
{
	return (TickType_t)(((uint64_t)ms * configTICK_RATE_HZ + 999) / 1000) +
	       1;
}

/* --------------------------------------------------------------- I2C -- */

int servo_i2c_probe(servo_i2c_bus handle, uint8_t address,
		    uint32_t timeout_ms)
{
	return servo_errno(i2c_master_probe(handle, address,
					    (int)timeout_ms));
}

static int servo_i2c_transfer(struct servo_bus *bus,
			      uint8_t cmd,
			      const uint8_t *tx, size_t tx_len, uint8_t *rx,
			      size_t rx_len)
{
	uint8_t out[1 + SERVO_BUS_MAX_LEN];

	if ((tx_len && rx_len) || tx_len > SERVO_BUS_MAX_LEN)
		return -EINVAL;
	out[0] = cmd;
	if (rx_len)
		return servo_errno(i2c_master_transmit_receive(
			bus->i2c, out, 1, rx, rx_len, (int)bus->timeout_ms));
	if (tx_len)
		memcpy(out + 1, tx, tx_len);
	return servo_errno(i2c_master_transmit(bus->i2c, out,
					       1 + tx_len,
					       (int)bus->timeout_ms));
}

static void servo_i2c_detach(struct servo_bus *bus)
{
	i2c_master_bus_rm_device(bus->i2c);
}

static const struct servo_bus_ops servo_i2c_ops = {
	.kind = SERVO_BUS_I2C,
	.transfer = servo_i2c_transfer,
	.detach = servo_i2c_detach,
};

int servo_i2c_attach(struct servo_bus *bus,
		     servo_i2c_bus handle,
		     uint8_t address, uint32_t scl_hz, uint32_t timeout_ms)
{
	i2c_device_config_t devcfg = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = address,
		.scl_speed_hz = scl_hz ? scl_hz : DEFAULT_SCL_HZ,
	};

	int ret;

	ret = servo_errno(i2c_master_bus_add_device(handle, &devcfg,
						    &bus->i2c));
	if (ret)
		return ret;
	/* Set only now: a bus with ops is one that has something to detach. */
	bus->ops = &servo_i2c_ops;
	bus->timeout_ms = timeout_ms;
	return 0;
}

/* --------------------------------------------------------------- SPI -- */

/*
 * One full-duplex transaction, so CS stays low across cmd, tx and rx: clock
 * out cmd and tx followed by zeros, and keep what came back after them. The
 * buffers live on the stack, word-aligned: IDF bounces non-DMA-capable or
 * unaligned buffers through a copy of its own.
 */
static int servo_spi_transfer(struct servo_bus *bus,
			      uint8_t cmd,
			      const uint8_t *tx, size_t tx_len, uint8_t *rx,
			      size_t rx_len)
{
	uint8_t out[1 + SERVO_BUS_MAX_LEN] __attribute__((aligned(4))) = {
		0
	};
	uint8_t in[1 + SERVO_BUS_MAX_LEN] __attribute__((aligned(4)));
	spi_transaction_t t = { 0 };
	int ret;

	if (tx_len + rx_len > SERVO_BUS_MAX_LEN)
		return -EINVAL;
	out[0] = cmd;
	if (tx_len)
		memcpy(out + 1, tx, tx_len);

	t.length = (1 + tx_len + rx_len) * 8;
	t.tx_buffer = out;
	t.rx_buffer = in;
	ret = servo_errno(spi_device_polling_transmit(bus->spi, &t));
	if (!ret && rx_len)
		memcpy(rx, in + 1 + tx_len, rx_len);
	return ret;
}

/*
 * One frame, full duplex: rx[i] is what came back while tx[i] went out.
 * Polled, not queued: for a few bytes the interrupt and task switch of a
 * queued transfer cost more than the transfer itself. A frame of up to four
 * bytes travels in the transaction itself (tx_data, rx_data); a longer one,
 * through word-aligned buffers on the stack.
 */
static int servo_spi_exchange(struct servo_bus *bus,
			      const uint8_t *tx,
			      uint8_t *rx, size_t len)
{
	uint8_t out[1 + SERVO_BUS_MAX_LEN] __attribute__((aligned(4)));
	uint8_t in[1 + SERVO_BUS_MAX_LEN] __attribute__((aligned(4)));
	spi_transaction_t t = { 0 };
	bool inline_data = len <= sizeof(t.tx_data);
	int ret;

	if (!len || len > sizeof(out))
		return -EINVAL;

	t.length = len * 8;
	if (inline_data) {
		t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
		memcpy(t.tx_data, tx, len);
	} else {
		memcpy(out, tx, len);
		t.tx_buffer = out;
		t.rx_buffer = in;
	}
	ret = servo_errno(spi_device_polling_transmit(bus->spi, &t));
	if (!ret)
		memcpy(rx, inline_data ? t.rx_data : in, len);
	return ret;
}

static void servo_spi_detach(struct servo_bus *bus)
{
	spi_bus_remove_device(bus->spi);
}

static const struct servo_bus_ops servo_spi_ops = {
	.kind = SERVO_BUS_SPI,
	.transfer = servo_spi_transfer,
	.exchange = servo_spi_exchange,
	.detach = servo_spi_detach,
};

int servo_spi_attach(struct servo_bus *bus,
		     spi_host_device_t host,
		     int cs_pin, uint32_t clock_hz, uint8_t mode)
{
	spi_device_interface_config_t devcfg = {
		.mode = mode,
		.clock_speed_hz = (int)clock_hz,
		.spics_io_num = cs_pin,
		.queue_size = 1,
	};

	int ret;

	ret = servo_errno(spi_bus_add_device(host, &devcfg,
					     &bus->spi));
	if (ret)
		return ret;
	bus->ops = &servo_spi_ops;
	bus->timeout_ms = 0;	/* spi_master transfers take none */
	return 0;
}

/* -------------------------------------------------------------- UART -- */

/*
 * Drop whatever is waiting -- a late reply to an earlier command would read
 * as the start of this one -- then send, then wait for the whole reply.
 * uart_read_bytes() returns early only with everything; a short count means
 * the timeout ran out.
 */
static int servo_uart_transfer(struct servo_bus *bus,
			       uint8_t cmd,
			       const uint8_t *tx, size_t tx_len,
			       uint8_t *rx, size_t rx_len)
{
	uint8_t out[1 + SERVO_BUS_MAX_LEN];
	int n, ret;

	if (tx_len > SERVO_BUS_MAX_LEN || rx_len > INT32_MAX)
		return -EINVAL;
	out[0] = cmd;
	if (tx_len)
		memcpy(out + 1, tx, tx_len);

	ret = servo_errno(uart_flush_input(bus->uart));
	if (ret)
		return ret;
	n = uart_write_bytes(bus->uart, out, 1 + tx_len);
	if (n != (int)(1 + tx_len))
		return -EIO;
	if (!rx_len)
		return 0;
	n = uart_read_bytes(bus->uart, rx, (uint32_t)rx_len,
			    servo_ticks(bus->timeout_ms));
	if (n < 0)
		return -EIO;
	return (size_t)n < rx_len ? -ETIMEDOUT : 0;
}

/*
 * The UART driver keeps everything the line delivers in its RX ring buffer,
 * so the echo of a whole datagram waits there however fast it came: flush,
 * send, wait for the last bit to leave, then read back as many bytes as
 * went out.
 */
static int servo_uart_exchange(struct servo_bus *bus,
			       const uint8_t *tx,
			       uint8_t *rx, size_t len)
{
	int n, ret;

	if (!len || len > 1 + SERVO_BUS_MAX_LEN)
		return -EINVAL;

	ret = servo_errno(uart_flush_input(bus->uart));
	if (ret)
		return ret;
	n = uart_write_bytes(bus->uart, tx, len);
	if (n != (int)len)
		return -EIO;
	ret = servo_errno(uart_wait_tx_done(bus->uart,
					    servo_ticks(bus->timeout_ms)));
	if (ret)
		return ret;
	n = uart_read_bytes(bus->uart, rx, (uint32_t)len,
			    servo_ticks(bus->timeout_ms));
	if (n < 0)
		return -EIO;
	return (size_t)n < len ? -ETIMEDOUT : 0;
}

static int servo_uart_receive(struct servo_bus *bus,
			      uint8_t *rx,
			      size_t rx_len)
{
	int n;

	if (rx_len > INT32_MAX)
		return -EINVAL;
	n = uart_read_bytes(bus->uart, rx, (uint32_t)rx_len,
			    servo_ticks(bus->timeout_ms));
	if (n < 0)
		return -EIO;
	return (size_t)n < rx_len ? -ETIMEDOUT : 0;
}

/* The driver is the app's: nothing was added, nothing to remove. */
static void servo_uart_detach(struct servo_bus *bus)
{
	(void)bus;
}

static const struct servo_bus_ops servo_uart_ops = {
	.kind = SERVO_BUS_UART,
	.transfer = servo_uart_transfer,
	.exchange = servo_uart_exchange,
	.receive = servo_uart_receive,
	.detach = servo_uart_detach,
};

int servo_uart_attach(struct servo_bus *bus, uart_port_t port,
		      uint32_t timeout_ms)
{
	if (!uart_is_driver_installed(port))
		return -EINVAL;
	bus->ops = &servo_uart_ops;
	bus->timeout_ms = timeout_ms;
	bus->uart = port;
	return 0;
}

/* ------------------------------------------------------------ output -- */

#define SERVO_OUTPUT_MODE	LEDC_LOW_SPEED_MODE
#define SERVO_OUTPUT_DUTY_FULL	(1u << SERVO_OUTPUT_DUTY_BITS)

/*
 * pulse_us as LEDC duty: the share of one period, rounded to the nearest of
 * 2^14 steps. At 50 Hz one step is 1.22 us. A pulse as long as the period
 * or longer leaves no low time: -EINVAL.
 */
static int servo_output_duty(const struct servo_output *output,
			     uint32_t pulse_us, uint32_t *duty)
{
	uint64_t scaled = (uint64_t)pulse_us * output->rate_hz;

	if (scaled >= 1000000u)
		return -EINVAL;
	*duty = (uint32_t)((scaled * SERVO_OUTPUT_DUTY_FULL + 500000u) /
			   1000000u);
	return 0;
}

int servo_output_check(const struct servo_output *output, uint32_t pulse_us)
{
	uint32_t duty;

	return servo_output_duty(output, pulse_us, &duty);
}

/*
 * What the timer runs at now, or 0 for nothing. ledc_get_freq() returns 0
 * for a timer never configured, and before the LEDC has been used at all
 * its own error code as the frequency: ESP_ERR_INVALID_STATE, with an
 * "LEDC is not initialized" error line (ledc.c:1116, IDF v5.5.5). Both
 * mean nothing runs on that timer. ESP_ERR_INVALID_ARG, for a timer number
 * out of range, is left to ledc_timer_config() to refuse. A timer really
 * running at 258 or 259 Hz reads as nothing too, and is configured over:
 * those two rates are the price of the cast.
 */
static uint32_t servo_output_timer_hz(ledc_timer_t timer)
{
	uint32_t hz = ledc_get_freq(SERVO_OUTPUT_MODE, timer);

	if (hz == (uint32_t)ESP_ERR_INVALID_STATE ||
	    hz == (uint32_t)ESP_ERR_INVALID_ARG)
		return 0;
	return hz;
}

int servo_output_attach(struct servo_output *output, int gpio,
			ledc_timer_t timer, ledc_channel_t channel,
			uint32_t rate_hz)
{
	ledc_timer_config_t timer_cfg = {
		.speed_mode = SERVO_OUTPUT_MODE,
		/* SERVO_OUTPUT_DUTY_BITS */
		.duty_resolution = LEDC_TIMER_14_BIT,
		.timer_num = timer,
		.freq_hz = rate_hz,
		.clk_cfg = LEDC_AUTO_CLK,
	};
	uint32_t running_hz;
	int ret;

	if (!rate_hz || gpio < 0 || (unsigned)channel >= LEDC_CHANNEL_MAX)
		return -EINVAL;
	/*
	 * A timer already at rate_hz is shared as it is: configuring it again
	 * would reset its counter under every channel on it. ledc_get_freq()
	 * reads the rate back from the divider, rounded to the hertz. For a
	 * timer set by ledc_timer_config() at 14 bits on the 80 MHz APB clock,
	 * which LEDC_AUTO_CLK tries first on esp32, -s3 and -c3, that is
	 * exactly the rate asked for at every rate from 1 to 1139 Hz; other
	 * targets are unverified. See docs/chips.md.
	 */
	running_hz = servo_output_timer_hz(timer);
	if (running_hz && running_hz != rate_hz)
		return -EBUSY;
	if (!running_hz) {
		ret = servo_errno(ledc_timer_config(&timer_cfg));
		if (ret)
			return ret;
	}
	output->gpio = gpio;
	output->timer = timer;
	output->channel = channel;
	output->rate_hz = rate_hz;
	return 0;
}

int servo_output_start(struct servo_output *output, uint32_t pulse_us)
{
	ledc_channel_config_t channel_cfg = {
		.gpio_num = output->gpio,
		.speed_mode = SERVO_OUTPUT_MODE,
		.channel = output->channel,
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = output->timer,
	};
	uint32_t duty;
	int ret;

	ret = servo_output_duty(output, pulse_us, &duty);
	if (ret)
		return ret;
	channel_cfg.duty = duty;
	return servo_errno(ledc_channel_config(&channel_cfg));
}

int servo_output_write_us(struct servo_output *output, uint32_t pulse_us)
{
	uint32_t duty;
	int ret;

	ret = servo_output_duty(output, pulse_us, &duty);
	if (ret)
		return ret;
	ret = servo_errno(ledc_set_duty(SERVO_OUTPUT_MODE, output->channel,
					duty));
	if (ret)
		return ret;
	return servo_errno(ledc_update_duty(SERVO_OUTPUT_MODE,
					    output->channel));
}

void servo_output_stop(struct servo_output *output)
{
	ledc_stop(SERVO_OUTPUT_MODE, output->channel, 0);
}

/* ------------------------------------------------------------- delay -- */

void servo_delay_us(uint32_t us)
{
	esp_rom_delay_us(us);
}

#endif /* ESP_PLATFORM */
