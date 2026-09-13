// ============================================================
// energy_vad.h - 能量阈值 VAD（静音检测）
// ============================================================
//
// Phase 2 前的最简实现：基于窗内 RMS 能量阈值。
// Phase 3 可替换 WebRTC VAD / Silero VAD 无 API 变化。
//
// 用法：
//   EnergyVad vad;
//   vad.begin(/*rms_thresh*/3000, /*min_voice_ms*/200, /*silence_ms*/700);
//   vad.update(samples, n);
//   switch (vad.state()) {
//     case VAD_IDLE:    // 静默中，等待语音
//     case VAD_SPEAK:   // 有语音，正在积累
//     case VAD_END:     // 静音超时，本段结束
//   }
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
    void begin(uint32_t rmsThreshold,
               uint32_t minVoiceMs,
               uint32_t silenceMs);

    // 更新状态；每次调用传入一组 mono int16 样本
    void update(const int16_t *samples, size_t n, uint32_t sampleRate);

    VadState state() const { return _state; }

    // 触发时刻 / 静音超时时刻（毫秒，单调递增）
    uint32_t triggerMs() const  { return _triggerMs; }
    uint32_t silenceMs() const  { return _lastVoiceMs; }

private:
    uint32_t _rmsThreshold;
    uint32_t _minVoiceMs;
    uint32_t _silenceMs;

    VadState _state;
    uint32_t _triggerMs;
    uint32_t _lastVoiceMs;
};

#endif  // ENERGY_VAD_H
