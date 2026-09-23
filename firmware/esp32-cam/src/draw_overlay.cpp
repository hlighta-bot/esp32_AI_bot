// ============================================================
// DrawOverlay · Step 12-2-A
//
// 实现：
//   1. drawPixel / drawRectOutline / drawRectFilled / drawCross
//   2. 5x7 bitmap font（0-9, A-Z, 空格, . - : / = >）
//   3. drawText
//   4. drawPersonOverlay（组合 bounding box + 中心点 + 信息条）
//
// 内存：全部内联 + 静态 const，零 heap 分配
// ============================================================

#include "draw_overlay.h"

#include <cstring>
#include <cstdio>


// ============================================================================
// 5x7 Bitmap Font
//
// 每字符：5 列 x 7 行
// 每字节代表一行的 5 个像素（LSB = 最左像素）
// 字符占 7 字节
//
// 索引方式：font[char - ' '][row]  或  font[(char - ' ')*7 + row]
//
// 使用 flat array，节省内存
// ============================================================================

// 支持的字符：
//   空格 (0x20)
//   '.' (0x2E)
//   '-' (0x2D)
//   ':' (0x3A)
//   '/' (0x2F)
//   '=' (0x3D)
//   '>' (0x3E)
//   '0'-'9' (0x30-0x39)
//   'A'-'Z' (0x41-0x5A)
//
// 字符码 -> 索引映射使用函数，避免大 sparse table

// 5x7 字符集定义
// 每个字符 7 字节：row0..row6
// bit 0 = 最左像素

#define FONT_WIDTH   5
#define FONT_HEIGHT  7
#define FONT_STEP    (FONT_WIDTH + 1)  // 6 px wide per char (含 1 px 间距)

// 字符索引定义
#define IDX_SPACE 0
#define IDX_DOT   1
#define IDX_MINUS 2
#define IDX_COLON 3
#define IDX_SLASH 4
#define IDX_EQ    5
#define IDX_GT    6
// 数字 0-9: idx 7-16
#define IDX_0     7
// 字母 A-Z: idx 17-42
#define IDX_A     17

static const uint8_t s_font[][FONT_HEIGHT] = {
    // 0x20 SPACE  ----
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // space

    // 0x2E '.'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18},

    // 0x2D '-'
    {0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00},

    // 0x3A ':'
    {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00},

    // 0x2F '/'
    {0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x00},

    // 0x3D '='
    {0x00, 0x00, 0x3C, 0x00, 0x3C, 0x00, 0x00},

    // 0x3E '>'
    {0x20, 0x10, 0x08, 0x04, 0x08, 0x10, 0x20},

    // '0' 0x30
    {0x1C, 0x22, 0x51, 0x51, 0x51, 0x22, 0x1C},

    // '1' 0x31
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x1C},

    // '2' 0x32
    {0x1C, 0x22, 0x02, 0x04, 0x08, 0x10, 0x3E},

    // '3' 0x33
    {0x1C, 0x22, 0x02, 0x1C, 0x02, 0x22, 0x1C},

    // '4' 0x34
    {0x02, 0x06, 0x0A, 0x12, 0x3E, 0x02, 0x02},

    // '5' 0x35
    {0x3E, 0x20, 0x3C, 0x02, 0x02, 0x22, 0x1C},

    // '6' 0x36
    {0x0C, 0x10, 0x20, 0x3C, 0x22, 0x22, 0x1C},

    // '7' 0x37
    {0x3E, 0x02, 0x04, 0x08, 0x10, 0x10, 0x10},

    // '8' 0x38
    {0x1C, 0x22, 0x22, 0x1C, 0x22, 0x22, 0x1C},

    // '9' 0x39
    {0x1C, 0x22, 0x22, 0x1E, 0x02, 0x0C, 0x18},

    // 'A' 0x41
    {0x1C, 0x22, 0x22, 0x3E, 0x22, 0x22, 0x22},

    // 'B' 0x42
    {0x3C, 0x22, 0x22, 0x3C, 0x22, 0x22, 0x3C},

    // 'C' 0x43
    {0x1C, 0x22, 0x20, 0x20, 0x20, 0x22, 0x1C},

    // 'D' 0x44
    {0x3C, 0x22, 0x22, 0x22, 0x22, 0x22, 0x3C},

    // 'E' 0x45
    {0x3E, 0x20, 0x20, 0x3C, 0x20, 0x20, 0x3E},

    // 'F' 0x46
    {0x3E, 0x20, 0x20, 0x3C, 0x20, 0x20, 0x20},

    // 'G' 0x47
    {0x1C, 0x22, 0x20, 0x2E, 0x22, 0x22, 0x1E},

    // 'H' 0x48
    {0x22, 0x22, 0x22, 0x3E, 0x22, 0x22, 0x22},

    // 'I' 0x49
    {0x1C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1C},

    // 'J' 0x4A
    {0x38, 0x08, 0x08, 0x08, 0x08, 0x28, 0x10},

    // 'K' 0x4B
    {0x22, 0x24, 0x28, 0x30, 0x28, 0x24, 0x22},

    // 'L' 0x4C
    {0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x3E},

    // 'M' 0x4D
    {0x22, 0x36, 0x2A, 0x2A, 0x22, 0x22, 0x22},

    // 'N' 0x4E
    {0x22, 0x32, 0x3A, 0x2A, 0x26, 0x22, 0x22},

    // 'O' 0x4F
    {0x1C, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1C},

    // 'P' 0x50
    {0x3C, 0x22, 0x22, 0x3C, 0x20, 0x20, 0x20},

    // 'Q' 0x51
    {0x1C, 0x22, 0x22, 0x22, 0x26, 0x2A, 0x14},

    // 'R' 0x52
    {0x3C, 0x22, 0x22, 0x3C, 0x28, 0x24, 0x22},

    // 'S' 0x53
    {0x1E, 0x20, 0x20, 0x1C, 0x02, 0x02, 0x3C},

    // 'T' 0x54
    {0x3E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},

    // 'U' 0x55
    {0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1C},

    // 'V' 0x56
    {0x22, 0x22, 0x22, 0x22, 0x22, 0x14, 0x08},

    // 'W' 0x57
    {0x22, 0x22, 0x22, 0x2A, 0x2A, 0x36, 0x22},

    // 'X' 0x58
    {0x22, 0x22, 0x14, 0x08, 0x14, 0x22, 0x22},

    // 'Y' 0x59
    {0x22, 0x22, 0x22, 0x14, 0x08, 0x08, 0x08},

    // 'Z' 0x5A
    {0x3E, 0x02, 0x04, 0x08, 0x10, 0x20, 0x3E}
};

