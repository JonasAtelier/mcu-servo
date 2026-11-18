/*
 * Shared by the backends inside this library. Not public API.
 *
 * Two layers sit under the backends, the way regmap sits under a kernel
 * driver:
 *
 *   - The bus kind is chosen at run time. A driver holds a struct servo_bus
 *     and reaches it only through bus->ops, so it can sit on I2C, SPI or a
 *     UART. Every category library offers all three kinds, used or not; the
 *     PCA9685 is on I2C. The MCU's own PWM is outside the bus layer: it is
 *     the output port at the end of this file.
 *   - The platform is chosen at link time. servo_port_esp.c and
 *     servo_port_stm.c each define the ops tables, the attach functions and
 *     the output port below for their platform; the other one compiles to
 *     nothing.
 *
 * ESP_PLATFORM, which ESP-IDF defines for every component, and the HAL's own
 * module switches are defined or not, never 0 or 1, so they are the one place
 * #ifdef is right; this library's CONFIG_SERVO_* options are always tested
 * with #if.
 */
#ifndef SERVO_PRIV_H
#define SERVO_PRIV_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The most bytes after cmd: tx on I2C and UART, tx and rx together on SPI. */
#define SERVO_BUS_MAX_LEN	31

struct servo_bus;

enum servo_bus_kind {
	SERVO_BUS_I2C,
	SERVO_BUS_SPI,
	SERVO_BUS_UART,
};

struct servo_bus_ops {
	enum servo_bus_kind kind;
	/*
	 * One transaction: cmd, then tx_len bytes of tx, then rx_len bytes
	 * into rx. cmd is the register address, with any SPI read bit already
	 * set by the chip, and the first byte of a datagram on a UART.
	 *
	 *   I2C:  rx_len ? cmd, repeated start, read : cmd and tx, one write;
	 *         a bare cmd is a one-byte write. Sending tx and then reading
	 *         is -EINVAL.
	 *   SPI:  CS low, cmd, tx, rx, CS high -- high again even when the
	 *         transfer failed.
	 *   UART: drop stale input, send cmd and tx, then wait for rx_len
	 *         bytes; fewer within the timeout is -ETIMEDOUT. For a line
	 *         where the MCU does not hear itself; see exchange().
	 *
	 * On SPI more than SERVO_BUS_MAX_LEN bytes after cmd is -EINVAL; on
	 * I2C and UART only tx is bounded so, and rx on the HAL by its 16-bit
	 * count. 0 or a negative errno.
	 */
	int (*transfer)(struct servo_bus *bus, uint8_t cmd,
			const uint8_t *tx, size_t tx_len, uint8_t *rx,
			size_t rx_len);
	/*
	 * len bytes of tx out while len bytes come back into rx, rx[i] the
	 * one that answered tx[i]. More than 1 + SERVO_BUS_MAX_LEN bytes, or
	 * none, is -EINVAL. NULL on I2C.
	 *
	 *   SPI:  one frame, full duplex: CS low, the bytes at the same clocks,
	 *         CS high -- high again even when the transfer failed. For a
	 *         part whose answer to one frame comes back in the next.
	 *   UART: drop stale input, send tx, and collect what the line gave
	 *         back meanwhile. On a single wire the MCU hears its own
	 *         transmission; this is how a chip takes that echo off the
	 *         line, so that the answer which follows starts clean. Fewer
	 *         than len bytes back within the timeout is -ETIMEDOUT.
	 */
	int (*exchange)(struct servo_bus *bus, const uint8_t *tx,
			uint8_t *rx, size_t len);
	/*
	 * rx_len bytes, with nothing sent and nothing dropped first: the
	 * answer that follows an exchange(). Fewer within the timeout is
	 * -ETIMEDOUT; rx is then undefined. NULL on I2C and SPI.
	 */
	int (*receive)(struct servo_bus *bus, uint8_t *rx,
		       size_t rx_len);
	/* Undo the attach. The bus itself is the app's and is left alone. */
	void (*detach)(struct servo_bus *bus);
};

#ifdef ESP_PLATFORM
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_err.h"

