// ============================================================
// energy_vad.cpp - 能量阈值 VAD 实现
// ============================================================
//
// 算法：
//   RMS = sqrt(Σ x² / N)
//   触发：RMS ≥ threshold 且 语音持续时间 ≥ minVoiceMs → VAD_SPEAK
//   结束：连续 silenceMs 无有效语音 → VAD_END
//
// 数值稳定性：用 uint64 累加平方，避免 int32 溢出
// ============================================================

#include "energy_vad.h"

EnergyVad::EnergyVad()
    : _rmsThreshold(500)
    , _minVoiceMs(200)
    , _silenceMs(700)
    , _state(VAD_IDLE)
    , _triggerMs(0)
    , _lastVoiceMs(0)
{
}

void EnergyVad::begin(uint32_t rmsThreshold,
                      uint32_t minVoiceMs,
                      uint32_t silenceMs)
{
    _rmsThreshold = rmsThreshold;
    _minVoiceMs   = minVoiceMs;
    _silenceMs    = silenceMs;
    _state        = VAD_IDLE;
}

void EnergyVad::update(const int16_t *samples, size_t n, uint32_t sampleRate)
{
    if (n == 0) return;

    // AC RMS（去直流方差）：Σ(x-mean)²/n 再开方
    //
    // 实测依据（2026-09-13，mic-test + MAX9814 AR 悬空）：
    //   MAX9814 静默 DC ≈ 1.1V（raw≈1300），非理想 2048 中点。
    //   若直接用 Σx²/n 平方根，静默 DC 也算能量 → VAD 恒 SPEAK，
    //   永远无 VAD_END → 无 RPTF（服务器只见 VAD trigger 后连接被重置）。
    //   用方差可去除 DC：静默 ≈100~140，说话 ≈400~1800（均为 ×2 增益后）。
    int64_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        sum += samples[i];
    }
    int64_t meanSigned = sum / (int64_t)n;          // 带符号均值（去 DC 基准）

    uint64_t sumDiffSq = 0;
    for (size_t i = 0; i < n; i++) {
        int64_t d = (int64_t)samples[i] - meanSigned;
        sumDiffSq += (uint64_t)(d * d);
    }
    uint64_t var = sumDiffSq / n;                   // 方差（恒 ≥ 0，无需保护）

    // RMS = sqrt(var)，整数近似
    uint32_t rms = 0;
    {
        uint32_t lo = 0, hi = 32767;
        while (lo < hi) {
            uint32_t mid = (lo + hi + 1) / 2;
            if ((uint64_t)mid * (uint64_t)mid <= var) lo = mid; else hi = mid - 1;
        }
        rms = lo;
    }

    bool voiced = (rms >= _rmsThreshold);
    uint32_t now = millis();

    switch (_state) {
        case VAD_IDLE:
            if (voiced) {
                _triggerMs = now;
                _lastVoiceMs = now;
                _state = VAD_SPEAK;
            }
            break;

        case VAD_SPEAK:
            if (voiced) {
                _lastVoiceMs = now;
            } else if (now - _lastVoiceMs >= _silenceMs) {
                _state = VAD_END;
            }
            break;

        case VAD_END:
            // 复位到 IDLE，等待下一段
            _state = VAD_IDLE;
            break;
    }
}
