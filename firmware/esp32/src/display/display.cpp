// ============================================================
// display.cpp - ST7735S 160x80 LCD 显示实现
// ============================================================
//
// 实现：
//   - ST7735S 初始化（4-line SPI，160x80 tab config）
//   - 机器人状态显示（8 种状态）
//   - 简单机器人脸（5 种表情）
//   - 未 ready 时所有 API no-op
//   - 状态改变才重绘
//   - 最小刷新间隔 DISPLAY_MIN_REFRESH_MS=120 ms：
//     两次实际重绘之间至少间隔 120 ms。120 ms 内多次
//     display_set_state 会被合并，只绘制最新状态。
//
// 关键原则：
//   "Display failure must not block Robot Core."
//
// 布局（160x80）：
//   整个 UI 相对原设计整体下移 Y_OFFSET=8 px（避免眼睛贴顶）。
//     眼：y = 22 + 8 = 30
//     嘴：y = 42 + 8 = 50
//     文字：y = 60 + 8 = 68
// ============================================================

#include "display.h"

#include <Adafruit_ST7735.h>

// ============================================================
// 硬件参数
// ============================================================

#define LCD_SCLK_GPIO   14
#define LCD_MOSI_GPIO   13
#define LCD_RST_GPIO    12
#define LCD_DC_GPIO     11
#define LCD_CS_GPIO     10

// 布局整体 Y 偏移：脸/文字相对原设计统一下移 8 px，
// 避免眼睛贴 LCD 上沿。所有绘图 Y 坐标必须加此常量。
static constexpr int Y_OFFSET = 8;

// ST7735S 160x80 tab config（Adafruit_ST7735 的 INITR_GREENTAB 会自动
// 设置 colstart=26, rowstart=1）
#define LCD_ROTATION    1  // LANDSCAPE（160 宽 × 80 高）

// SPI 时钟频率：Adafruit_ST7735::begin(freq) 参数单位为 kHz。
#define LCD_SPI_KHZ     20000  // 20 MHz

// 最小刷新间隔：两次实际重绘之间至少间隔这么多毫秒。
// 期间多次 display_set_state 会被合并，只绘制最新状态。
static constexpr uint32_t DISPLAY_MIN_REFRESH_MS = 120;

// Adafruit_ST7735 颜色（RGB565）
#define LCD_COLOR_BLACK  ST7735_BLACK
#define LCD_COLOR_WHITE  ST7735_WHITE
#define LCD_COLOR_RED    ST7735_RED

// ============================================================
// 内部对象
//
// Adafruit_ST7735 构造函数签名：
//   Adafruit_ST7735(int8_t cs, int8_t dc, int8_t mosi, int8_t sclk, int8_t rst)
// ============================================================

static Adafruit_ST7735 g_tft = Adafruit_ST7735(
    LCD_CS_GPIO, LCD_DC_GPIO, LCD_MOSI_GPIO, LCD_SCLK_GPIO, LCD_RST_GPIO
);
static bool g_displayReady = false;

// 刷新节流状态：
//   g_currentState    当前 LCD 上正在显示的状态（可能不是最新的）
//   g_pendingState    最近一次请求的目标状态；非 null 表示有待刷新的状态
//   g_lastRefreshMs   上一次实际重绘的 millis()
static const char* g_currentState = nullptr;
static const char* g_pendingState = nullptr;
static uint32_t    g_lastRefreshMs = 0;

// ============================================================
// 内部工具
// ============================================================