#define FONT_CHAR_COUNT (sizeof(s_font) / sizeof(s_font[0]))


// ============================================================================
// 字符索引查找
//
// 未支持字符返回 -1
// ============================================================================

static int getCharIndex(char c)
{
    if (c == ' ')   return IDX_SPACE;
    if (c == '.')   return IDX_DOT;
    if (c == '-')   return IDX_MINUS;
    if (c == ':')   return IDX_COLON;
    if (c == '/')   return IDX_SLASH;
    if (c == '=')   return IDX_EQ;
    if (c == '>')   return IDX_GT;
    if (c >= '0' && c <= '9') return IDX_0 + (c - '0');
    if (c >= 'A' && c <= 'Z') return IDX_A + (c - 'A');

    // 小写字母自动大写
    if (c >= 'a' && c <= 'z') return IDX_A + (c - 'a');

    return -1;
}


// ============================================================================
// 基础绘制
// ============================================================================

void drawPixel(
    uint16_t *rgb565,
    int bufW,
    int bufH,
    int x,
    int y,
    uint16_t color)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= bufW) return;
    if (y >= bufH) return;

    rgb565[y * bufW + x] = color;
}


void drawRectOutline(
    uint16_t *rgb565,
    int bufW,
    int bufH,
    int x0,
    int y0,
    int x1,
    int y1,
    uint16_t color,
    int thickness)
{
    // clamp
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= bufW) x1 = bufW - 1;
    if (y1 >= bufH) y1 = bufH - 1;

    if (x0 > x1 || y0 > y1) return;

    if (thickness < 1) thickness = 1;

    // 上
    for (int t = 0; t < thickness; t++)
    {
        for (int x = x0; x <= x1; x++)
        {
            drawPixel(rgb565, bufW, bufH, x, y0 + t, color);
        }
    }
    // 下
    for (int t = 0; t < thickness; t++)
    {
        for (int x = x0; x <= x1; x++)
        {
            drawPixel(rgb565, bufW, bufH, x, y1 - t, color);
        }
    }
    // 左
    for (int t = 0; t < thickness; t++)
    {
        for (int y = y0; y <= y1; y++)
        {
            drawPixel(rgb565, bufW, bufH, x0 + t, y, color);
        }
    }
    // 右
    for (int t = 0; t < thickness; t++)
    {
        for (int y = y0; y <= y1; y++)
        {
            drawPixel(rgb565, bufW, bufH, x1 - t, y, color);
        }
    }
}


void drawRectFilled(
    uint16_t *rgb565,
    int bufW,
    int bufH,
    int x0,
    int y0,
    int x1,
    int y1,
    uint16_t color)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= bufW) x1 = bufW - 1;
    if (y1 >= bufH) y1 = bufH - 1;

    if (x0 > x1 || y0 > y1) return;

    for (int y = y0; y <= y1; y++)
    {
        for (int x = x0; x <= x1; x++)
        {
            rgb565[y * bufW + x] = color;
        }
    }
}


void drawCross(
    uint16_t *rgb565,
    int bufW,
    int bufH,
    int cx,
    int cy,
    int arm,
    uint16_t color)
{
    if (arm < 1) arm = 1;

    for (int dx = -arm; dx <= arm; dx++)
    {
        drawPixel(rgb565, bufW, bufH, cx + dx, cy, color);
    }
    for (int dy = -arm; dy <= arm; dy++)
    {
        drawPixel(rgb565, bufW, bufH, cx, cy + dy, color);
    }
}


// ============================================================================
// 文字绘制
// ============================================================================

