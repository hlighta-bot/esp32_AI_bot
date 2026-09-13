#include <Arduino.h>
#include <driver/i2s.h>

#ifdef FIRMWARE_MODE_WIFI
#include "network/wifi_client.h"
#include "audio/mic_adc.h"
#include "audio/mic_uploader.h"
#include "vad/energy_vad.h"
#endif

#include "protocol/frame.h"


#ifdef FIRMWARE_MODE_WIFI

// ============================================================
// 麦克风
// ============================================================

// MAX9814 麦克风 GPIO
// GPIO1 = ADC1_CH0
#define MIC_GPIO              1


// ============================================================
// VAD
// ============================================================

// 实测依据（2026-09-13，firmware/esp32-mic-test + MAX9814）
//
// 静默：原始 ADC 去偏置后 rms ≈ 50~70
// ×2 软件增益后 ≈ 100~140
//
// 说话：rms ≈ 200~900
//
// 旧值 2500 远超实际说话幅度，VAD 很难触发。
// 当前使用 400。
#define VAD_RMS_THRESHOLD     400
#define VAD_MIN_VOICE_MS      200
#define VAD_SILENCE_MS        700

#endif


// ============================================================
// I2S
// ============================================================

#define I2S_PORT I2S_NUM_1

#define I2S_BCLK 16
#define I2S_LRC  17
#define I2S_DIN  15


// ============================================================
// Audio
// ============================================================

#define SAMPLE_RATE       16000
#define BITS_PER_SAMPLE   16


// ============================================================
// Serial
// ============================================================

#define SERIAL_BAUD 921600


// ============================================================
// 数据块
// ============================================================

#define PCM_CHUNK_SIZE 4096

uint8_t pcmBuffer[PCM_CHUNK_SIZE];


// Mono -> Stereo
//
// PCM 输入：
//
//     L R
//
// 实际发送给 MAX98357A：
//
//     L L
//     R R
//
// 因为 MAX98357A 当前按 stereo 接收，
// 所以把 mono 复制到左右声道。
int16_t stereoBuffer[PCM_CHUNK_SIZE];


// ============================================================
// 全局对象
// ============================================================

#ifdef FIRMWARE_MODE_WIFI

static WifiClient   g_wifi;
static MicAdc       g_mic;
static EnergyVad    g_vad;
static MicUploader  g_uploader;

#endif


// ============================================================
// I2S 初始化
// ============================================================

void setupI2S()
{
    i2s_config_t config =
    {
        .mode =
            (i2s_mode_t)(
                I2S_MODE_MASTER |
                I2S_MODE_TX
            ),

        .sample_rate =
            SAMPLE_RATE,

        .bits_per_sample =
            I2S_BITS_PER_SAMPLE_16BIT,

        .channel_format =
            I2S_CHANNEL_FMT_RIGHT_LEFT,

        .communication_format =
            I2S_COMM_FORMAT_STAND_I2S,

        .intr_alloc_flags =
            ESP_INTR_FLAG_LEVEL1,

        .dma_buf_count =
            8,

        .dma_buf_len =
            256,

        .use_apll =
            false,

        .tx_desc_auto_clear =
            true,

        .fixed_mclk =
            0
    };


    i2s_pin_config_t pins =
    {
        .bck_io_num =
            I2S_BCLK,

        .ws_io_num =
            I2S_LRC,

        .data_out_num =
            I2S_DIN,

        .data_in_num =
            I2S_PIN_NO_CHANGE
    };


    esp_err_t result;


    result =
        i2s_driver_install(
            I2S_PORT,
            &config,
            0,
            NULL
        );


    if (result != ESP_OK)
    {
        Serial.printf(
            "I2S driver failed: %d\n",
            result
        );


        while (true)
        {
            delay(1000);
        }
    }


    result =
        i2s_set_pin(
            I2S_PORT,
            &pins
        );


    if (result != ESP_OK)
    {
        Serial.printf(
            "I2S pin failed: %d\n",
            result
        );


        while (true)
        {
            delay(1000);
        }
    }


    i2s_zero_dma_buffer(
        I2S_PORT
    );
}


