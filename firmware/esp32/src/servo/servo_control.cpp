// ============================================================
// servo_control.cpp - MG90S 舵机最小控制实现
// ============================================================
//
// PWM:
//   - 50Hz, 13-bit resolution
//   - 20ms period -> 0x1FFF maps to 20ms
//   - conservative range: 60deg .. 120deg
// ============================================================

#include "servo_control.h"

#include <Arduino.h>
#include <driver/ledc.h>

namespace {

#define SERVO_MIN_ANGLE         60
#define SERVO_MAX_ANGLE         120

#define SERVO_MIN_PULSE_MS      0.6f
#define SERVO_MAX_PULSE_MS      1.4f
#define SERVO_PERIOD_MS         20.0f
#define SERVO_ANGLE_RANGE       (SERVO_MAX_ANGLE - SERVO_MIN_ANGLE)
#define SERVO_PWM_FULL_SCALE    ((1UL << SERVO_PWM_RES_BITS) - 1UL)

bool s_started = false;

uint32_t angleToDuty(int angle)
{
    int clamped = angle;

    if (clamped < SERVO_MIN_ANGLE)
    {
        clamped = SERVO_MIN_ANGLE;
    }

    if (clamped > SERVO_MAX_ANGLE)
    {
        clamped = SERVO_MAX_ANGLE;
    }

    float relative = static_cast<float>(clamped - SERVO_MIN_ANGLE) /
                     static_cast<float>(SERVO_ANGLE_RANGE);
    float pulseMs = SERVO_MIN_PULSE_MS +
                    (SERVO_MAX_PULSE_MS - SERVO_MIN_PULSE_MS) * relative;
    float dutyRatio = pulseMs / SERVO_PERIOD_MS;
    uint32_t duty = static_cast<uint32_t>(dutyRatio * static_cast<float>(SERVO_PWM_FULL_SCALE));

    if (duty > SERVO_PWM_FULL_SCALE)
    {
        duty = SERVO_PWM_FULL_SCALE;
    }

    return duty;
}

}  // namespace

void servo_init(void)
{
    if (s_started)
    {
        return;
    }

    ledcSetup(SERVO_PWM_CHANNEL, SERVO_PWM_FREQ_HZ, SERVO_PWM_RES_BITS);
    ledcAttachPin(SERVO_SIGNAL_GPIO, SERVO_PWM_CHANNEL);
    ledcWrite(SERVO_PWM_CHANNEL, 0);

    s_started = true;

    Serial.printf("[servo] init GPIO=%d\n", SERVO_SIGNAL_GPIO);
    Serial.printf("[servo] frequency=%dHz\n", SERVO_PWM_FREQ_HZ);

    servo_center();
}

void servo_set_angle(int angle)
{
    if (!s_started)
    {
        return;
    }

    uint32_t duty = angleToDuty(angle);
    ledcWrite(SERVO_PWM_CHANNEL, duty);

    Serial.printf("[servo] angle=%d duty=%lu\n", angle, duty);
}

void servo_center(void)
{
    servo_set_angle(SERVO_CENTER_ANGLE);
}

void servo_left(void)
{
    servo_set_angle(SERVO_LEFT_ANGLE);
}

void servo_right(void)
{
    servo_set_angle(SERVO_RIGHT_ANGLE);
}

void servo_run_test_sequence(void)
{
    servo_init();

    Serial.println("[servo-test] CENTER");
    servo_center();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] LEFT");
    servo_left();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] CENTER");
    servo_center();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] RIGHT");
    servo_right();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] CENTER");
    servo_center();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] done");
}
