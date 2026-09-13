#include "mic_uploader.h"
#include "protocol/frame.h"

#define UPLOAD_ACCUM_SAMPLES 1600


MicUploader::MicUploader()
    : _wifi(nullptr)
    , _mic(nullptr)
    , _vad(nullptr)
    , _sessionBytes(0)
    , _sessionActive(false)
    , _firstInSession(true)
    , _lastSendMs(0)
    , _waitingForPlayback(false)
{
}


void MicUploader::begin(
    WifiClient *wifi,
    MicAdc *mic,
    EnergyVad *vad
)
{
    _wifi = wifi;
    _mic  = mic;
    _vad  = vad;

    _sessionBytes = 0;
    _sessionActive = false;
    _firstInSession = true;
    _waitingForPlayback = false;
}


bool MicUploader::waitingForPlayback() const
{
    return _waitingForPlayback;
}


void MicUploader::notifyPlaybackDone()
{
    /*
     * PC 已经把 PLAY 的全部 PCM 发完，
     * ESP32 也已经播放完毕。
     *
     * 现在才真正结束本次语音 session，
     * 允许下一次 VAD -> RECM。
     */

    _waitingForPlayback = false;

    _sessionBytes = 0;
    _sessionActive = false;
    _firstInSession = true;

    Serial.println(
        "[mic] playback done, ready for next session"
    );
}


bool MicUploader::sendRecChunk(
    const int16_t *samples,
    size_t n,
    uint16_t flags
)
{
    if (!_wifi || !_wifi->isConnected())
    {
        return false;
    }

    size_t chunkBytes =
        n * sizeof(int16_t);

    if (chunkBytes > FRAME_CHUNK_SIZE)
    {
        chunkBytes = FRAME_CHUNK_SIZE;
        n = chunkBytes / sizeof(int16_t);
    }

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
        (uint32_t)chunkBytes
    );


    if (
        _wifi->write(
            header,
            10
        ) != 10
    )
    {
        return false;
    }


    if (
        _wifi->write(
            (const uint8_t *)samples,
            chunkBytes
        ) != (int)chunkBytes
    )
    {
        return false;
    }


    _wifi->flush();

    return true;
}


