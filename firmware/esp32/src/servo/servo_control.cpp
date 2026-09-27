// ============================================================
// servo_control.cpp - MG90S 双舵机控制实现
// ============================================================
//
// PWM:
//   - 50 Hz, 13-bit resolution
//   - 20 ms period -> 0x1FFF maps to 20 ms
//   - conservative range: 60° .. 120°
//   - pulse range: 0.6 ms (60°) .. 1.4 ms (120°)
//
// 通道：
//   - Vertical  (Tilt, 上下) : GPIO 5, LEDC channel 0
//   - Horizontal(Pan, 左右) : GPIO 4, LEDC channel 1
//
// 状态保存：
//   - s_verticalAngle / s_horizontalAngle 由 ESP32 独占保存
//   - PC 端无状态，仅发送动作命令
// ============================================================

#include "servo_control.h"

#include <Arduino.h>
#include <driver/ledc.h>

namespace {

#define SERVO_MIN_PULSE_MS      0.6f
#define SERVO_MAX_PULSE_MS      1.4f
#define SERVO_PERIOD_MS         20.0f
#define SERVO_ANGLE_RANGE       (SERVO_MAX_ANGLE - SERVO_MIN_ANGLE)
#define SERVO_PWM_FULL_SCALE    ((1UL << SERVO_PWM_RES_BITS) - 1UL)

bool s_started = false;
int  s_verticalAngle   = SERVO_CENTER_ANGLE;
int  s_horizontalAngle = SERVO_CENTER_ANGLE;

// ------------------------------------------------------------
// 角度 → duty（含 clamp）
// ------------------------------------------------------------

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
    uint32_t duty = static_cast<uint32_t>(
                        dutyRatio * static_cast<float>(SERVO_PWM_FULL_SCALE));

    if (duty > SERVO_PWM_FULL_SCALE)
    {
        duty = SERVO_PWM_FULL_SCALE;
    }

    return duty;
}

// ------------------------------------------------------------
// 底层写：将给定角度写入指定 channel（含 clamp + 状态回写）
// ------------------------------------------------------------

void writeVertical(int angle)
{
    int clamped = angle;
    if (clamped < SERVO_MIN_ANGLE) clamped = SERVO_MIN_ANGLE;
    if (clamped > SERVO_MAX_ANGLE) clamped = SERVO_MAX_ANGLE;

    s_verticalAngle = clamped;
    uint32_t duty = angleToDuty(clamped);
    ledcWrite(SERVO_V_PWM_CHANNEL, duty);

    Serial.printf(
        "[servo] vertical %d duty=%lu\n",
        clamped,
        duty
    );
}

void writeHorizontal(int angle)
{
    int clamped = angle;
    if (clamped < SERVO_MIN_ANGLE) clamped = SERVO_MIN_ANGLE;
    if (clamped > SERVO_MAX_ANGLE) clamped = SERVO_MAX_ANGLE;

    s_horizontalAngle = clamped;
    uint32_t duty = angleToDuty(clamped);
    ledcWrite(SERVO_H_PWM_CHANNEL, duty);

    Serial.printf(
        "[servo] horizontal %d duty=%lu\n",
        clamped,
        duty
    );
}

}  // namespace

// ============================================================
// 初始化
// ============================================================

void servo_init(void)
{
    if (s_started)
    {
        return;
    }

    // 两个舵机都使用 50Hz / 13-bit，仅 channel 与 GPIO 不同
    ledcSetup(SERVO_V_PWM_CHANNEL, SERVO_PWM_FREQ_HZ, SERVO_PWM_RES_BITS);
    ledcAttachPin(SERVO_V_SIGNAL_GPIO, SERVO_V_PWM_CHANNEL);
    ledcWrite(SERVO_V_PWM_CHANNEL, 0);

    ledcSetup(SERVO_H_PWM_CHANNEL, SERVO_PWM_FREQ_HZ, SERVO_PWM_RES_BITS);
    ledcAttachPin(SERVO_H_SIGNAL_GPIO, SERVO_H_PWM_CHANNEL);
    ledcWrite(SERVO_H_PWM_CHANNEL, 0);

    s_started = true;
    s_verticalAngle   = SERVO_CENTER_ANGLE;
    s_horizontalAngle = SERVO_CENTER_ANGLE;

    Serial.printf(
        "[servo] init vertical gpio=%d channel=%d\n",
        SERVO_V_SIGNAL_GPIO,
        SERVO_V_PWM_CHANNEL
    );
    Serial.printf(
        "[servo] init horizontal gpio=%d channel=%d\n",
        SERVO_H_SIGNAL_GPIO,
        SERVO_H_PWM_CHANNEL
    );
    Serial.printf(
        "[servo] pwm=%dHz res=%d-bit range=%d..%d center=%d step=%d\n",
        SERVO_PWM_FREQ_HZ,
        SERVO_PWM_RES_BITS,
        SERVO_MIN_ANGLE,
        SERVO_MAX_ANGLE,
        SERVO_CENTER_ANGLE,
        SERVO_STEP_DEG
    );

    // 正式启动：两个舵机都回到中心位（无机械测试序列）
    servo_center_all();
}

