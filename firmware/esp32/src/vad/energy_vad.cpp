// ============================================================
// energy_vad.cpp - 能量阈值 VAD 实现
// ============================================================
//
// 算法：
//
//   1. 对当前窗口计算 AC RMS：
//        RMS = sqrt(Σ(x - mean)^2 / N)
//
//   2. VAD_IDLE：
//        RMS >= startThreshold
//        ↓
//        开始计时
//        ↓
//        连续达到 minVoiceMs
//        ↓
//        VAD_SPEAK
//
//   3. VAD_SPEAK：
//        RMS >= endThreshold
//        ↓
//        认为仍然有声音
//
//        RMS < endThreshold
//        ↓
//        开始静音计时
//        ↓
//        连续达到 silenceMs
//        ↓
//        VAD_END
//
// 双阈值：
//   startThreshold = 500
//   endThreshold   = 300
//
// 这样可以避免：
//   静音 RMS ≈ 300
//   偶尔噪声 > 400
//   ↓
//   立即触发 VAD
//
// 数值稳定性：
//   使用 uint64_t 累加平方，避免 int32 溢出。
// ============================================================

#include "energy_vad.h"

EnergyVad::EnergyVad()
    : _rmsThreshold(500)
    , _endThreshold(300)
    , _minVoiceMs(200)
    , _silenceMs(700)
    , _state(VAD_IDLE)
    , _triggerMs(0)
    , _lastVoiceMs(0)
    , _lastRms(0)
{
}

void EnergyVad::begin(uint32_t rmsThreshold,
                      uint32_t minVoiceMs,
                      uint32_t silenceMs)
{
    _rmsThreshold = rmsThreshold;
    _minVoiceMs   = minVoiceMs;
    _silenceMs    = silenceMs;

    // 当前麦克风实测：
    //
    // 静音 RMS ≈ 290~350
    // 普通说话 RMS ≈ 700~1000
    //
    // 因此结束阈值采用启动阈值的 60%。
    //
    // 默认：
    //   500 * 0.6 = 300
    //
    // 同时保证至少为 1。
    _endThreshold = (_rmsThreshold * 6) / 10;

    if (_endThreshold < 1) {
        _endThreshold = 1;
    }

    reset();
}

void EnergyVad::reset()
{
    _state = VAD_IDLE;

    _triggerMs = 0;
    _lastVoiceMs = 0;
    _lastRms = 0;
}

void EnergyVad::update(const int16_t *samples,
                       size_t n,
                       uint32_t sampleRate)
{
    if (samples == nullptr || n == 0) {
        return;
    }

    // sampleRate 当前不直接参与 RMS 计算。
    // 保留这个参数是为了以后可以根据采样率计算
    // 精确的窗口时间。
    (void)sampleRate;

    // ========================================================
    // 第一步：计算窗口平均值
    // ========================================================

    int64_t sum = 0;

    for (size_t i = 0; i < n; i++) {
        sum += samples[i];
    }

    int64_t meanSigned = sum / (int64_t)n;

    // ========================================================
    // 第二步：计算 AC RMS
    //
    // RMS = sqrt(Σ(x - mean)^2 / N)
    // ========================================================

    uint64_t sumDiffSq = 0;

    for (size_t i = 0; i < n; i++) {

        int64_t d =
            (int64_t)samples[i] - meanSigned;

        sumDiffSq += (uint64_t)(d * d);
    }

    uint64_t var =
        sumDiffSq / n;

    // ========================================================
    // 第三步：整数平方根
    // ========================================================

    uint32_t rms = 0;

    {
        uint32_t lo = 0;
        uint32_t hi = 32767;

        while (lo < hi) {

            uint32_t mid =
                (lo + hi + 1) / 2;

            if ((uint64_t)mid * (uint64_t)mid <= var) {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }

        rms = lo;
    }

    _lastRms = rms;

    const uint32_t now = millis();

    const bool aboveStart =
        (rms >= _rmsThreshold);

    const bool aboveEnd =
        (rms >= _endThreshold);

    // ========================================================
    // 状态机
    // ========================================================

    switch (_state) {

        // ----------------------------------------------------
        // IDLE
        // ----------------------------------------------------
        case VAD_IDLE:

            if (aboveStart) {

                // 第一次超过启动阈值
                if (_triggerMs == 0) {

                    _triggerMs = now;

                    Serial.printf(
                        "[vad] candidate start rms=%u threshold=%u\n",
                        (unsigned)rms,
                        (unsigned)_rmsThreshold
                    );
                }

                // 必须连续达到 minVoiceMs
                if (now - _triggerMs >= _minVoiceMs) {

                    _state = VAD_SPEAK;

                    _lastVoiceMs = now;

                    Serial.printf(
                        "[vad] SPEAK rms=%u start=%u end=%u\n",
                        (unsigned)rms,
                        (unsigned)_rmsThreshold,
                        (unsigned)_endThreshold
                    );
                }

            } else {

                // 没有持续超过启动阈值
                // 取消这次候选触发
                _triggerMs = 0;
            }

            break;


        // ----------------------------------------------------
        // SPEAK
        // ----------------------------------------------------
        case VAD_SPEAK:

            if (aboveEnd) {

                // 只要 RMS 还高于结束阈值，
                // 就认为仍然有有效声音。
                _lastVoiceMs = now;

            } else {

                // 低于结束阈值。
                //
                // 如果持续 silenceMs，
                // 则认为本次语音结束。
                if (now - _lastVoiceMs >= _silenceMs) {

                    _state = VAD_END;

                    Serial.printf(
                        "[vad] END rms=%u silence=%ums\n",
                        (unsigned)rms,
                        (unsigned)_silenceMs
                    );
                }
            }

            break;


        // ----------------------------------------------------
        // END
        // ----------------------------------------------------
        case VAD_END:

            // 交给上层处理 VAD_END。
            //
            // 下一次 update() 再自动回到 IDLE。
            _state = VAD_IDLE;

            _triggerMs = 0;
            _lastVoiceMs = 0;

            break;
    }
}