// ============================================================
// 接收指定数量的数据
//
// Wi-Fi：
// TCP 可能出现半包，因此必须循环读满 size。
//
// Serial：
// 保持原来的阻塞读取语义。
// ============================================================

bool receiveBytes(
    uint8_t *buffer,
    size_t size
)
{
    size_t received = 0;


    while (received < size)
    {

#ifdef FIRMWARE_MODE_WIFI

        int n =
            g_wifi.read(
                buffer + received,
                size - received
            );


        if (n < 0)
        {
            return false;
        }


        if (n == 0)
        {
            /*
             * TCP 暂时没有数据。
             *
             * 注意：
             * 这个函数只应该在已经确认有 PLAY
             * 数据到来的情况下调用。
             */
            continue;
        }


        received +=
            (size_t)n;

#else

        size_t n =
            Serial.readBytes(
                buffer + received,
                size - received
            );


        if (n > 0)
        {
            received += n;
        }

#endif
    }


    return true;
}


// ============================================================
// 接收 uint32 little endian
// ============================================================

uint32_t receiveUint32()
{
    uint8_t b[4];


    if (!receiveBytes(b, 4))
    {
        return 0;
    }


    return unpackU32(b);
}


// ============================================================
// 发送 ACK
//
// PC 每发送一个 PCM chunk 后等待一个 ACK。
// ============================================================

void sendAck()
{
#ifdef FIRMWARE_MODE_WIFI

    g_wifi.write(
        (const uint8_t *)PROTO_ACK,
        3
    );

    g_wifi.flush();

#else

    Serial.write('A');
    Serial.write('C');
    Serial.write('K');
    Serial.flush();

#endif
}


// ============================================================
// 播放一个 PCM 数据块
// ============================================================

void playChunk(
    uint8_t *data,
    size_t bytes
)
{
    int16_t *mono =
        (int16_t *)data;


    size_t samples =
        bytes / 2;


    // ========================================================
    // Mono -> Stereo
    // ========================================================

    for (
        size_t i = 0;
        i < samples;
        i++
    )
    {
        stereoBuffer[i * 2] =
            mono[i];

        stereoBuffer[i * 2 + 1] =
            mono[i];
    }


    size_t bytesToWrite =
        samples * 4;


    size_t bytesWritten;


    i2s_write(
        I2S_PORT,
        stereoBuffer,
        bytesToWrite,
        &bytesWritten,
        portMAX_DELAY
    );
}


// ============================================================
// 播放完整 PCM
//
// PC：
//
//     PLAY + uint32 size
//     ↓
//     PCM chunk
//     ↓
//     ACK
//     ↓
//     PCM chunk
//     ↓
//     ACK
//     ↓
//     ...
//
// ESP32：
//
//     receive chunk
//     ↓
//     play
//     ↓
//     ACK
// ============================================================

void playPCM(
    uint32_t dataSize
)
{
    uint32_t remaining =
        dataSize;


    while (remaining > 0)
    {
        size_t chunkSize =
            min(
                (uint32_t)PCM_CHUNK_SIZE,
                remaining
            );


        // ====================================================
        // 16 bit PCM 必须保证偶数字节
        // ====================================================

        if (chunkSize & 1)
        {
            chunkSize--;
        }


        if (chunkSize == 0)
        {
            break;
        }


        // ====================================================
        // 接收 PCM
        // ====================================================

        if (
            !receiveBytes(
                pcmBuffer,
                chunkSize
            )
        )
        {
            Serial.println(
                "[play] receive PCM failed"
            );

            return;
        }


        // ====================================================
        // 播放
        // ====================================================

        playChunk(
            pcmBuffer,
            chunkSize
        );


        remaining -=
            chunkSize;


        // ====================================================
        // 播放过程中只发送 ACK
        //
        // 不发送任何文字。
        // ====================================================

        sendAck();
    }


    Serial.println(
        "[play] PLAY completed"
    );
}


// ============================================================
// setup
// ============================================================