/* The I2C argument of every I2C attach on this platform. */
typedef i2c_master_bus_handle_t servo_i2c_bus;

/*
 * One device on one bus, as its driver holds it. SERVO_HAVE_BUS
 * says the platform has at least one bus kind, so the struct exists.
 */
#define SERVO_HAVE_BUS
struct servo_bus {
	const struct servo_bus_ops *ops;
	uint32_t timeout_ms;
	union {
		i2c_master_dev_handle_t i2c;
		spi_device_handle_t spi;
		uart_port_t uart;
	};
};

/*
 * esp_err_t -> negative errno, at the bus boundary and nowhere else.
 * ESP_ERR_NOT_FOUND is -ENXIO (nothing at that address), not -ENODEV
 * (something answered but it is the wrong chip). Every code listed here
 * comes back as itself through servo_esp_err().
 */
static inline int servo_errno(esp_err_t err)
{
	switch (err) {
	case ESP_OK:
		return 0;
	case ESP_ERR_INVALID_ARG:
		return -EINVAL;
	case ESP_ERR_TIMEOUT:
		return -ETIMEDOUT;
	case ESP_ERR_NOT_FOUND:
		return -ENXIO;
	case ESP_ERR_INVALID_SIZE:
		return -EMSGSIZE;
	case ESP_ERR_NO_MEM:
		return -ENOMEM;
	case ESP_ERR_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case ESP_ERR_INVALID_STATE:
		return -EBUSY;
	default:
		return -EIO;
	}
}

/*
 * negative errno -> esp_err_t, for pca9685.h, which returns esp_err_t on
 * ESP-IDF as esp-pca9685-pwm did. The inverse of servo_errno() for every
 * code it lists, so an IDF error that reached the bus comes back to the app
 * as it was; -ENODEV, a chip that answered but is not a PCA9685, is
 * ESP_ERR_NOT_FOUND as before. Only codes servo_errno() folds into -EIO
 * come back as ESP_FAIL.
 */
static inline esp_err_t servo_esp_err(int err)
{
	switch (err) {
	case 0:
		return ESP_OK;
	case -EINVAL:
		return ESP_ERR_INVALID_ARG;
	case -ETIMEDOUT:
		return ESP_ERR_TIMEOUT;
	case -ENXIO:
	case -ENODEV:
		return ESP_ERR_NOT_FOUND;
	case -EMSGSIZE:
		return ESP_ERR_INVALID_SIZE;
	case -ENOMEM:
		return ESP_ERR_NO_MEM;
	case -EOPNOTSUPP:
		return ESP_ERR_NOT_SUPPORTED;
	case -EBUSY:
		return ESP_ERR_INVALID_STATE;
	default:
		return ESP_FAIL;
	}
}

#else /* STM32 HAL */
/*
 * Undefined, the include below fails with only
 * `#include expects "FILENAME" or <FILENAME>`. Every library source that
 * needs the HAL includes this header before any backend header, so one check
 * here names the cause on every build route. The HAL module checks sit in
 * its #else: without the family header no module looks enabled, and each of
 * them would blame a module instead.
 */
#ifndef CMSIS_device_header
#error "servo: CMSIS_device_header is not defined; define it as the STM32 family header, \"stm32f4xx.h\" (CMake: set(MCU_DEVICE_HEADER stm32f4xx.h))"
#else
#include CMSIS_device_header

#if !defined(HAL_TIM_MODULE_ENABLED) && CONFIG_SERVO_PWM
#error "the PWM backend needs the HAL TIM module: #define HAL_TIM_MODULE_ENABLED in the app's stm32<family>xx_hal_conf.h, with stm32<family>xx_hal_tim.c in the build"
#endif
#if !defined(HAL_I2C_MODULE_ENABLED) && CONFIG_SERVO_PCA9685
#error "the PCA9685 driver needs the HAL I2C module: #define HAL_I2C_MODULE_ENABLED in the app's stm32<family>xx_hal_conf.h, with stm32<family>xx_hal_i2c.c in the build"
#endif
#endif /* CMSIS_device_header */

