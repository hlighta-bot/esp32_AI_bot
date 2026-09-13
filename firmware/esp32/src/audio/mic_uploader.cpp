// ============================================================
// mic_uploader.cpp
// ESP32 → PC 上行录音发送
// ============================================================
//
// 帧结构：
//
//   RECM:
//     [magic 4B]
//     [flags 2B]
//     [chunk_size 4B]
//     [pcm <= 4096B]
//
//   RPTF:
//     [magic 4B]
//     [total_size 4B]
//
// ============================================================

#include "mic_uploader.h"
#include "protocol/frame.h"


// ============================================================
// 内部录音缓存
//
// 16000 Hz × 2 bytes × 100 ms
// = 3200 bytes
//
// 1600 samples = 3200 bytes
// ============================================================

#define UPLOAD_ACCUM_SAMPLES 1600


// ============================================================
// 构造函数
// ============================================================

MicUploader::MicUploader()
    : _wifi(nullptr)
    , _mic(nullptr)
    , _vad(nullptr)
    , _sessionBytes(0)
    , _sessionActive(false)
    , _firstInSession(true)
    , _lastSendMs(0)
    , _waitingForPlayback(false)
    , _cooldownUntilMs(0)
{
}


// ============================================================
// begin
// ============================================================

void MicUploader::begin(
    WifiClient *wifi,
    MicAdc *mic,
    EnergyVad *vad)
{
    _wifi = wifi;
    _mic  = mic;
    _vad  = vad;

    _sessionBytes = 0;

    _sessionActive = false;

    _firstInSession = true;

    _lastSendMs = 0;

    _waitingForPlayback = false;

    _cooldownUntilMs = 0;

    // --------------------------------------------------------
    // 初始化时也清一次麦克风 Ring Buffer
    // --------------------------------------------------------

    if (_mic) {
        _mic->clear();
    }
}


// ============================================================
// waitingForPlayback
// ============================================================

bool MicUploader::waitingForPlayback() const
{
    return _waitingForPlayback;
}


// ============================================================
// notifyPlaybackDone
//
// PC 已经把 PLAY 数据发送完，ESP32 也已经播放完成。
//
// 注意：
//
// 这里不能直接恢复 VAD。
//
// 因为：
//
//   playPCM() 返回
//       ↓
//   喇叭仍然有余音
//       ↓
//   房间还有反射声
//       ↓
//   麦克风仍可能检测到高能量
//
// 所以进入 cooldown。
// ============================================================

void MicUploader::notifyPlaybackDone()
{
    _waitingForPlayback = false;

    _sessionBytes = 0;

    _sessionActive = false;

    _firstInSession = true;

    // --------------------------------------------------------
    // 从现在开始计时
    // --------------------------------------------------------

    _cooldownUntilMs =
        millis() + MIC_PLAYBACK_COOLDOWN_MS;

    Serial.printf(
        "[mic] playback done, cooldown %u ms\n",
        (unsigned)MIC_PLAYBACK_COOLDOWN_MS
    );
}


// ============================================================
// discardMicSamples
//
// 将当前 ADC → PCM Ring Buffer 中的数据全部丢掉。
//
// 这里不做 VAD。
// 这些数据可能包含：
//
//   - 喇叭声音
//   - 房间反射
//   - 播放尾音
//
// 都不能进入下一次用户录音。
// ============================================================

void MicUploader::discardMicSamples()
{
    if (!_mic) {
        return;
    }

    // --------------------------------------------------------
    // 先继续 ADC 采样
    //
    // 这样不会因为停止读取而让 Ring Buffer 堵满。
    // --------------------------------------------------------

    _mic->poll();

    // --------------------------------------------------------
    // 直接清空当前 PCM Ring Buffer
    // --------------------------------------------------------

    _mic->clear();
}


// ============================================================
// sendRecChunk
// ============================================================

bool MicUploader::sendRecChunk(
    const int16_t *samples,
    size_t n,
    uint16_t flags)
{
    if (!_wifi ||
        !_wifi->isConnected()) {

        return false;
    }

    // --------------------------------------------------------
    // PCM 字节数
    // --------------------------------------------------------

    size_t chunkBytes =
        n * sizeof(int16_t);

    // --------------------------------------------------------
    // 限制最大 frame
    // --------------------------------------------------------

    if (chunkBytes > FRAME_CHUNK_SIZE) {

        chunkBytes =
            FRAME_CHUNK_SIZE;

        n =
            chunkBytes / sizeof(int16_t);
    }

    // --------------------------------------------------------
    // REC header
    //
    //   4B magic
    //   2B flags
    //   4B chunk size
    // --------------------------------------------------------

    uint8_t header[10];

    memcpy(
        header,
        PROTO_REC,
        4
    );

    packU16(
        header + 4,
        flags
    );

    packU32(
        header + 6,
        static_cast<uint32_t>(chunkBytes)
    );

    // --------------------------------------------------------
    // header
    // --------------------------------------------------------

    if (_wifi->write(header, 10) != 10) {
        return false;
    }

    // --------------------------------------------------------
    // PCM
    // --------------------------------------------------------

    if (_wifi->write(
            (const uint8_t *)samples,
            chunkBytes) != (int)chunkBytes) {

        return false;
    }

    // --------------------------------------------------------
    // 确保 TX 数据发出
    // --------------------------------------------------------

    _wifi->flush();

    return true;
}


