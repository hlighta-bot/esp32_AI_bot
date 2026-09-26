// ============================================================
// servo_control.h - MG90S 舵机最小控制模块
// ============================================================
//
// 目的：
//   为 ESP32-S3 提供一组极简、独立的 MG90S 舵机接口，
//   仅支持 center / left / right 三个安全角度。
//
// 设计原则：
//   1. 参数集中定义，避免散落
//   2. 不引入额外库，使用 ESP32 原生 LEDC
//   3. 不影响既有音频 / 网络 / Web 功能
//   4. 第一版只做最小动作验证，不做复杂状态机
// ============================================================

#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include <Arduino.h>

// ============================================================
// 硬件参数
// ============================================================

#define SERVO_SIGNAL_GPIO       5
#define SERVO_PWM_FREQ_HZ       50
#define SERVO_PWM_RES_BITS      13
#define SERVO_PWM_CHANNEL       0
#define SERVO_TEST_STEP_MS      1000

// ============================================================
// 角度范围（保守值，避免极端角和过大脉宽）
// ============================================================

#define SERVO_CENTER_ANGLE      90
#define SERVO_LEFT_ANGLE        60
#define SERVO_RIGHT_ANGLE       120

void servo_init(void);
void servo_set_angle(int angle);
void servo_center(void);
void servo_left(void);
void servo_right(void);
void servo_run_test_sequence(void);

#endif  // SERVO_CONTROL_H
