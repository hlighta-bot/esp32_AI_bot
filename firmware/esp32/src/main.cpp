#include <Arduino.h>
#include <driver/i2s.h>

#ifdef FIRMWARE_MODE_WIFI
#include "network/wifi_client.h"
#include "audio/mic_adc.h"
#include "audio/mic_uploader.h"
#include "vad/energy_vad.h"
#include "config/device_config.h"
#include "web/config_web.h"
#include "web/robot_event_server.h"
#endif

#include "assets/hi_hello.h"

#include "servo/servo_control.h"
#include "protocol/frame.h"


#ifdef FIRMWARE_MODE_WIFI

// ============================================================
// 麦克风
// ============================================================

// MAX9814 麦克风 GPIO
// GPIO1 = ADC1_CH0
#define MIC_GPIO              1


// ========================================================
// Config Mode 入口：BOOT 键 (GPIO0)
//
// 仅作为运行中进入配置界面的入口：
// Normal Mode 下长按 3 秒 → Config Mode。
// 不重置任何 NVS 配置。
// ========================================================

#define CONFIG_MODE_BUTTON_GPIO      0
#define CONFIG_MODE_BUTTON_HOLD_MS   3000


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


// ============================================================
// 播放打断检测 (P2)
//
// 用户说"停"等词语时，麦克风能量会显著高于喇叭回声。
// 使用比 VAD 更高的阈值来区分用户语音和回声。
//
// 实测依据（同 VAD 注释）：
//   静默：rms ≈ 50~70 (×2 后 ≈ 100~140)
//   喇叭回声：rms ≈ 100~500
//   用户正常说话：rms ≈ 200~900
//   用户大声说话（如喊"停！"）：rms ≈ 800~2000+
//
// 阈值 1200：高于典型回声，低于大声说话。
// 宽限期 500ms：播放刚开始时喇叭回声最大，跳过检测。
// ============================================================

#define INTERRUPT_RMS_THRESHOLD   1200
#define INTERRUPT_GRACE_MS        500

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

static WifiClient      g_wifi;
static MicAdc          g_mic;
static EnergyVad       g_vad;
static MicUploader     g_uploader;
static DeviceConfig    g_config;
static ConfigWeb       g_web;
static RobotEventServer g_robotEvent;
static bool            g_inConfigMode = false;

// ------------------------------------------------------------
// Robot event 播放"你好！"的 pending 标志。
//
// 引入原因：RobotEventServer 的 HTTP 回调在 g_web.loopHTTP()
//           内部执行（同步 send(200) 后返回）。若同一轮 loop
//           里紧接着调用阻塞约 1.87 s 的 playHelloHi()，
//           WebServer 的 socket 响应可能被延迟到 playHelloHi
//           结束后才真正 flush 到 wire，ESP32-CAM 侧 HTTP
//           客户端可能触发 read Timeout（HTTPClient error -11）。
//
// 解决方案：consumePlayTrigger() 那一轮只把 flag 转成 pending，
//           本轮 return 让出 CPU 让 WebServer 完成 TCP 应答；
//           下一轮 loop 才进入 playHelloHi() 阻塞播放。
//
// 幂等性：RobotEventServer 内部状态机 (NOT_PRESENT / PRESENT)
//           已保证重复 person_detected 不会再置位 s_playTriggered，
//           因此 s_helloPending 不会因重复触发而累积。
// ------------------------------------------------------------
static bool s_helloPending = false;

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
// 播放打断检测 (P2)
//
// 在 playPCM() 的 chunk 间隙调用。
// 轮询麦克风 ADC，读取若干样本计算 RMS。
// 如果超过 INTERRUPT_RMS_THRESHOLD 且已过宽限期，
// 返回 true 表示用户正在说话，应停止播放。
//
// 注意：此函数不修改 g_mic 的 Ring Buffer 状态
//       （read 是消费式，但 poll 只填充不消费）。
//       播放结束后 MicUploader 的 cooldown 会 clear()
//       所有残留数据，所以这里读取的样本不会影响
//       下一轮录音。
// ============================================================

#ifdef FIRMWARE_MODE_WIFI

static bool checkPlaybackInterrupt(uint32_t elapsedMs)
{
    // 宽限期内不检测
    if (elapsedMs < INTERRUPT_GRACE_MS) {
        return false;
    }

    // 轮询 ADC，填充 Ring Buffer
    g_mic.poll();

    // 读取一批样本（最多 256 个 ≈ 16ms@16kHz）
    static int16_t buf[256];
    size_t n = g_mic.read(buf, 256);

    if (n == 0) {
        return false;
    }

    // 计算 AC RMS
    int64_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        sum += buf[i];
    }
    int32_t mean = (int32_t)(sum / (int64_t)n);

    int64_t sqSum = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t d = (int32_t)buf[i] - mean;
        sqSum += (int64_t)d * d;
    }

    // 整数平方根（二分搜索）
    int32_t sqAvg = (int32_t)(sqSum / (int64_t)n);
    if (sqAvg <= 0) {
        return false;
    }

    int32_t lo = 0;
    int32_t hi = sqAvg;
    if (hi > 46340) {  // sqrt(INT32_MAX) ≈ 46340
        hi = 46340;
    }
    while (lo < hi) {
        int32_t mid = (lo + hi + 1) / 2;
        if (mid <= 46340 && (int64_t)mid * mid <= sqAvg) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    int32_t rms = lo;

    if (rms >= INTERRUPT_RMS_THRESHOLD) {
        Serial.printf(
            "[play] interrupt detected, rms=%d (threshold=%d)\n",
            (int)rms,
            (int)INTERRUPT_RMS_THRESHOLD
        );
        return true;
    }

    return false;
}