// ============================================================
// 旧 API（仅 Tilt / Vertical），保留向后兼容
// ============================================================

void servo_set_angle(int angle)
{
    writeVertical(angle);
}

void servo_center(void)
{
    servo_set_vertical_angle(SERVO_CENTER_ANGLE);
}

void servo_left(void)
{
    servo_set_vertical_angle(SERVO_LEFT_ANGLE);
}

void servo_right(void)
{
    servo_set_vertical_angle(SERVO_RIGHT_ANGLE);
}

// ============================================================
// 新双舵机 API
// ============================================================

int servo_get_vertical_angle(void)
{
    return s_verticalAngle;
}

int servo_get_horizontal_angle(void)
{
    return s_horizontalAngle;
}

void servo_set_vertical_angle(int angle)
{
    if (!s_started)
    {
        return;
    }

    int old = s_verticalAngle;
    int clamped = angle;
    if (clamped < SERVO_MIN_ANGLE) clamped = SERVO_MIN_ANGLE;
    if (clamped > SERVO_MAX_ANGLE) clamped = SERVO_MAX_ANGLE;

    if (clamped == old)
    {
        return;
    }

    writeVertical(clamped);

    if (angle != clamped)
    {
        Serial.printf(
            "[servo] vertical limit reached: %d\n",
            clamped
        );
    }
    else
    {
        Serial.printf(
            "[servo] vertical %d -> %d\n",
            old,
            clamped
        );
    }
}

void servo_set_horizontal_angle(int angle)
{
    if (!s_started)
    {
        return;
    }

    int old = s_horizontalAngle;
    int clamped = angle;
    if (clamped < SERVO_MIN_ANGLE) clamped = SERVO_MIN_ANGLE;
    if (clamped > SERVO_MAX_ANGLE) clamped = SERVO_MAX_ANGLE;

    if (clamped == old)
    {
        return;
    }

    writeHorizontal(clamped);

    if (angle != clamped)
    {
        Serial.printf(
            "[servo] horizontal limit reached: %d\n",
            clamped
        );
    }
    else
    {
        Serial.printf(
            "[servo] horizontal %d -> %d\n",
            old,
            clamped
        );
    }
}

void servo_center_vertical(void)
{
    servo_set_vertical_angle(SERVO_CENTER_ANGLE);
}

void servo_center_horizontal(void)
{
    servo_set_horizontal_angle(SERVO_CENTER_ANGLE);
}

void servo_center_all(void)
{
    if (!s_started)
    {
        return;
    }

    writeVertical(SERVO_CENTER_ANGLE);
    writeHorizontal(SERVO_CENTER_ANGLE);

    Serial.printf(
        "[servo] center_all vertical=%d horizontal=%d\n",
        s_verticalAngle,
        s_horizontalAngle
    );
}

void servo_step_vertical(int delta)
{
    servo_set_vertical_angle(s_verticalAngle + delta);
}

void servo_step_horizontal(int delta)
{
    servo_set_horizontal_angle(s_horizontalAngle + delta);
}

// ============================================================
// 硬件自检序列（不用于正常启动）
// ============================================================

void servo_run_test_sequence(void)
{
    // 旧 API：仅跑 Tilt
    servo_init();

    Serial.println("[servo-test] VERTICAL CENTER");
    servo_center_vertical();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] VERTICAL LEFT");
    servo_set_vertical_angle(SERVO_LEFT_ANGLE);
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] VERTICAL CENTER");
    servo_center_vertical();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] VERTICAL RIGHT");
    servo_set_vertical_angle(SERVO_RIGHT_ANGLE);
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] VERTICAL CENTER");
    servo_center_vertical();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] done");
}

void servo_run_horizontal_test_sequence(void)
{
    servo_init();

    Serial.println("[servo-test] HORIZONTAL CENTER");
    servo_center_horizontal();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] HORIZONTAL LEFT");
    servo_set_horizontal_angle(SERVO_LEFT_ANGLE);
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] HORIZONTAL CENTER");
    servo_center_horizontal();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] HORIZONTAL RIGHT");
    servo_set_horizontal_angle(SERVO_RIGHT_ANGLE);
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] HORIZONTAL CENTER");
    servo_center_horizontal();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] done");
}

void servo_run_dual_test_sequence(void)
{
    servo_init();

    Serial.println("[servo-test] DUAL CENTER");
    servo_center_all();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] HORIZONTAL LEFT");
    servo_set_horizontal_angle(SERVO_LEFT_ANGLE);
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] HORIZONTAL CENTER");
    servo_center_horizontal();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] HORIZONTAL RIGHT");
    servo_set_horizontal_angle(SERVO_RIGHT_ANGLE);
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] HORIZONTAL CENTER");
    servo_center_horizontal();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] VERTICAL UP (RIGHT)");
    servo_set_vertical_angle(SERVO_RIGHT_ANGLE);
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] VERTICAL CENTER");
    servo_center_vertical();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] VERTICAL DOWN (LEFT)");
    servo_set_vertical_angle(SERVO_LEFT_ANGLE);
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] VERTICAL CENTER");
    servo_center_vertical();
    delay(SERVO_TEST_STEP_MS);

    Serial.println("[servo-test] dual done");
}
