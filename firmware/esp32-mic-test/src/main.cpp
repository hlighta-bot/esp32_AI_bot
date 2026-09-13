// ============================================================================
// MAX9814 麦克风独立测试固件
//
// 用途：
//   只跑 MAX9814 ADC 采集，验证麦克风接线与信号是否正常。
//   对着麦克风说话，串口 monitor 里 raw / rms / bar 都会波动。
//
// 接线（MAX9814 5-pin breakout）：
//   GND  → ESP32-S3 GND
//   VDD  → ESP32-S3 3V3
//   Out  → ESP32-S3 GPIO1   (ADC1_CH0)
//   GAIN → 悬空   (+50 dB)
//   AR   → 悬空   (DC-coupled, 静默 ≈ 1.65 V)
//   建议在 VDD↔GND 之间接 100 nF 电容
//
// 串口输出（921600 baud）：
//   每 100 ms 一行：
//     [mic] raw=2048  dc=   0  rms=  12  | ########
//   静默时 raw≈2048, dc≈0, rms 小；有声音时 rms 明显变大，bar 变长。
//
// 依赖：无（不用 Wi-Fi、无 lib_deps，最轻量）
// ============================================================================

#include <Arduino.h>

// -------------------- 参数 --------------------
#define MIC_GPIO          1        // MAX9814 Out → GPIO1 (ADC1_CH0)
#define ADC_MID_VALUE     2048     // ESP32 ADC12 静默中点 (0x1000)
#define SAMPLES_PER_MS    10       // 每 1 ms 采 10 次（10 kHz，够判有/无声音）
#define REPORT_PERIOD_MS  100      // 每 100 ms 上报一次
#define BAR_WIDTH         40       // 可视化条最大长度
// AC RMS → bar 归一化系数：实测静默 rms≈50、说话 rms≈400+
// 500 时静默 ≈4 格、说话 ≈32+ 格，对比明显（60 时静默即 35 格无区分）
#define RMS_DB_SCALE      500

// -------------------- 全局 --------------------
static unsigned long lastReportMs = 0;
static uint16_t maxRaw = 0;
static uint16_t minRaw = 4095;     // 初始为最大 ADC 值，避免漏记真实最小值
static float sumRaw = 0.0f;        // 窗口内 raw 累加 → 求均值（去直流）
static float sumSqRaw = 0.0f;      // 窗口内 raw² 累加 → 求 AC 方差
static unsigned int sampleCount = 0;

// -------------------- 主逻辑 --------------------
void setup()
{
    // CH340 USB-UART 模式（CDC_ON_BOOT=0）：Serial = Serial0 = UART0 = CH340 = ttyACM0
    Serial.begin(921600);
    delay(500);  // 等 USB 枚举稳定

    // --- BOOT 标志：确认新固件真的在跑 ---
    Serial.println();
    Serial.println("========================================");
    Serial.println("  MIC-TEST BOOT v1.3");
    Serial.println("  If you see this banner, USB is OK");
    Serial.println("========================================");
    Serial.printf("millis        : %lu\n", (unsigned long)millis());
    Serial.printf("chip revision : %u\n", (unsigned)ESP.getChipRevision());
    Serial.printf("heap free     : %u\n", (unsigned)ESP.getFreeHeap());
    Serial.println("========================================");
    Serial.flush();
    delay(200);

    Serial.println("==========================================");
    Serial.println("  MAX9814 Mic Test");
    Serial.println("==========================================");
    Serial.printf("ADC pin        : GPIO%d (ADC1_CH0)\n", MIC_GPIO);
    Serial.printf("ADC range      : 12-bit (0..%d)\n", 4095);
    Serial.printf("Expected silent: raw ~= %d (dc ~= 0)\n", ADC_MID_VALUE);
    Serial.printf("Sample rate    : %d Hz\n", SAMPLES_PER_MS * 1000);
    Serial.printf("Report period  : %d ms\n", REPORT_PERIOD_MS);
    Serial.println("Speaking into mic should make 'rms' rise and bar grow.");
    Serial.println("------------------------------------------");

    // ESP32-S3 ADC1 默认无需 enable；analogRead 直接用
    analogReadResolution(12);   // 12 bit
    analogSetAttenuation(ADC_11db);  // ESP32-S3: ADC_11db（≈ 0..3.3 V 满量程，12-bit）

    lastReportMs = millis();
    maxRaw = 0;
    minRaw = 4095;
    sumRaw = 0.0f;
    sumSqRaw = 0.0f;
    sampleCount = 0;
}

void loop()
{
    // 每个 loop 采 SAMPLES_PER_MS 个样本，累计 AC 统计量与 min/max
    for (int i = 0; i < SAMPLES_PER_MS; i++)
    {
        int raw = analogRead(MIC_GPIO);
        if (raw > maxRaw) maxRaw = (uint16_t)raw;
        if (raw < minRaw) minRaw = (uint16_t)raw;

        sumRaw += (float)raw;
        sumSqRaw += (float)raw * (float)raw;
        sampleCount++;
    }

    // 每 REPORT_PERIOD_MS 上报一次
    if (millis() - lastReportMs >= REPORT_PERIOD_MS)
    {
        int latest = analogRead(MIC_GPIO);
        int latestDc = latest - ADC_MID_VALUE;
        // 去直流 AC RMS = 窗口内均方差开方：恒定直流电平（如接 GND/3V3）rms≈0
        float mean = sampleCount > 0 ? (sumRaw / (float)sampleCount) : 0.0f;
        float variance = sampleCount > 0 ? (sumSqRaw / (float)sampleCount) - mean * mean : 0.0f;
        if (variance < 0.0f) variance = 0.0f;   // 浮点舍入保护
        float rms = sqrtf(variance);

        // 可视化 bar：rms 归一到 [0, BAR_WIDTH]
        int barLen = (int)(rms * BAR_WIDTH / RMS_DB_SCALE);
        if (barLen > BAR_WIDTH) barLen = BAR_WIDTH;
        if (barLen < 0) barLen = 0;

        Serial.printf("[mic] raw=%4d  dc=%+5d  rms=%6.1f  min=%4d max=%4d | ",
                      latest, latestDc, rms, minRaw, maxRaw);
        for (int i = 0; i < BAR_WIDTH; i++)
        {
            Serial.print(i < barLen ? '#' : '.');
        }
        Serial.println();

        // 重置窗口
        lastReportMs = millis();
        maxRaw = 0;
        minRaw = 4095;
        sumRaw = 0.0f;
        sumSqRaw = 0.0f;
        sampleCount = 0;
    }
}
