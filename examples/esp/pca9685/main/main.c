/*
 * A PCA9685: four servos on channels 0-3 through servo.h, and an LED on
 * channel 15 as a plain PWM output, through pca9685.h.
 *
 * Give the servos' V+ rail its own supply and common the grounds; see
 * docs/hardware.md. MIN_US, MAX_US and RANGE_DEG are placeholders, not any
 * servo's figures: measure yours (docs/usage.md, "Calibrating") and replace
 * them before the first run.
 */
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pca9685.h"
#include "servo.h"

#define PIN_SDA     21
#define PIN_SCL     22
#define SERVOS      4
#define LED_CHANNEL 15
#define MIN_US      1250        /* placeholder: measured pulse at 0 deg */
#define MAX_US      1750        /* placeholder: measured pulse at RANGE_DEG */
#define RANGE_DEG   60.0f       /* placeholder: measured travel */

static const char *TAG = "example";

void app_main(void)
{
    i2c_master_bus_config_t buscfg = {
        .i2c_port = -1,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    const pca9685_config_t cfg = {
        .frequency_hz = 50.0f,  /* placeholder: your servos' frame rate */
        .totem_pole = true,
    };
    const struct servo_calibration calibration = {
        .min_us = MIN_US,
        .max_us = MAX_US,
        .range_deg = RANGE_DEG,
    };
    i2c_master_bus_handle_t bus;
    pca9685_handle_t chip;
    struct servo_dev *servo[SERVOS];

    ESP_ERROR_CHECK(i2c_new_master_bus(&buscfg, &bus));
    ESP_ERROR_CHECK(pca9685_init(bus, &cfg, &chip));
    ESP_LOGI(TAG, "running at %.2f Hz", pca9685_frequency(chip));

    for (int ch = 0; ch < SERVOS; ch++) {
        struct servo_calibration own = calibration;

        own.invert = (ch == 1);     /* the second servo is mounted mirrored */
        servo[ch] = pca9685_servo(chip, ch);
        if (servo_set_calibration(servo[ch], &own))
            ESP_LOGE(TAG, "channel %d: calibration refused", ch);
    }

    for (int round = 0; ; round++) {
        float angle = (round % 2) ? RANGE_DEG : 0.0f;

        for (int ch = 0; ch < SERVOS; ch++)
            servo_set_angle(servo[ch], angle);
        /* The LED, as any PWM output: half brightness, then off. */
        pca9685_set_duty(chip, LED_CHANNEL, (round % 2) ? 0.5f : 0.0f);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