void MicUploader::sendReport(
    uint32_t totalBytes
)
{
    if (!_wifi || !_wifi->isConnected())
    {
        Serial.println(
            "[mic] RPTF send failed: Wi-Fi disconnected"
        );

        return;
    }

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


    if (
        _wifi->write(
            header,
            8
        ) != 8
    )
    {
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


void MicUploader::run()
{
    if (!_wifi || !_mic || !_vad)
    {
        return;
    }


    if (!_wifi->isConnected())
    {
        return;
    }


    /*
     * =========================================================
     * 等待 PC PLAY
     * =========================================================
     *
     * RPTF 已经发送以后：
     *
     *     ESP32 ---> RPTF ---> PC
     *
     * PC 此时正在：
     *
     *     Whisper
     *       ↓
     *     LLM
     *       ↓
     *     TTS
     *       ↓
     *     PLAY
     *
     * 在 PLAY 完成以前，绝对不能开始下一轮 RECM。
     *
     * 否则 PC 在等待 PLAY ACK 时可能收到：
     *
     *     REC...
     *
     * 从而出现：
     *
     *     ACK mismatch: b'REC'
     */
    if (_waitingForPlayback)
    {
        return;
    }


    /*
     * ADC 采集
     */
    _mic->poll();


    if (!_mic->available())
    {
        return;
    }


    static int16_t samples[512];


    size_t n =
        _mic->read(
            samples,
            sizeof(samples) / sizeof(samples[0])
        );


    if (n == 0)
    {
        return;
    }


    /*
     * 更新 VAD
     */
    _vad->update(
        samples,
        n,
        MIC_TARGET_SAMPLE_HZ
    );


    VadState st =
        _vad->state();


    /*
     * 1600 samples = 100 ms @ 16 kHz
     */
    static int16_t uploadBuffer[UPLOAD_ACCUM_SAMPLES];

    static size_t uploadCount = 0;


    switch (st)
    {
        // =====================================================
        // 没有说话
        // =====================================================

        case VAD_IDLE:
        {
            /*
             * 只有在当前没有 session 时，
             * 才进行普通的 idle 清理。
             */
            if (!_sessionActive)
            {
                uploadCount = 0;
                _firstInSession = true;
                _sessionBytes = 0;
            }

            break;
        }


        // =====================================================
        // 正在说话
        // =====================================================

        case VAD_SPEAK:
        {
            /*
             * 第一次进入 SPEAK，
             * 标记本次 session 已经真正开始。
             */
            _sessionActive = true;


            size_t copyCount =
                n;


            if (
                copyCount >
                UPLOAD_ACCUM_SAMPLES - uploadCount
            )
            {
                copyCount =
                    UPLOAD_ACCUM_SAMPLES - uploadCount;
            }


            memcpy(
                uploadBuffer + uploadCount,
                samples,
                copyCount * sizeof(int16_t)
            );


            uploadCount +=
                copyCount;


            /*
             * 没有积累满 1600 samples，
             * 暂时不发送。
             */
            if (
                uploadCount <
                UPLOAD_ACCUM_SAMPLES
            )
            {
                return;
            }


            uint16_t flags = 0;


            if (_firstInSession)
            {
                flags |=
                    REC_FLAG_FIRST |
                    REC_FLAG_VAD_TRIGGER;

                _firstInSession = false;
            }


            Serial.printf(
                "[mic] SEND chunk=%u samples=%u bytes=%u\n",
                (unsigned)uploadCount,
                (unsigned)uploadCount,
                (unsigned)(
                    uploadCount * sizeof(int16_t)
                )
            );


            bool ok =
                sendRecChunk(
                    uploadBuffer,
                    uploadCount,
                    flags
                );


            if (ok)
            {
                _sessionBytes +=
                    uploadCount * sizeof(int16_t);


                Serial.printf(
                    "[mic] SENT total=%u B\n",
                    (unsigned)_sessionBytes
                );
            }
            else
            {
                Serial.printf(
                    "[mic] SEND FAILED samples=%u\n",
                    (unsigned)uploadCount
                );
            }


            uploadCount = 0;

            break;
        }


        // =====================================================
        // 说话结束
        // =====================================================

        case VAD_END:
        {
            /*
             * 防止一个已经结束的 session 在播放结束以后，
             * 因为 VAD 仍暂时处于 END 状态而再次发送 RPTF。
             */
            if (!_sessionActive)
            {
                uploadCount = 0;
                break;
            }


            /*
             * 发送最后不足 1600 samples 的尾部。
             */
            if (uploadCount > 0)
            {
                uint16_t flags = 0;


                if (_firstInSession)
                {
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
                        uploadCount * sizeof(int16_t)
                    )
                );


                bool ok =
                    sendRecChunk(
                        uploadBuffer,
                        uploadCount,
                        flags
                    );


                if (ok)
                {
                    _sessionBytes +=
                        uploadCount * sizeof(int16_t);


                    Serial.printf(
                        "[mic] SENT tail total=%u B\n",
                        (unsigned)_sessionBytes
                    );
                }
                else
                {
                    Serial.println(
                        "[mic] SEND tail FAILED"
                    );
                }


                uploadCount = 0;
            }


            /*
             * =================================================
             * 告诉 PC：
             *
             *     本次录音已经全部发送完毕
             *
             * PC 收到 RPTF 后开始：
             *
             *     Whisper
             *     LLM
             *     TTS
             *     PLAY
             */
            uint32_t total =
                _sessionBytes;


            sendReport(total);


            Serial.printf(
                "[mic] session end, total=%u B, waiting for PLAY\n",
                (unsigned)total
            );


            /*
             * =================================================
             * 进入 WAIT_PLAY
             *
             * 从这里开始禁止任何新的 RECM。
             * 必须等 main.cpp 中 playPCM() 完成后，
             * 调用 notifyPlaybackDone()。
             * =================================================
             */
            _waitingForPlayback = true;


            /*
             * 当前 session 已经发送结束。
             *
             * 注意：
             * 这里不能马上把 _sessionBytes 清零，
             * 也不能马上允许下一次 RECM。
             */
            _sessionActive = false;


            break;
        }


        default:
        {
            break;
        }
    }
}