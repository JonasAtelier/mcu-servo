/*
 * Kernel dev_err()-style logging for the servo backends and their users.
 * The first argument is the line's prefix: a servo_dev's name, or a chip's.
 *
 * ESP-IDF: everything goes out under the one ESP-IDF tag "servo",
 * prefixed with that name, so one esp_log_level_set("servo", ...)
 * controls every backend and a line still says which one it came from.
 * There is deliberately no level machinery of our own; servo_dbg()
 * is also subject to CONFIG_LOG_MAXIMUM_LEVEL, and CONFIG_SERVO_DEBUG
 * is not used.
 *
 * STM32 HAL: every line goes out through printf() as
 * "servo: <name>: ...". Where printf() ends up is the app's
 * business: with newlib, retarget _write() to a UART. servo_dbg()
 * alone is compiled out unless CONFIG_SERVO_DEBUG is 1 (stm-forge:
 * SERVO_DEBUG = y).
 */
#ifndef SERVO_LOG_H
#define SERVO_LOG_H

#ifdef ESP_PLATFORM
#include "esp_log.h"
#else
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ESP_PLATFORM
#define SERVO_LOG_TAG	"servo"

#define servo_err(name, fmt, ...) \
	ESP_LOGE(SERVO_LOG_TAG, "%s: " fmt, (name), ##__VA_ARGS__)
#define servo_warn(name, fmt, ...) \
	ESP_LOGW(SERVO_LOG_TAG, "%s: " fmt, (name), ##__VA_ARGS__)
#define servo_info(name, fmt, ...) \
	ESP_LOGI(SERVO_LOG_TAG, "%s: " fmt, (name), ##__VA_ARGS__)
#define servo_dbg(name, fmt, ...) \
	ESP_LOGD(SERVO_LOG_TAG, "%s: " fmt, (name), ##__VA_ARGS__)

#else /* STM32 HAL */
#define servo_err(name, fmt, ...) \
	printf("servo: %s: " fmt "\n", (name), ##__VA_ARGS__)
#define servo_warn(name, fmt, ...) \
	printf("servo: %s: " fmt "\n", (name), ##__VA_ARGS__)
#define servo_info(name, fmt, ...) \
	printf("servo: %s: " fmt "\n", (name), ##__VA_ARGS__)

#if CONFIG_SERVO_DEBUG
#define servo_dbg(name, fmt, ...) \
	printf("servo: %s: " fmt "\n", (name), ##__VA_ARGS__)
#else
/* Still type-checked, so a debug line cannot rot while it is off. */
#define servo_dbg(name, fmt, ...) do { \
	if (0) \
		printf("servo: %s: " fmt "\n", (name), ##__VA_ARGS__); \
} while (0)
#endif
#endif /* ESP_PLATFORM */

#ifdef __cplusplus
}
#endif

#endif /* SERVO_LOG_H */