#endif


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

    const uint32_t playStartMs =
        millis();

    bool interrupted = false;


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


#ifdef FIRMWARE_MODE_WIFI

        // ====================================================
        // P2: 播放打断检测
        //
        // 每个 chunk 播放后（约 128ms@4096B/16kHz）检查
        // 麦克风能量。如果检测到用户说话，停止播放。
        // ====================================================

        if (!interrupted &&
            checkPlaybackInterrupt(
                millis() - playStartMs
            ))
        {
            interrupted = true;
            Serial.println(
                "[play] user interrupt, stopping playback"
            );
        }

#endif

    }


    // ========================================================
    // 如果被打断，清空 I2S DMA 缓冲中残留的音频
    // ========================================================

#ifdef FIRMWARE_MODE_WIFI

    if (interrupted) {
        // 清空 I2S TX 缓冲，立即停止喇叭输出
        size_t bytesWritten;
        uint8_t silence[1024];
        memset(silence, 0, sizeof(silence));
        // 写入一小段静音确保 DMA 缓冲排空
        i2s_write(
            I2S_PORT,
            silence,
            sizeof(silence),
            &bytesWritten,
            pdMS_TO_TICKS(100)
        );
    }

#endif


    Serial.println(
        interrupted
            ? "[play] PLAY interrupted by user"
            : "[play] PLAY completed"
    );
}


// ============================================================
// playHelloHi
//
// 播放 assets/hi_hello.h 中内嵌的"你好！" PCM。
// 分块调用 playChunk()，块大小 = PCM_CHUNK_SIZE (4096)。
//
// 阻塞时长约 1.87 秒（59904 B / 16000 * 2）；
// 由 main.cpp::loop() 触发（不在 HTTP 回调中直接调用）。
// ============================================================

void playHelloHi()
{
    size_t offset = 0;
    size_t total  = HI_HELLO_PCM_SIZE;

    Serial.printf(
        "[play-hi] playing hi_hello (%u bytes)\n",
        (unsigned)total
    );

    while (offset < total)
    {
        size_t chunk = total - offset;

        if (chunk > PCM_CHUNK_SIZE)
        {
            chunk = PCM_CHUNK_SIZE;
        }

        // 16-bit 采样必须偶数字节
        if (chunk & 1)
        {
            chunk--;
        }

        if (chunk == 0)
        {
            break;
        }

        playChunk(
            const_cast<uint8_t *>(&HI_HELLO_PCM[offset]),
            chunk
        );

        offset += chunk;
    }

    Serial.println("[play-hi] done");
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
    // DeviceConfig (NVS / defaults)
    // ========================================================

    g_config.begin();

    // BOOT 键 (GPIO0) 作为 Config Mode 入口：
    // Normal Mode 下长按 3 秒进入配置界面（不重置任何配置）
    pinMode(CONFIG_MODE_BUTTON_GPIO, INPUT_PULLUP);

    if (!g_config.isConfigValid())
    {
        g_inConfigMode = true;
        g_web.begin(g_config);

        // Config Mode 下也允许 ESP32-CAM 上报事件（同一 WebServer）
        g_robotEvent.begin(g_web.server());

        Serial.println(
            "Configuration mode enabled (SoftAP + captive portal)."
        );
    }
    else
    {
        const RuntimeConfig& cfg =
            g_config.getConfig();

        // ========================================================
        // Wi-Fi + TCP
        // ========================================================

        g_wifi.begin(
            cfg.wifi_ssid,
            cfg.wifi_pass,
            cfg.pc_host,
            cfg.pc_port
        );


        // ========================================================
        // MAX9814 ADC
        // ========================================================

        g_mic.begin(
            MIC_GPIO
        );


        // ========================================================
        // VAD (from RuntimeConfig)
        // ========================================================

        g_vad.begin(
            cfg.vad_rms,
            cfg.vad_min_ms,
            cfg.vad_sil_ms
        );


        // ========================================================
        // MicUploader
        // ========================================================

        g_uploader.begin(
            &g_wifi,
            &g_mic,
            &g_vad
        );


        // ========================================================
        // Normal Mode 下启动 HTTP 配置入口（STA 模式）
        //
        // 仅启动 WebServer，不切换 Wi-Fi 模式，不启动 SoftAP/DNS。
        // 用户可通过 http://esp32-voice-ai.local 访问配置页面。
        // 访问页面不影响 TCP/Mic/VAD 语音链路。
        // ========================================================

        g_web.beginHTTP(g_config);

        // ========================================================
        // Robot Event HTTP 接收器（ESP32-CAM → /robot/event）
        //
        // 借用 ConfigWeb 已启动的同一个 WebServer(80) 实例注册路由，
        // 避免两个 WebServer 实例同时绑定 port 80 冲突。
        // ConfigWeb::loopHTTP() 已负责 handleClient()，
        // RobotEventServer::loop() 为空实现。
        // 见 web/robot_event_server.cpp。
        // ========================================================

        g_robotEvent.begin(g_web.server());


        Serial.println(
            "Wi-Fi mode enabled (TCP client + ADC mic)."
        );
    }

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


    // ========================================================
    // 最小舵机自测：
    // 仅在启动阶段执行一次，用于验证 PWM、方向、中位稳定。
    // 不影响后续 loop、Wi-Fi、HTTP、robot-event、播放链路。
    // ========================================================

    servo_run_test_sequence();

    Serial.println(
        "READY"
    );
}


