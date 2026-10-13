/*
 * One servo on an LEDC channel: centre it, sweep end to end in small steps,
 * then stop the pulses.
 *
 * Give the servo its own supply and common the grounds; see
 * docs/hardware.md. RATE_HZ, MIN_US, MAX_US and RANGE_DEG are placeholders,
 * not any servo's figures: measure yours (docs/usage.md, "Calibrating") and
 * replace them before the first run.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "servo.h"
#include "servo_pwm.h"

#define PIN_SERVO   18
#define RATE_HZ     50          /* placeholder: your servo's frame rate */
#define MIN_US      1250        /* placeholder: measured pulse at 0 deg */
#define MAX_US      1750        /* placeholder: measured pulse at RANGE_DEG */
#define RANGE_DEG   60.0f       /* placeholder: measured travel */

static const char *TAG = "example";

void app_main(void)
{
    const struct servo_pwm_config cfg = {
        .gpio = PIN_SERVO,
        .timer = LEDC_TIMER_0,
        .channel = LEDC_CHANNEL_0,
        .rate_hz = RATE_HZ,
    };
    const struct servo_calibration calibration = {
        .min_us = MIN_US,
        .max_us = MAX_US,
        .range_deg = RANGE_DEG,
    };
    static struct servo_pwm pwm;
    struct servo_dev *servo;
    int err;

    /* Nothing goes out until the first move: the servo stays parked. */
    err = servo_pwm_init(&pwm, &cfg);
    if (err) {
        ESP_LOGE(TAG, "servo_pwm_init: %d", err);
        return;
    }
    servo = servo_pwm_servo(&pwm);
    err = servo_set_calibration(servo, &calibration);
    if (err) {
        ESP_LOGE(TAG, "servo_set_calibration: %d", err);
        return;
    }

    servo_set_angle(servo, RANGE_DEG / 2.0f);
    vTaskDelay(pdMS_TO_TICKS(1000));
    for (float angle = 0.0f; angle <= RANGE_DEG; angle += 2.0f) {
        servo_set_angle(servo, angle);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    for (float angle = RANGE_DEG; angle >= 0.0f; angle -= 2.0f) {
        servo_set_angle(servo, angle);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGI(TAG, "sweep done; stopping the pulses");
    servo_pwm_del(&pwm);
}