#ifdef HAL_I2C_MODULE_ENABLED
typedef I2C_HandleTypeDef *servo_i2c_bus;
#endif

#if defined(HAL_I2C_MODULE_ENABLED) || defined(HAL_SPI_MODULE_ENABLED) || \
    defined(HAL_UART_MODULE_ENABLED)
#define SERVO_HAVE_BUS
struct servo_bus {
	const struct servo_bus_ops *ops;
	uint32_t timeout_ms;
	union {
#ifdef HAL_I2C_MODULE_ENABLED
		struct {
			I2C_HandleTypeDef *handle;
			uint8_t address;	/* 7-bit */
		} i2c;
#endif
#ifdef HAL_SPI_MODULE_ENABLED
		struct {
			SPI_HandleTypeDef *handle;
			GPIO_TypeDef *cs_port;	/* app's output, idle high */
			uint16_t cs_pin;
		} spi;
#endif
#ifdef HAL_UART_MODULE_ENABLED
		UART_HandleTypeDef *uart;
#endif
	};
};
#endif

/*
 * HAL_StatusTypeDef -> negative errno, at the bus boundary and nowhere else.
 * HAL_BUSY -- the bus or the handle in use, or a handle never initialised --
 * is -EAGAIN. A NACK in the middle of a transfer is HAL_ERROR like any other
 * fault, so -EIO.
 */
static inline int servo_hal_errno(HAL_StatusTypeDef status)
{
	switch (status) {
	case HAL_OK:
		return 0;
	case HAL_TIMEOUT:
		return -ETIMEDOUT;
	case HAL_BUSY:
		return -EAGAIN;
	default:
		return -EIO;
	}
}

#endif /* STM32 HAL */

#ifdef SERVO_HAVE_BUS
/* bus->ops->exchange(), or -EOPNOTSUPP on a kind that has none. */
static inline int servo_bus_exchange(struct servo_bus *bus,
				     const uint8_t *tx, uint8_t *rx,
				     size_t len)
{
	if (!bus->ops->exchange)
		return -EOPNOTSUPP;
	return bus->ops->exchange(bus, tx, rx, len);
}

/* bus->ops->receive(), or -EOPNOTSUPP on a kind that has none. */
static inline int servo_bus_receive(struct servo_bus *bus,
				    uint8_t *rx,
				    size_t rx_len)
{
	if (!bus->ops->receive)
		return -EOPNOTSUPP;
	return bus->ops->receive(bus, rx, rx_len);
}

static inline void servo_bus_detach(struct servo_bus *bus)
{
	bus->ops->detach(bus);
}
#endif

#if defined(ESP_PLATFORM) || defined(HAL_I2C_MODULE_ENABLED)
/*
 * Is anything at this 7-bit address? -ENXIO when nothing ACKs -- nothing at
 * that address -- not -ENODEV, which means something answered but it is the
 * wrong chip. A NACK in the middle of a transfer is a plain -EIO on both
 * platforms, which is why a driver decides presence here, never by its
 * first transfer.
 */
int servo_i2c_probe(servo_i2c_bus handle, uint8_t address,
		    uint32_t timeout_ms);

/*
 * Bind bus to the I2C device at address. On ESP-IDF the driver adds the
 * device at scl_hz, 0 for 400 kHz; on the HAL the clock is the handle's and
 * scl_hz is ignored. Undo with servo_bus_detach(). Every attach leaves
 * bus->ops NULL when it fails, so a bus with ops is one with something to
 * detach.
 */
int servo_i2c_attach(struct servo_bus *bus,
		     servo_i2c_bus handle,
		     uint8_t address, uint32_t scl_hz, uint32_t timeout_ms);
#endif

#ifdef ESP_PLATFORM
/*
 * Add the SPI device on host's cs_pin, in SPI mode 0-3 at clock_hz. The
 * host is the app's, already spi_bus_initialize()d.
 */
int servo_spi_attach(struct servo_bus *bus,
		     spi_host_device_t host,
		     int cs_pin, uint32_t clock_hz, uint8_t mode);

