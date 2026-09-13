// ============================================================
// energy_vad.h - 能量阈值 VAD（双阈值 + 最短语音时间）
// ============================================================
//
// 工作逻辑：
//
//   VAD_IDLE
//      │
//      │ RMS >= startThreshold
//      │ 持续 >= minVoiceMs
//      ▼
//   VAD_SPEAK
//      │
//      │ RMS < endThreshold
//      │ 持续 >= silenceMs
//      ▼
//   VAD_END
//
// 特点：
//   1. 启动和结束使用不同阈值，避免噪声导致 VAD 抖动。
//   2. 必须连续超过启动阈值一段时间，避免瞬时噪声触发。
//   3. RMS 使用 AC RMS，自动去除每个窗口的 DC 分量。
// ============================================================

#ifndef ENERGY_VAD_H
#define ENERGY_VAD_H

#include <Arduino.h>

enum VadState {
    VAD_IDLE  = 0,
    VAD_SPEAK = 1,
    VAD_END   = 2,
};

class EnergyVad {
public:
    EnergyVad();

    // 参数：
    //   rmsThreshold = 启动阈值
    //   minVoiceMs   = 连续超过启动阈值多少毫秒后才确认语音
    //   silenceMs    = 低于结束阈值多少毫秒后结束
    //
    // 当前实际使用：
    //   start threshold = rmsThreshold
    //   end threshold   = rmsThreshold * 0.6
    //
    // 例如：
    //   rmsThreshold = 500
    //   end threshold = 300
    void begin(uint32_t rmsThreshold,
               uint32_t minVoiceMs,
               uint32_t silenceMs);

    // 更新状态；每次调用传入一组 mono int16 样本
    void update(const int16_t *samples,
                size_t n,
                uint32_t sampleRate);

    // 完全复位 VAD 状态
    void reset();

    VadState state() const {
        return _state;
    }

    // 第一次检测到超过启动阈值的时刻
    uint32_t triggerMs() const {
        return _triggerMs;
    }

    // 最近一次有效语音时刻
    uint32_t silenceMs() const {
        return _lastVoiceMs;
    }

    // 最近一次计算出的 RMS，方便调试
    uint32_t rms() const {
        return _lastRms;
    }

    // 当前启动阈值
    uint32_t startThreshold() const {
        return _rmsThreshold;
    }

    // 当前结束阈值
    uint32_t endThreshold() const {
        return _endThreshold;
    }

private:
    uint32_t _rmsThreshold;   // 启动阈值
    uint32_t _endThreshold;   // 结束阈值
    uint32_t _minVoiceMs;     // 最短连续语音时间
    uint32_t _silenceMs;      // 连续静音时间

    VadState _state;

    // 第一次超过启动阈值的时间
    uint32_t _triggerMs;

    // 最近一次超过结束阈值的有效语音时间
    uint32_t _lastVoiceMs;

    // 最近一次 RMS
    uint32_t _lastRms;
};

#endif  // ENERGY_VAD_H