static void drawFace(const char* expression)
{
    if (!g_displayReady)
    {
        return;
    }

    uint16_t eye = LCD_COLOR_WHITE;
    uint16_t mouth = LCD_COLOR_WHITE;

    if (expression != nullptr &&
        strcmp(expression, DISPLAY_FACE_ERROR) == 0)
    {
        eye = LCD_COLOR_RED;
        mouth = LCD_COLOR_RED;
    }

    g_tft.fillScreen(LCD_COLOR_BLACK);

    const int eyeY   = 22 + Y_OFFSET;   // 30
    const int mouthY = 42 + Y_OFFSET;   // 50
    const int eyeLeftX  = 55;
    const int eyeRightX = 105;

    if (expression != nullptr &&
        strcmp(expression, DISPLAY_FACE_ERROR) == 0)
    {
        // 错误：X 眼睛
        g_tft.drawLine(eyeLeftX - 5, eyeY - 5, eyeLeftX + 5, eyeY + 5, eye);
        g_tft.drawLine(eyeLeftX - 5, eyeY + 5, eyeLeftX + 5, eyeY - 5, eye);
        g_tft.drawLine(eyeRightX - 5, eyeY - 5, eyeRightX + 5, eyeY + 5, eye);
        g_tft.drawLine(eyeRightX - 5, eyeY + 5, eyeRightX + 5, eyeY - 5, eye);
    }
    else if (expression != nullptr &&
             strcmp(expression, DISPLAY_FACE_LISTENING) == 0)
    {
        // 监听：眼睛放大
        g_tft.fillCircle(eyeLeftX, eyeY, 6, eye);
        g_tft.fillCircle(eyeRightX, eyeY, 6, eye);
    }
    else if (expression != nullptr &&
             strcmp(expression, DISPLAY_FACE_THINKING) == 0)
    {
        // 思考：眼睛眯起（横线）
        g_tft.drawLine(eyeLeftX - 5, eyeY, eyeLeftX + 5, eyeY, eye);
        g_tft.drawLine(eyeRightX - 5, eyeY, eyeRightX + 5, eyeY, eye);
    }
    else if (expression != nullptr &&
             strcmp(expression, DISPLAY_FACE_SPEAKING) == 0)
    {
        // 说话：眼睛正常 + 嘴巴张开（下）
        g_tft.fillCircle(eyeLeftX, eyeY, 4, eye);
        g_tft.fillCircle(eyeRightX, eyeY, 4, eye);
        g_tft.fillCircle(80, mouthY, 5, mouth);
        return;
    }
    else
    {
        // normal：两个小圆点
        g_tft.fillCircle(eyeLeftX, eyeY, 4, eye);
        g_tft.fillCircle(eyeRightX, eyeY, 4, eye);
    }

    // 嘴巴（默认横线）
    g_tft.drawLine(65, mouthY, 95, mouthY, mouth);
}

static void drawStateText(const char* state)
{
    if (!g_displayReady)
    {
        return;
    }

    g_tft.setTextColor(LCD_COLOR_WHITE);
    g_tft.setTextSize(1);

    const size_t textLen = strlen(state);
    const int16_t charW = 6;  // Font 0 字符宽 6px
    const int16_t textW = textLen * charW;
    const int16_t x = (160 - textW) / 2;
    const int16_t y = 60 + Y_OFFSET;  // 68

    // 覆盖旧文字（避免残留）
    g_tft.fillRect(x - 2, y - 2, textW + 4, 12, LCD_COLOR_BLACK);
    g_tft.setCursor(x, y);
    g_tft.print(state);
}

// 状态 → 表情映射（保留原有语义）
static const char* mapFaceForState(const char* state)
{
    if (strcmp(state, DISPLAY_STATE_LISTENING) == 0)
    {
        return DISPLAY_FACE_LISTENING;
    }
    else if (strcmp(state, DISPLAY_STATE_THINKING) == 0)
    {
        return DISPLAY_FACE_THINKING;
    }
    else if (strcmp(state, DISPLAY_STATE_SPEAKING) == 0)
    {
        return DISPLAY_FACE_SPEAKING;
    }
    else if (strcmp(state, DISPLAY_STATE_ERROR) == 0)
    {
        return DISPLAY_FACE_ERROR;
    }
    return DISPLAY_FACE_NORMAL;
}

