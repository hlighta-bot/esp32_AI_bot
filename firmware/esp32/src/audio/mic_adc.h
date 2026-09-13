// ============================================================
// mic_adc.h - MAX9814 模拟 MEMS 麦克风 → ESP32-S3 ADC
// ============================================================
//
// MAX9814 输出模拟电压（静态 ~VDD/2），走 ESP32-S3 ADC1。
//
// 当前 PlatformIO 使用：
//   Arduino-ESP32 2.0.x / ESP-IDF 4.4
//
// 因此使用传统 ADC API：
//   adc1_config_width()
//   adc1_config_channel_atten()
//   adc1_get_raw()
//
// 关键处理链：
//   1. ADC 约 50 kS/s，12-bit unsigned
//   2. 去直流偏置（减去 2048）
//   3. 一阶低通
//   4. 50 kHz → 16 kHz 重采样
//   5. 软件增益
//   6. Ring Buffer
//
// 输出：
//   int16 mono PCM
//   16000 Hz
// ============================================================

#ifndef MIC_ADC_H
#define MIC_ADC_H

#include <Arduino.h>
#include <driver/adc.h>

// ============================================================
// 采样率
// ============================================================

#define MIC_ADC_SAMPLE_HZ     50000
#define MIC_TARGET_SAMPLE_HZ  16000

// ============================================================
// ADC 硬件参数
// ============================================================

#define MIC_ADC_UNIT          ADC_UNIT_1
#define MIC_ADC_WIDTH_BITS    12

#define MIC_ADC_MID_VALUE     2048
#define MIC_ADC_MAX_VALUE     4095

// DC 偏置自动跟踪速度
#define MIC_DC_TRACK_SHIFT    8


// ============================================================
// Ring Buffer
// ============================================================

// 原始采样缓冲预留
#define MIC_RAW_RING_SAMPLES  256

// 输出 PCM Ring Buffer
#define MIC_OUT_RING_SAMPLES  4096

// ============================================================
// 增益与滤波
// ============================================================

// << 1 = ×2
#define MIC_GAIN_SHIFT        1

// 一阶低通滤波器，Q13
//
// y = α*x + (8192-α)*y_prev
//
// 6554 / 8192 ≈ 0.8
#define MIC_LP_ALPHA_FIXED    6554

// ============================================================
// poll 时序
// ============================================================

// 每 2 ms 读取一批 ADC
#define MIC_POLL_US           2000


class MicAdc {
public:

    // 构造函数
    MicAdc();

    // 初始化 ADC1
    //
    // ESP32-S3:
    //   GPIO1  → ADC1_CH0
    //   GPIO2  → ADC1_CH1
    //   ...
    //   GPIO10 → ADC1_CH9
    void begin(int gpio);

    // 主循环调用
    void poll();

    // 是否存在已经处理好的 PCM 样本
    bool available() const;

    // 批量读取 PCM
    size_t read(int16_t *buf, size_t max_samples);

    // 最近一次 ADC 去偏置后的值
    int16_t lastRaw() const
    {
        return _lastRaw;
    }

private:

    // 读取一批 ADC 原始数据
    void readRawBatch();

    // ========================================================
    // ADC
    // ========================================================

    // ESP32 Arduino 2.x 使用 adc1_channel_t
    adc1_channel_t _chan;         // ADC channel（0..8），与 gpio 对应
    // ========================================================
    // 输出 Ring Buffer
    // ========================================================

    int16_t _outRing[MIC_OUT_RING_SAMPLES];

    // 下一次写入位置
    size_t _outHead;

    // 最旧数据 / 下一次读取位置
    size_t _outTail;

    // 当前有效样本数量
    size_t _outCount;

    // ========================================================
    // 重采样
    // ========================================================

    uint32_t _phaseAcc;

    // DC 偏置估计
    int32_t _dcState;


    // 一阶低通状态
    int32_t _lpState;

    // 最近一次去偏置后的 ADC 值
    int16_t _lastRaw;

    // ========================================================
    // poll 时序
    // ========================================================

    uint32_t _lastPollUs;

    // 是否初始化成功
    bool _started;

    // 使用的 GPIO
    int _gpio;
};

#endif  // MIC_ADC_H

