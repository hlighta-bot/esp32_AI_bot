// ============================================================
// servo_control.h - MG90S 双舵机控制模块（Pan / Tilt）
// ============================================================
//
// 目的：
//   为 ESP32-S3 提供 Pan (Horizontal, GPIO4) + Tilt (Vertical, GPIO5)
//   双舵机控制接口。每个舵机使用独立 GPIO + 独立 LEDC channel，
//   互不干扰。
//
// 设计原则：
//   1. 参数集中定义，避免散落
//   2. 不引入额外库，使用 ESP32 原生 LEDC
//   3. 不影响既有音频 / 网络 / Web 功能
//   4. 兼容旧 API（servo_center / servo_left / servo_right 等）
//      这些旧函数只操作 Tilt（Vertical / GPIO5）
//   5. 正式运行时只做 init + center，不跑机械测试序列
//   6. 状态（当前角度）由 ESP32 保存，PC 无状态
// ============================================================

#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include <Arduino.h>

// ============================================================
// 硬件参数 - Vertical (Tilt, 上下)
// ============================================================

#define SERVO_V_SIGNAL_GPIO       5
#define SERVO_V_PWM_CHANNEL       0

// ============================================================
// 硬件参数 - Horizontal (Pan, 左右)
// ============================================================

#define SERVO_H_SIGNAL_GPIO       4
#define SERVO_H_PWM_CHANNEL       1

// ============================================================
// 公共 PWM 参数（两个舵机共用）
// ============================================================

#define SERVO_PWM_FREQ_HZ       50
#define SERVO_PWM_RES_BITS      13
#define SERVO_TEST_STEP_MS      1000

// ============================================================
// 角度范围（保守值，避免极端角和过大脉宽）
//
// 两个舵机使用相同安全范围：60° ~ 120°，center 90°，步进 10°。
// 详见 Step 12 计划与 docs/hardware.md。
// ============================================================

#define SERVO_CENTER_ANGLE      90
#define SERVO_MIN_ANGLE         60
#define SERVO_MAX_ANGLE         120

// 旧符号保留（向后兼容：servo_left / servo_right 使用）
#define SERVO_LEFT_ANGLE        SERVO_MIN_ANGLE
#define SERVO_RIGHT_ANGLE       SERVO_MAX_ANGLE

// 步进（°）
#define SERVO_STEP_DEG          10

// ============================================================
// 初始化 & 基础 API
// ============================================================

void servo_init(void);
void servo_run_test_sequence(void);

// ============================================================
// 旧 API（仅 Tilt / Vertical），保留向后兼容
// ============================================================

void servo_set_angle(int angle);
void servo_center(void);
void servo_left(void);
void servo_right(void);

// ============================================================
// 新双舵机 API（Pan / Tilt 分离）
// ============================================================

int  servo_get_vertical_angle(void);
int  servo_get_horizontal_angle(void);
void servo_set_vertical_angle(int angle);
void servo_set_horizontal_angle(int angle);
void servo_center_vertical(void);
void servo_center_horizontal(void);
void servo_center_all(void);
void servo_step_vertical(int delta);      // + = UP, - = DOWN
void servo_step_horizontal(int delta);    // + = RIGHT, - = LEFT

// ============================================================
// 硬件自检序列（不用于正常启动）
// ============================================================

void servo_run_dual_test_sequence(void);  // Pan + Tilt 全序列
void servo_run_horizontal_test_sequence(void);  // 仅 Pan

#endif  // SERVO_CONTROL_H