// 执行一次实际重绘：清空屏、画脸、画状态文字。
static void doDraw(const char* state)
{
    if (!g_displayReady)
    {
        return;
    }

    g_tft.fillScreen(LCD_COLOR_BLACK);
    drawFace(mapFaceForState(state));
    drawStateText(state);

    g_currentState = state;
    g_lastRefreshMs = millis();
}

// 若满足节流条件（有待绘制状态且距上次绘制 >= DISPLAY_MIN_REFRESH_MS），
// 立即执行一次绘制。否则立即返回，不阻塞主循环。
//
// 公开 API：由 main.cpp::loop() 周期性调用，确保 pending 状态最终一定会被绘制。
void display_pump()
{
    if (!g_displayReady)
    {
        return;
    }

    if (g_pendingState == nullptr)
    {
        return;
    }

    if (millis() - g_lastRefreshMs < DISPLAY_MIN_REFRESH_MS)
    {
        return;  // 距离上次绘制太近，稍后再试
    }

    const char* pending = g_pendingState;
    g_pendingState = nullptr;
    doDraw(pending);
}

// ============================================================
// 公共 API 实现
// ============================================================

void display_init()
{
    // 任何一步失败都不阻塞主程序；仅设置 g_displayReady = false
    g_displayReady = false;

    // initR 内部会调用 begin(freq) 完成 SPI 时钟设置与 ST7735S 时序初始化。
    // 使用 INITR_GREENTAB：自动配置 colstart=26, rowstart=1（160x80 tab）。
    // 构造函数已绑定 cs/dc/mosi/sclk/rst GPIO，无需再显式 SPI.begin。
    g_tft.initR(INITR_GREENTAB);

    g_tft.setRotation(LCD_ROTATION);
    g_tft.invertDisplay(true);
    g_tft.fillScreen(LCD_COLOR_BLACK);

    g_displayReady = true;
    g_currentState = nullptr;
    g_pendingState = nullptr;
    g_lastRefreshMs = 0;

    Serial.println("[display] ST7735S 160x80 ready @ 20 MHz");
}

void display_clear()
{
    if (!g_displayReady)
    {
        return;
    }
    g_tft.fillScreen(LCD_COLOR_BLACK);
    g_currentState = nullptr;
    g_pendingState = nullptr;
    g_lastRefreshMs = millis();
}

void display_show_text(const char* text)
{
    if (!g_displayReady || text == nullptr)
    {
        return;
    }

    g_tft.setTextColor(LCD_COLOR_WHITE);
    g_tft.setTextSize(1);

    const int16_t textW = strlen(text) * 6;
    const int16_t x = (160 - textW) / 2;
    const int16_t y = 60 + Y_OFFSET;  // 与 drawStateText 保持一致
    g_tft.fillRect(x - 2, y - 2, textW + 4, 14, LCD_COLOR_BLACK);
    g_tft.setCursor(x, y);
    g_tft.print(text);
}

void display_show_face(const char* expression)
{
    if (!g_displayReady)
    {
        return;
    }

    // 清除文字区，只重绘脸（文字区随 Y_OFFSET 一起下移）
    const int textClearY = 55 + Y_OFFSET;  // 63
    const int textClearH = 25 - Y_OFFSET;  // 17
    g_tft.fillRect(0, textClearY, 160, textClearH, LCD_COLOR_BLACK);
    drawFace(expression);
}

void display_set_state(const char* state)
{
    if (!g_displayReady || state == nullptr)
    {
        return;
    }

    // 先尝试绘制上一次排队中的 pending（若节流窗口已过期）。
    // 这样即便本次调用与 pending 相同，pending 也仍有机会真正绘制出来。
    display_pump();

    // 与当前已显示状态相同 → 完全 no-op（保留原有 strcmp 去重）
    if (g_currentState != nullptr && strcmp(g_currentState, state) == 0)
    {
        g_pendingState = nullptr;
        return;
    }

    // 用最新状态覆盖 pending（LISTENING→THINKING→SPEAKING 合并为 SPEAKING）
    g_pendingState = state;

    // 若距上次绘制足够久，立即绘制
    display_pump();
}
