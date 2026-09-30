// ============================================================
// display.h - ST7735S 160x80 LCD 显示模块
// ============================================================
//
// 目的：
//   为 ESP32-S3 提供 0.96" ST7735S IPS LCD 的最小显示能力。
//   用于显示机器人当前状态（READY / LISTENING / THINKING /
//   SPEAKING / SERVO / WIFI / STARTING / ERROR）与简单机器人脸。
//
// 硬件接线（ST7735S 160x80，4-line SPI）：
//   SCL -> GPIO14
//   SDA -> GPIO13
//   RES -> GPIO12
//   DC  -> GPIO11
//   CS  -> GPIO10
//   BLK -> 3.3V（常亮，不使用 PWM 调光）
//   VCC -> 3.3V，GND -> GND
//
// 设计原则：
//   1. LCD 是辅助模块。任何 display_* API 都不能阻塞主程序。
//   2. 未 ready 时所有 API no-op（不使用 C++ try/catch 兜底）。
//   3. 采用"状态改变才重绘"策略，不每轮刷新。
//   4. 不实现动画、不实现触摸（硬件不支持）、不做中文。
//   5. 不暴露 Adafruit_GFX / ST7735 内部类型，主程序只看到本 header。
//   6. Display failure must not block Robot Core.
//
// 依赖：Adafruit_ST7735 + Adafruit_GFX（见 platformio.ini lib_deps）
// ============================================================

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

// ============================================================
// 状态字符串（display_set_state 参数）
//
// 主程序使用这些字符串常量调用 display_set_state()，
// display.cpp 内部映射到对应的绘制内容。
// ============================================================

#define DISPLAY_STATE_STARTING  "STARTING"
#define DISPLAY_STATE_WIFI      "WIFI"
#define DISPLAY_STATE_READY     "READY"
#define DISPLAY_STATE_LISTENING "LISTENING"
#define DISPLAY_STATE_THINKING  "THINKING"
#define DISPLAY_STATE_SPEAKING  "SPEAKING"
#define DISPLAY_STATE_SERVO     "SERVO"
#define DISPLAY_STATE_ERROR     "ERROR"

// ============================================================
// 表情字符串（display_show_face 参数）
// ============================================================

#define DISPLAY_FACE_NORMAL    "normal"
#define DISPLAY_FACE_LISTENING "listening"
#define DISPLAY_FACE_THINKING  "thinking"
#define DISPLAY_FACE_SPEAKING  "speaking"
#define DISPLAY_FACE_ERROR     "error"

// ============================================================
// 公共 API
//
// 所有函数在未初始化（display_init 失败或未调用）时 no-op，
// 不阻塞、不 panic、不 while。
// ============================================================

/// 初始化 LCD。失败时打印日志，返回 false，其余 API 全部 no-op。
/// 内部完成：SPI.begin / ST7735 init / tab config / rotation / inversion。
void display_init();

/// 清屏（黑色）。
void display_clear();

/// 显示纯文本（顶部区，1-3 行 ASCII）。
/// 用于覆盖式显示短消息；调用后不影响机器人脸区域。
void display_show_text(const char* text);

/// 显示指定表情的机器人脸。
/// expression 必须是 DISPLAY_FACE_* 之一；其他值视为 normal。
void display_show_face(const char* expression);

/// 显示机器人当前状态（状态改变才重绘）。
/// state 必须是 DISPLAY_STATE_* 之一；重复调用相同状态是 no-op。
/// 内部会同时更新表情和文字区域。
///
/// 由于内部采用 DISPLAY_MIN_REFRESH_MS=120 的节流策略，
/// 状态可能在短时间内排队（pending），需要 loop() 中定期调用
/// display_pump() 来完成实际重绘。
void display_set_state(const char* state);

/// 非阻塞 pump：距离上次重绘 >= DISPLAY_MIN_REFRESH_MS 且有待绘制状态时，
/// 立即绘制最新 pending 状态。否则立即返回。
/// 主循环应周期性调用，确保 pending 状态最终一定会显示。
void display_pump();

#endif  // DISPLAY_H