void drawText(
    uint16_t *rgb565,
    int bufW,
    int bufH,
    int x,
    int y,
    const char *text,
    uint16_t color)
{
    if (text == nullptr) return;

    int cx = x;
    int cy = y;

    const char *p = text;
    while (*p)
    {
        int idx = getCharIndex(*p);

        if (idx >= 0 && idx < (int)FONT_CHAR_COUNT)
        {
            // 绘制 5x7 字符
            for (int row = 0; row < FONT_HEIGHT; row++)
            {
                uint8_t rowBits = s_font[idx][row];

                for (int col = 0; col < FONT_WIDTH; col++)
                {
                    if (rowBits & (1 << col))
                    {
                        drawPixel(
                            rgb565, bufW, bufH,
                            cx + col, cy + row,
                            color
                        );
                    }
                }
            }
        }

        cx += FONT_STEP;
        p++;
    }
}


// ============================================================================
// 主入口
//
// 布局（160 x 120）：
//
//   [ 0-155, 0-31 ] 顶部信息条（黑色背景，2 行文字）
//   [ 0-155, 32-119 ] 检测框 + 中心十字点
//
// 备注：
//   Arduino snprintf 不支持 %f（除非 -D__printf_float），
//   因此使用 fmtFloat() 手动将 float 转为 "0.NN" 字符串。
//   字符集 5x7，QQVGA 160 宽 ≈ 26 字符/行，文字行保持简洁。
// ============================================================================

// 手动格式化 float 为定点字符串，输出 "N.NN"
// val 自动 clamp 到 [0.0, 1.0]
static void fmtFloat(char *out, size_t outSize, float val)
{
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;
    long hundredths = (long)(val * 100.0f + 0.5f);
    long intPart   = hundredths / 100;
    long fracPart  = hundredths % 100;
    snprintf(out, outSize, "%ld.%02ld", intPart, fracPart);
}


void drawPersonOverlay(
    uint16_t *rgb565,
    int bufW,
    int bufH,
    const PersonDetection &det)
{
    // ------------------------------------------------------------
    // 顶部信息条：2 行文字（7 px 高 + 11 px 间距）
    // 黑色背景（无 alpha 混合）保证文字清晰
    // ------------------------------------------------------------

    drawRectFilled(rgb565, bufW, bufH, 0, 0, bufW - 1, 31, 0x0000);

    // 行 1: P:Y X:0.50 Y:0.50
    // 字符集宽度预算：QQVGA 160 / 6 = 26 字符
    char line1[32];
    char s1[8], s2[8];
    fmtFloat(s1, sizeof(s1), det.center_x);
    fmtFloat(s2, sizeof(s2), det.center_y);
    snprintf(line1, sizeof(line1), "P:%c X:%s Y:%s",
             det.detected ? 'Y' : 'N', s1, s2);
    drawText(rgb565, bufW, bufH, 2, 2, line1,
             det.detected ? COLOR_GREEN : COLOR_RED);

    // 行 2: W:0.30 H:0.40 C:0.60
    char line2[32];
    char s3[8], s4[8], s5[8];
    fmtFloat(s3, sizeof(s3), det.width);
    fmtFloat(s4, sizeof(s4), det.height);
    fmtFloat(s5, sizeof(s5), det.confidence);
    snprintf(line2, sizeof(line2), "W:%s H:%s C:%s", s3, s4, s5);
    drawText(rgb565, bufW, bufH, 2, 13, line2,
             det.confidence >= 0.5f ? COLOR_YELLOW : COLOR_CYAN);

    // 行 3: 状态说明
    char line3[32];
    if (det.detected)
    {
        snprintf(line3, sizeof(line3), "SKIN OK");
        drawText(rgb565, bufW, bufH, 2, 22, line3, COLOR_WHITE);
    }
    else
    {
        snprintf(line3, sizeof(line3), "NO SKIN");
        drawText(rgb565, bufW, bufH, 2, 22, line3, COLOR_RED);
    }

    // ------------------------------------------------------------
    // 检测框（仅在检测到且信息条以下有空间时绘制）
    // ------------------------------------------------------------

    if (det.detected)
    {
        // 归一化坐标 -> 像素坐标
        int boxX0 = (int)(det.center_x * bufW) -
                    (int)(det.width    * bufW / 2.0f);
        int boxY0 = (int)(det.center_y * bufH) -
                    (int)(det.height   * bufH / 2.0f);
        int boxX1 = (int)(det.center_x * bufW) +
                    (int)(det.width    * bufW / 2.0f);
        int boxY1 = (int)(det.center_y * bufH) +
                    (int)(det.height   * bufH / 2.0f);

        // 计算中心点
        int cx = (int)(det.center_x * bufW);
        int cy = (int)(det.center_y * bufH);

        // 画框（绿色，2 px 粗）
        drawRectOutline(rgb565, bufW, bufH,
                        boxX0, boxY0, boxX1, boxY1,
                        COLOR_GREEN, 2);

        // 中心十字点（红色，5 px 臂长）
        drawCross(rgb565, bufW, bufH, cx, cy, 5, COLOR_RED);
    }
}