// ============================================================
// sendReport
// ============================================================

void MicUploader::sendReport(
    uint32_t totalBytes)
{
    if (!_wifi ||
        !_wifi->isConnected()) {

        Serial.println(
            "[mic] RPTF send failed: Wi-Fi disconnected"
        );

        return;
    }

    // --------------------------------------------------------
    // RPTF:
    //
//   4B magic
//   4B total PCM bytes
// --------------------------------------------------------

    uint8_t header[8];

    memcpy(
        header,
        PROTO_RPTF,
        4
    );

    packU32(
        header + 4,
        totalBytes
    );

    if (_wifi->write(header, 8) != 8) {

        Serial.println(
            "[mic] RPTF send failed"
        );

        return;
    }

    _wifi->flush();

    Serial.printf(
        "[mic] RPTF total=%u B\n",
        (unsigned)totalBytes
    );
}


// ============================================================
// run
// ============================================================

void MicUploader::run()
{
    if (!_wifi ||
        !_mic ||
        !_vad) {

        return;
    }

    if (!_wifi->isConnected()) {
        return;
    }


    // ========================================================
    // 状态 1：等待 PC 播放
    //
    // 已经发送 RPTF。
    //
    // 现在 PC 正在：
    //
    //   Whisper
    //   ↓
    //   LLM
    //   ↓
    //   TTS
    //   ↓
    //   PLAY
    //
    // 这期间绝对不能让麦克风数据进入 VAD。
    // ========================================================

    if (_waitingForPlayback) {

        discardMicSamples();

        return;
    }


    // ========================================================
    // 状态 2：播放结束后的 cooldown
    // ========================================================

    if (_cooldownUntilMs != 0) {

        // ----------------------------------------------------
        // 继续丢弃麦克风数据
        // ----------------------------------------------------

        discardMicSamples();

        // ----------------------------------------------------
        // cooldown 是否结束
        // ----------------------------------------------------

        const uint32_t now =
            millis();

        if ((int32_t)(
                now - _cooldownUntilMs) < 0) {

            // 还在 cooldown
            return;
        }

        // ----------------------------------------------------
        // cooldown 完成
        // ----------------------------------------------------

        _cooldownUntilMs = 0;

        // ----------------------------------------------------
        // 最后再清一次 Ring Buffer
        //
        // 防止 cooldown 刚好结束时残留一批数据。
        // ----------------------------------------------------

        _mic->clear();

        // ----------------------------------------------------
        // 清除当前 VAD 状态
        //
        // EnergyVad 没有公开 reset() 接口时，
        // 这里通过“重新初始化对象”的方式会比较危险，
        // 因此不在这里修改 VAD 对象。
        //
        // 当前 VAD 在 cooldown 期间没有收到任何 sample，
        // 所以它会保持之前的状态。
        //
        // 下一轮实际 sample 到来后，
        // 正常 update() 会重新进入判断。
        // ----------------------------------------------------

        Serial.println(
            "[mic] cooldown done, listening again"
        );

        return;
    }


    // ========================================================
    // 正常录音状态
    // ========================================================

    _mic->poll();

    if (!_mic->available()) {
        return;
    }


    // ========================================================
    // 从麦克风 Ring Buffer 读取 PCM
    // ========================================================

    static int16_t samples[512];

    size_t n =
        _mic->read(
            samples,
            sizeof(samples) /
            sizeof(samples[0])
        );

    if (n == 0) {
        return;
    }


    // ========================================================
    // VAD
    // ========================================================

    _vad->update(
        samples,
        n,
        MIC_TARGET_SAMPLE_HZ
    );

    VadState st =
        _vad->state();


    // ========================================================
    // 上传 session 的临时缓存
    //
    // 1600 samples = 100 ms
    // ========================================================

    static int16_t uploadBuffer[
        UPLOAD_ACCUM_SAMPLES
    ];

    static size_t uploadCount = 0;


    // ========================================================
    // VAD 状态机
    // ========================================================

    switch (st)
    {

        // ====================================================
        // IDLE
        // ====================================================

        case VAD_IDLE:
        {
            if (!_sessionActive) {

                uploadCount = 0;

                _firstInSession = true;

                _sessionBytes = 0;
            }

            break;
        }


        // ====================================================
        // SPEAK
        // ====================================================

        case VAD_SPEAK:
        {
            // ------------------------------------------------
            // 新 session
            // ------------------------------------------------

            _sessionActive = true;


            // ------------------------------------------------
            // 放入上传缓存
            // ------------------------------------------------

            size_t copyCount = n;

            if (copyCount >
                UPLOAD_ACCUM_SAMPLES -
                uploadCount) {

                copyCount =
                    UPLOAD_ACCUM_SAMPLES -
                    uploadCount;
            }

            memcpy(
                uploadBuffer + uploadCount,
                samples,
                copyCount *
                sizeof(int16_t)
            );

            uploadCount += copyCount;


            // ------------------------------------------------
            // 不足一个 chunk
            // ------------------------------------------------

            if (uploadCount <
                UPLOAD_ACCUM_SAMPLES) {

                return;
            }


            // ------------------------------------------------
            // FIRST flag
            // ------------------------------------------------

            uint16_t flags = 0;

            if (_firstInSession) {

                flags |=
                    REC_FLAG_FIRST |
                    REC_FLAG_VAD_TRIGGER;

                _firstInSession = false;
            }


            // ------------------------------------------------
            // 发送 chunk
            // ------------------------------------------------

            Serial.printf(
                "[mic] SEND chunk=%u samples=%u bytes=%u\n",
                (unsigned)uploadCount,
                (unsigned)uploadCount,
                (unsigned)(
                    uploadCount *
                    sizeof(int16_t)
                )
            );


            bool ok =
                sendRecChunk(
                    uploadBuffer,
                    uploadCount,
                    flags
                );


            // ------------------------------------------------
            // 更新 session 字节数
            // ------------------------------------------------

            if (ok) {

                _sessionBytes +=
                    uploadCount *
                    sizeof(int16_t);

                Serial.printf(
                    "[mic] SENT total=%u B\n",
                    (unsigned)_sessionBytes
                );

            } else {

                Serial.printf(
                    "[mic] SEND FAILED samples=%u\n",
                    (unsigned)uploadCount
                );
            }


            // ------------------------------------------------
            // 清空临时 chunk
            // ------------------------------------------------

            uploadCount = 0;

            break;
        }


        // ====================================================
        // END
        // ====================================================

        case VAD_END:
        {
            // ------------------------------------------------
            // 没有 active session
            // ------------------------------------------------

            if (!_sessionActive) {

                uploadCount = 0;

                break;
            }


            // ------------------------------------------------
            // 发送最后不足 1600 samples 的尾部
            // ------------------------------------------------

            if (uploadCount > 0) {

                uint16_t flags = 0;

                if (_firstInSession) {

                    flags |=
                        REC_FLAG_FIRST |
                        REC_FLAG_VAD_TRIGGER;

                    _firstInSession = false;
                }


                Serial.printf(
                    "[mic] SEND tail=%u samples=%u bytes=%u\n",
                    (unsigned)uploadCount,
                    (unsigned)uploadCount,
                    (unsigned)(
                        uploadCount *
                        sizeof(int16_t)
                    )
                );


                bool ok =
                    sendRecChunk(
                        uploadBuffer,
                        uploadCount,
                        flags
                    );


                if (ok) {

                    _sessionBytes +=
                        uploadCount *
                        sizeof(int16_t);

                    Serial.printf(
                        "[mic] SENT tail total=%u B\n",
                        (unsigned)_sessionBytes
                    );

                } else {

                    Serial.println(
                        "[mic] SEND tail FAILED"
                    );
                }


                uploadCount = 0;
            }


            // ------------------------------------------------
            // 保存本次 session 总大小
            // ------------------------------------------------

            uint32_t total =
                _sessionBytes;


            // ------------------------------------------------
            // 告诉 PC：
            //
            // 本次录音结束
            // ------------------------------------------------

            sendReport(total);


            Serial.printf(
                "[mic] session end, total=%u B, waiting for PLAY\n",
                (unsigned)total
            );


            // ------------------------------------------------
            // 从现在开始：
            //
            //   不允许下一次 VAD
            //   不允许下一次 REC
            //
            // 等 PC 返回 PLAY。
            // ------------------------------------------------

            _waitingForPlayback = true;

            _sessionActive = false;

            break;
        }


        // ====================================================
        // 默认
        // ====================================================

        default:
            break;
    }
}
