// ============================================================
// mic_adc.cpp - MAX9814 ADC 采集实现
// ============================================================
//
// ADC API：
//   ESP32 Arduino 2.x / ESP-IDF 4.4
//
// 使用：
//   adc1_get_raw()
//
// 处理链：
//   ADC → 去偏置 → 低通 → 50kHz→16kHz → 增益 → Ring Buffer
// ============================================================

#include "mic_adc.h"


MicAdc::MicAdc()
    : _chan(ADC1_CHANNEL_0)
    , _outHead(0)
    , _outTail(0)
    , _outCount(0)
    , _phaseAcc(0)
    , _lpState(0)
    , _lastRaw(0)
    , _lastPollUs(0)
    , _started(false)
    , _gpio(-1)
{
}


void MicAdc::begin(int gpio)
{
    if (_started) {
        return;
    }

    // ========================================================
    // GPIO → ADC1 channel
    //
    // GPIO1  → CH0
    // GPIO2  → CH1
    // ...
    // GPIO10 → CH9
    // ========================================================

    if (gpio < 1 || gpio > 10) {
        Serial.printf(
            "[mic] invalid gpio %d for ADC1 (need 1..10)\n",
            gpio
        );
        return;
    }

    _chan = static_cast<adc1_channel_t>(gpio - 1);

    // ========================================================
    // ADC1 配置
    // ========================================================

    if (adc1_config_width(ADC_WIDTH_BIT_12) != ESP_OK) {
        Serial.println("[mic] adc1_config_width failed");
        return;
    }

    // ESP32-S3 ADC 衰减
    if (adc1_config_channel_atten(
            _chan,
            ADC_ATTEN_DB_12) != ESP_OK) {

        Serial.println(
            "[mic] adc1_config_channel_atten failed"
        );
        return;
    }

    _gpio = gpio;
    _lastPollUs = micros();
    _started = true;

    Serial.printf(
        "[mic] ready, gpio=%d chan=%d\n",
        gpio,
        static_cast<int>(_chan)
    );
}


void MicAdc::readRawBatch()
{
    // ========================================================
    // 50 kHz → 16 kHz
    //
    // phaseStep =
    //   65536 × 16000 / 50000
    //   ≈ 20971
    // ========================================================

    const uint32_t phaseStep = 20971;

    // ========================================================
    // 一阶低通
    // Q13
    // ========================================================

    const int32_t alpha = MIC_LP_ALPHA_FIXED;
    const int32_t oneMinus = 8192 - alpha;

    // ========================================================
    // 每批 100 个 ADC 样本
    //
    // 100 / 50000 = 2 ms
    // ========================================================

    for (int i = 0; i < 100; i++) {

        int raw = adc1_get_raw(_chan);

        if (raw < 0) {
            raw = 0;
        }

        if (raw > MIC_ADC_MAX_VALUE) {
            raw = MIC_ADC_MAX_VALUE;
        }

        // ----------------------------------------------------
        // 去 DC 偏置
        // ----------------------------------------------------

        _lastRaw =
            static_cast<int16_t>(
                raw - MIC_ADC_MID_VALUE
            );

        // ----------------------------------------------------
        // 一阶低通
        // ----------------------------------------------------

        int32_t acc =
            static_cast<int32_t>(_lastRaw) * alpha +
            _lpState * oneMinus;

        _lpState = acc >> 13;

        if (_lpState > 32767) {
            _lpState = 32767;
        }

        if (_lpState < -32768) {
            _lpState = -32768;
        }

        // ----------------------------------------------------
        // 50kHz → 16kHz
        // ----------------------------------------------------

        _phaseAcc += phaseStep;

        if (_phaseAcc >= 0x10000) {

            _phaseAcc -= 0x10000;

            // ------------------------------------------------
            // 软件增益
            // ------------------------------------------------

            int32_t amplified =
                _lpState << MIC_GAIN_SHIFT;

            if (amplified > 32767) {
                amplified = 32767;
            }

            if (amplified < -32768) {
                amplified = -32768;
            }

            const int16_t sample =
                static_cast<int16_t>(amplified);

            // ------------------------------------------------
            // Ring Buffer 写入
            // ------------------------------------------------

            _outRing[_outHead] = sample;

            _outHead++;

            if (_outHead >= MIC_OUT_RING_SAMPLES) {
                _outHead = 0;
            }

            if (_outCount < MIC_OUT_RING_SAMPLES) {

                // 缓冲区还没满
                _outCount++;

            } else {

                // 缓冲区已满：
                // 新数据覆盖最旧数据
                _outTail++;

                if (_outTail >= MIC_OUT_RING_SAMPLES) {
                    _outTail = 0;
                }
            }
        }
    }
}


void MicAdc::poll()
{
    if (!_started) {
        return;
    }

    const uint32_t now = micros();

    if (now - _lastPollUs < MIC_POLL_US) {
        return;
    }

    _lastPollUs = now;

    readRawBatch();
}


bool MicAdc::available() const
{
    return _outCount > 0;
}


size_t MicAdc::read(
    int16_t *buf,
    size_t max_samples)
{
    if (buf == nullptr ||
        max_samples == 0 ||
        _outCount == 0) {

        return 0;
    }

    size_t take =
        (_outCount < max_samples)
            ? _outCount
            : max_samples;

    // --------------------------------------------------------
    // 从最旧数据开始读取
    // --------------------------------------------------------

    size_t tail = _outTail;

    size_t space =
        MIC_OUT_RING_SAMPLES - tail;

    size_t first =
        (take < space)
            ? take
            : space;

    memcpy(
        buf,
        &_outRing[tail],
        first * sizeof(int16_t)
    );

    // --------------------------------------------------------
    // 如果跨越 Ring 尾部
    // --------------------------------------------------------

    if (take > first) {

        const size_t rest =
            take - first;

        memcpy(
            buf + first,
            &_outRing[0],
            rest * sizeof(int16_t)
        );
    }

    // --------------------------------------------------------
    // 更新读指针
    // --------------------------------------------------------

    tail += take;

    if (tail >= MIC_OUT_RING_SAMPLES) {
        tail -= MIC_OUT_RING_SAMPLES;
    }

    _outTail = tail;

    _outCount -= take;

    return take;
}