void setup()
{
    Serial.begin(
        SERIAL_BAUD
    );


    delay(1000);


#ifdef FIRMWARE_MODE_WIFI

    // ========================================================
    // Wi-Fi + TCP
    // ========================================================

    g_wifi.begin();


    // ========================================================
    // MAX9814 ADC
    // ========================================================

    g_mic.begin(
        MIC_GPIO
    );


    // ========================================================
    // VAD
    // ========================================================

    g_vad.begin(
        VAD_RMS_THRESHOLD,
        VAD_MIN_VOICE_MS,
        VAD_SILENCE_MS
    );


    // ========================================================
    // MicUploader
    // ========================================================

    g_uploader.begin(
        &g_wifi,
        &g_mic,
        &g_vad
    );


    Serial.println(
        "Wi-Fi mode enabled (TCP client + ADC mic)."
    );

#endif


    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "ESP32-S3 WAV AUDIO PLAYER"
    );

    Serial.println(
        "================================"
    );


    Serial.printf(
        "Sample Rate: %d Hz\n",
        SAMPLE_RATE
    );


    Serial.println(
        "Bits: 16"
    );


    Serial.println(
        "Channels: Mono input"
    );


    setupI2S();


    Serial.println(
        "I2S initialized."
    );


    Serial.println(
        "READY"
    );
}


// ============================================================
// loop
// ============================================================

void loop()
{

#ifdef FIRMWARE_MODE_WIFI

    // ========================================================
    // 确保 Wi-Fi 与 TCP 存活
    // ========================================================

    g_wifi.run();


    // ========================================================
    // ADC 采集
    // ========================================================

    g_mic.poll();


    if (!g_wifi.isConnected())
    {
        delay(50);
        return;
    }


    // ========================================================
    // 上行：
    //
    // ADC
    // ↓
    // VAD
    // ↓
    // RECM
    // ↓
    // RPTF
    //
    // RPTF 后 MicUploader 会进入 WAIT_PLAY，
    // 因此不会继续产生新的 RECM。
    // ========================================================

    g_uploader.run();


    // ========================================================
    // 下行门控
    //
    // PC 在收到 RPTF 后才会发送 PLAY。
    //
    // 如果现在 TCP 缓冲区没有至少 4 字节，
    // 就不要调用 receiveBytes()。
    //
    // 否则会阻塞 loop，导致 VAD/录音被饿死。
    // ========================================================

    if (g_wifi.available() < 4)
    {
        return;
    }

#else

    if (Serial.available() < 4)
    {
        delay(1);
        return;
    }

#endif


    // ========================================================
    // 读取 4 字节命令
    // ========================================================

    uint8_t cmd[4];


    if (!receiveBytes(cmd, 4))
    {
        return;
    }


    // ========================================================
    // PLAY
    // ========================================================

    if (
        cmd[0] == PROTO_PLAY[0] &&
        cmd[1] == PROTO_PLAY[1] &&
        cmd[2] == PROTO_PLAY[2] &&
        cmd[3] == PROTO_PLAY[3]
    )
    {
        uint32_t dataSize =
            receiveUint32();


        Serial.printf(
            "[play] PLAY received, %u bytes\n",
            (unsigned)dataSize
        );


        // ====================================================
        // 完整接收并播放
        // ====================================================

        playPCM(
            dataSize
        );


#ifdef FIRMWARE_MODE_WIFI

        /*
         * ====================================================
         * 关键：
         *
         * PLAY 全部播放完以后，
         * 才允许 MicUploader 开始下一轮录音。
         *
         * 这样整个状态变成：
         *
         *     RECORD
         *       ↓
         *     RPTF
         *       ↓
         *     WAIT_PLAY
         *       ↓
         *     PLAY
         *       ↓
         *     PLAY DONE
         *       ↓
         *     RECORD
         * ====================================================
         */

        g_uploader.notifyPlaybackDone();

#endif


        return;
    }


    // ========================================================
    // 未知命令
    // ========================================================

    Serial.printf(
        "[protocol] unknown command: "
        "%02X %02X %02X %02X\n",
        cmd[0],
        cmd[1],
        cmd[2],
        cmd[3]
    );


    delay(1);
}