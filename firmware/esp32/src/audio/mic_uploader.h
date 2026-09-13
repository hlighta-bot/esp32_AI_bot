#ifndef MIC_UPLOADER_H
#define MIC_UPLOADER_H

#include <Arduino.h>

#include "network/wifi_client.h"
#include "audio/mic_adc.h"
#include "vad/energy_vad.h"

class MicUploader
{
public:
    MicUploader();

    void begin(
        WifiClient *wifi,
        MicAdc *mic,
        EnergyVad *vad
    );

    void run();

    // RPTF 发送后，等待 PC 播放完成
    bool waitingForPlayback() const;

    // PC 的 PLAY 播放完成后调用
    void notifyPlaybackDone();

private:
    bool sendRecChunk(
        const int16_t *samples,
        size_t n,
        uint16_t flags
    );

    void sendReport(
        uint32_t totalBytes
    );

private:
    WifiClient *_wifi;
    MicAdc *_mic;
    EnergyVad *_vad;

    uint32_t _sessionBytes;

    // 当前语音 session 是否已经真正开始
    bool _sessionActive;

    // 当前 session 是否还没有发送过第一个 RECM
    bool _firstInSession;

    uint32_t _lastSendMs;

    // RPTF 已发送，等待 PC PLAY
    bool _waitingForPlayback;
};

#endif