/* port's driver is the app's, already installed and configured. */
int servo_uart_attach(struct servo_bus *bus, uart_port_t port,
		      uint32_t timeout_ms);
#else
#ifdef HAL_SPI_MODULE_ENABLED
/*
 * Bind bus to the SPI device behind cs_port/cs_pin, and deselect it, so the
 * first transfer starts on a falling edge. Mode, clock and frame are the
 * handle's; the chip select is an output the app has set up.
 */
int servo_spi_attach(struct servo_bus *bus,
		     SPI_HandleTypeDef *handle,
		     GPIO_TypeDef *cs_port, uint16_t cs_pin,
		     uint32_t timeout_ms);
#endif
#ifdef HAL_UART_MODULE_ENABLED
/* handle is the app's, already HAL_UART_Init()ed. */
int servo_uart_attach(struct servo_bus *bus,
		      UART_HandleTypeDef *handle,
		      uint32_t timeout_ms);
#endif
#endif

/*
 * The output port: one pin that repeats a pulse of pulse_us at rate_hz, for
 * the PWM backend. Outside the bus layer: no bytes cross it. mcu-esc's PWM
 * port, plus the shared-timer check below.
 *
 *   ESP-IDF: an LEDC channel in low-speed mode, which every ESP32 has, on a
 *            timer at rate_hz with 14-bit duty, the widest the ESP32-S2,
 *            -S3 and -C3 take. ledc_timer_config() resets the timer's
 *            counter (ledc.c:804, IDF v5.5.5), which would cut short the
 *            pulse in flight on every channel already on it. So the attach
 *            asks ledc_get_freq() first: a timer already at rate_hz is
 *            shared as it is, one at another rate is -EBUSY, and only a
 *            timer that runs at nothing is configured.
 *   HAL:     a TIM channel the app has set up in PWM mode. The rate is the
 *            app's: tick_hz, the counter clock after the prescaler, over
 *            ARR + 1. The attach reads it back into rate_hz.
 *
 * servo_output_attach() binds without driving the pin; servo_output_start()
 * then puts out pulse_us; servo_output_write_us() changes it, from the next
 * period on; servo_output_stop() holds the pin low, no pulses at all. A
 * pulse that does not fit inside one period is -EINVAL.
 */
#if defined(ESP_PLATFORM) || defined(HAL_TIM_MODULE_ENABLED)
#define SERVO_HAVE_OUTPUT
#include "servo_pwm.h"		/* struct servo_output, in struct servo_pwm */

#ifdef ESP_PLATFORM
#define SERVO_OUTPUT_DUTY_BITS	14

int servo_output_attach(struct servo_output *output, int gpio,
			ledc_timer_t timer, ledc_channel_t channel,
			uint32_t rate_hz);
#else
int servo_output_attach(struct servo_output *output, TIM_HandleTypeDef *tim,
			uint32_t channel, uint32_t tick_hz);
#endif
#endif

#ifdef SERVO_HAVE_OUTPUT
/* 0 when pulse_us fits in one period, else -EINVAL; touches nothing. */
int servo_output_check(const struct servo_output *output, uint32_t pulse_us);
int servo_output_start(struct servo_output *output, uint32_t pulse_us);
int servo_output_write_us(struct servo_output *output, uint32_t pulse_us);
void servo_output_stop(struct servo_output *output);
#endif

/*
 * Busy-wait at least us microseconds, for the PCA9685's oscillator. ESP-IDF:
 * esp_rom_delay_us(). HAL: HAL_Delay() in whole milliseconds, rounded up.
 */
void servo_delay_us(uint32_t us);

/*
 * The handle APIs in pca9685.h return esp_err_t on ESP-IDF, as
 * esp-pca9685-pwm did, and 0 or a negative errno on the HAL. The driver
 * works in errno and converts once, where each public call returns.
 */
#ifdef ESP_PLATFORM
typedef esp_err_t servo_handle_ret;
#define SERVO_HANDLE_RET(err)	servo_esp_err(err)
#else
typedef int servo_handle_ret;
#define SERVO_HANDLE_RET(err)	(err)
#endif

#endif /* SERVO_PRIV_H */
