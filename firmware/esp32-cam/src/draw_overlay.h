// ============================================================
// DrawOverlay · Step 12-2-A
//
// 目的：
//   在 QQVGA 160x120 RGB565 framebuffer 上直接绘制检测框、
//   中心点和参数文字。
//
// 特点：
//   - 直接在 RGB565 像素上操作，无额外 buffer
//   - 使用极小 5x7 bitmap font
//   - 无 heap 分配
//
// 支持字符集：
//   0-9 A-Z 空格 . - : / = >
//   其他字符显示为空格
//
// 不做：
//   字体抗锯齿 / 双宽字符 / 中文 / 缩放 / 透明混合
// ============================================================

#ifndef DRAW_OVERLAY_H
#define DRAW_OVERLAY_H

#include <cstdint>
#include <cstddef>
#include "person_detector.h"


// ============================================================================
// 颜色（RGB565 packed 16bit，0xRRRGBB 形式）
// ============================================================================

#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_WHITE   0xFFFF
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F


// ============================================================================
// 单点绘制
//
// rgb565 是 uint16_t 数组
// bufW / bufH 是 framebuffer 的宽 / 高
// 自动 clamp 到 framebuffer 边界
// ============================================================================

void drawPixel(
    uint16_t *rgb565,
    int bufW,
    int bufH,
    int x,
    int y,
    uint16_t color
);


// ============================================================================
// 矩形边框
//
// thickness 通常为 1
// 支持边界 clamp
// ============================================================================

void drawRectOutline(
    uint16_t *rgb565,
    int bufW,
    int bufH,
    int x0,
    int y0,
    int x1,
    int y1,
    uint16_t color,
    int thickness
);


// ============================================================================
// 填充矩形（用于文字背景）
// ============================================================================

void drawRectFilled(
    uint16_t *rgb565,
    int bufW,
    int bufH,
    int x0,
    int y0,
    int x1,
    int y1,
    uint16_t color
);


// ============================================================================
// 十字点
// ============================================================================

void drawCross(
    uint16_t *rgb565,
    int bufW,
    int bufH,
    int cx,
    int cy,
    int arm,
    uint16_t color
);


// ============================================================================
// 文字绘制（左对齐，无换行）
// ============================================================================

void drawText(
    uint16_t *rgb565,
    int bufW,
    int bufH,
    int x,
    int y,
    const char *text,
    uint16_t color
);


// ============================================================================
// 主入口：绘制人物检测 overlay
//
// 参数：
//   rgb565 : QQVGA 160x120 RGB565 像素 buffer（16bit 元素）
//   bufW   : buffer 宽（预期 160）
//   bufH   : buffer 高（预期 120）
//   det    : 检测结果
//
// 绘制内容：
//   1. bounding box (绿色)
//   2. 中心十字点 (红色)
//   3. 顶部信息条：
//      PERSON: YES/NO
//      CX/CY (归一化 x.xx)
//      W/H (归一化)
//      CONF (归一化)
//
// 检测框坐标从 0.0~1.0 转换到 0~bufW-1 / 0~bufH-1
// 且做 clamp
// ============================================================================

void drawPersonOverlay(
    uint16_t *rgb565,
    int bufW,
    int bufH,
    const PersonDetection &det
);


#endif  // DRAW_OVERLAY_H