// ============================================================
// checkConfigModeButton
//
// BOOT 键 (GPIO0) 长按检测：
//   按下 = LOW（INPUT_PULLUP，松开 = HIGH）
//   持续 LOW >= 3000 ms 触发
//   松开之前不重复触发，松开后清除计时
//   非阻塞，不 delay()
// ============================================================

#ifdef FIRMWARE_MODE_WIFI

bool checkConfigModeButton()
{
    static uint32_t pressStartMs = 0;

    if (digitalRead(CONFIG_MODE_BUTTON_GPIO) == LOW)
    {
        // 按下：记录起始时刻（首次按下）
        if (pressStartMs == 0)
        {
            pressStartMs = millis();
        }
        // 持续按下达到阈值 → 触发（只触发一次）
        else if (millis() - pressStartMs >= CONFIG_MODE_BUTTON_HOLD_MS)
        {
            pressStartMs = 0;
            return true;
        }
    }
    else
    {
        // 松开：清除计时，天然去抖
        pressStartMs = 0;
    }

    return false;
}

#endif


// ============================================================
// loop
// ============================================================

void loop()
{

#ifdef FIRMWARE_MODE_WIFI

    // Normal Mode 下检测 BOOT 长按 → 进入 Config Mode
    // if (!g_inConfigMode && checkConfigModeButton())
    if (false && !g_inConfigMode && checkConfigModeButton())
    {
        Serial.println(
            "[config] BOOT long-press: entering config mode"
        );

        // 1. 断开 TCP
        g_wifi.disconnect();

        // 2. 断开 STA Wi-Fi（ConfigWeb::begin 会切到 AP 模式）
        WiFi.disconnect();

        // 3. 清理 Normal Mode 已启动的 HTTP Server（避免路由/socket 残留）
        g_web.stop();

        // 4. 启动 SoftAP + DNS + Web（运行时重新初始化）
        g_web.begin(g_config);

        // 5. 路由被 stop()/begin() 重建，重新注册 /robot/event
        g_robotEvent.begin(g_web.server());

        // 6. 切换 loop() 分支
        g_inConfigMode = true;

        return;
    }

    if (g_inConfigMode)
    {
        g_web.loop();
        return;
    }

    // ========================================================
    // 确保 Wi-Fi 与 TCP 存活
    // ========================================================

    g_wifi.run();


    // ========================================================
    // Normal Mode 下轮询 HTTP 配置入口（STA 模式）
    //
    // WebServer::handleClient() 为异步非阻塞：无请求时立即
    // 返回，不影响后续语音链路；仅保存/重置/重启时触发动作。
    // ========================================================

    g_web.loopHTTP();

    // ========================================================
    // Robot Event HTTP 轮询（ESP32-CAM → /robot/event）
    //
    // 同样为异步非阻塞。回调里只做状态切换与 flag 置位，
    // 不阻塞播放。播放由下方 consumePlayTrigger 分支完成。
    // ========================================================

    g_robotEvent.loop();

    // ========================================================
    // 消费"你好！"播放触发
    //
    // 触发时机：ESP32-CAM POST /robot/event 首次 person_detected。
    // 阻塞时长约 1.87 秒（59904 B / 16000 * 2）。
    //
    // 与 TCP voice AI 主链路互斥：
    //   - playHelloHi() 期间不读 g_wifi，PC 侧的 PLAY 会被
    //     缓存在 TCP 缓冲区，之后按标准流程处理。
    //   - 播放结束后调用 notifyPlaybackDone()，触发 MicUploader
    //     的 500 ms cooldown，避免"你好！"的余音被 VAD 误识。
    // ========================================================

    if (g_robotEvent.consumePlayTrigger())
    {
        s_helloPending = true;
        return;
    }

    if (s_helloPending)
    {
        s_helloPending = false;
        playHelloHi();
        g_uploader.notifyPlaybackDone();
        return;
    }


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