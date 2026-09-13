# ESP32 Voice AI - 系统架构

> 本文档描述项目的分层设计、模块职责和数据流。
> 详细协议见 [`protocol.md`](./protocol.md)，硬件接线见 [`wiring.md`](./wiring.md)。

---

## 1. 目标与阶段划分

### 1.1 最终目标

一个独立的 ESP32 Voice AI 设备，麦克风与扬声器都在 ESP32 上，PC 作为云端服务器：

```text
麦克风 → ESP32-S3 → Wi-Fi → PC (ASR + LLM + TTS) → Wi-Fi → ESP32-S3 → 扬声器
```

关键点：

- **音频采集在 ESP32**：MAX9814 模拟 MEMS 麦克风走 ADC1 GPIO1，或后续切换到 I2S 数字麦克风（如 ICS-43434 / INMP441）
- **传输走 Wi-Fi**：ESP32 与 PC 之间通过 Wi-Fi 双向通信，不再依赖 USB
- **AI 处理仍在 PC**：Whisper / LLM / TTS 在 PC 侧运行，ESP32 只负责 Audio I/O 与网络
- **最终形态**：ESP32 端接入 VAD 与唤醒词后，PC 可脱离物理按键或常驻录音

### 1.2 Phase 1（当前代码状态）

Phase 1 已完成：ESP32 麦克风 + Wi-Fi 双向主链路跑通。

```text
MAX9814 → ESP32 ADC1(GPIO1) → VAD → TCP :8888 → PC / aidlux
                                                │
                                         ASR + LLM + TTS
                                                │
                                              TCP :8888
                                                │
MAX98357A ← I2S(GPIO16/17/15) ← ESP32 ← ─────────┘
```

### 1.3 演进路线

```text
Phase 1（已完成 2026-09-09）: ESP32 MAX9814 麦克风 + Wi-Fi 双向传输
                              上行：Mic → RECM/RPTF → PC
                              下行：PC → PLAY/chunk/ACK → I2S 播放
Phase 2:           更强的 VAD（WebRTC / Silero）、流式 TTS
Phase 3:           ESP32 部署 Wake Word / TinyML，免按键触发
Phase 4:           电池供电，独立 Wi-Fi Voice AI Device
```

过渡期「PC 麦克风 + USB Serial」链路仍保留在代码中，用于无 Wi-Fi 环境兜底和链路故障回退。

---

## 2. 顶层架构图

Phase 1 架构（ESP32 MAX9814 麦克风 + Wi-Fi/TCP 双向）：

```text
                          Wi-Fi / TCP :8888
┌─────────────────────────────────────────────────┐  ◄──────────►  ┌──────────────────────────────┐
│            PC 端 或 aidlux 端                     │                 │         ESP32-S3 N16R8        │
│                                                  │                 │                              │
│   wifi_server.py                                 │  上行 PCM      │  MAX9814 (analog MEMS mic)    │
│   ┌──────────────┐  ┌──────────────────┐         │  RECM/RPTF     │        │                     │
│   │  RECM 收帧   │  │ voice_pipeline() │         │                 │        ▼                     │
│   │  RPTF 触发   │─►│  ASR→LLM→TTS     │◄────────│────────────────│  ADC1 GPIO1 (50k→16k)         │
│   └──────────────┘  └──────────────────┘         │                 │  (mic_adc.cpp)               │
│                                                  │                 │        │                     │
│                                                  │  下行 PCM      │        ▼                     │
│   send_wav.py / _send_play_chunks               │  PLAY+chunk    │  EnergyVad                    │
│   ┌──────────────┐                              │  +ACK           │        │                     │
│   │  chunk+ACK   │◄────────────────────────────│────────────────│        ▼                     │
│   └──────────────┘                              │                 │  MicUploader                 │
│                                                  │                 │        │                     │
└──────────────────────────────────────────────────┘                 │        ▼                     │
                                                                     │  I2S (GPIO16/17/15) → MAX98357A ─► 🔊
                                                                     └──────────────────────────────┘
```

过渡期旁路（保留用于无 Wi-Fi / 首次烧录时的链路验证）：

```text
PC mic.py ─► asr.py ─► llm.py ─► tts.py ─► send_wav.py
                                      │
                                      │ USB Serial 921600
                                      ▼
                              ESP32 播放 ─► I2S ─► Speaker
```

---

## 3. 数据流

一次完整语音对话的端到端流程（Phase 1，当前实现）：

```text
用户说话
    │
    ▼
[1] MAX9814 输出模拟电压（静态 ~VDD/2）
    │
    ▼
[2] ESP32 ADC1 GPIO1 采样（mic_adc.cpp）
    │  50 kS/s → 相位累加器 → 16 kHz
    │  去直流偏置 → 一阶 LP 抗混叠 → 软件增益 ×2
    │  int16 mono PCM
    ▼
[3] EnergyVad（energy_vad.cpp）
    │  RMS > threshold 持续 minVoiceMs → VAD_SPEAK
    │  静音 > silenceMs → VAD_END
    ▼
[4] Wi-Fi/TCP 上行（mic_uploader.cpp）
    │  RECM | flags | size | pcm (每 100 ms 一 chunk)
    │  RPTF | total_size (段末)
    ▼
[5] wifi_server.py 收帧
    │  _read_one_frame() 解析 RECM / RPTF
    │  RPTF 触发 pipeline
    ▼
[6] voice_pipeline.pipeline(wav_bytes)
    │  pcm_to_wav() → Whisper → LLM → Edge TTS
    ▼
[7] 回复 WAV bytes
    │
    ▼
[8] 下行发送（wifi_server.py::_send_reply）
    │  resample_poly → int16 mono PCM
    │  分块 CHUNK_SIZE=4096
    │  PLAY + size + chunk + ACK ×N
    ▼
[9] ESP32 接收（receiveBytes / wifi recv）
    │
    ▼
[10] Mono → Stereo 复制（playChunk）
    │
    ▼
[11] i2s_write (DMA)
    │
    ▼
[12] MAX98357A 放大
    │
    ▼
[13] 扬声器发声
```

过渡期旁路（保留，用于无 Wi-Fi / 首次烧录）：

- 步骤 [1]–[4] 由 PC `mic.py` 录音 + USB Serial 传输替代
- 步骤 [8] 使用 USB Serial 而非 Wi-Fi
- 其余环节（ASR / LLM / TTS / I2S 播放）与主链路一致

---

## 4. 模块职责

### 4.1 PC 端模块

| 模块 | 主要接口 | 说明 |
|------|----------|------|
| [`config.py`](../pc/config.py) | 常量 + `.env` 加载 | 配置中心（含 Wi-Fi / TCP / mDNS / 上行协议常量） |
| [`wifi_server.py`](../pc/wifi_server.py) | `AudioServer.serve_forever()` / `ClientSession` | **Phase 1 主入口**：TCP listener，接收 ESP32 上行，下发 TTS 结果 |
| [`voice_pipeline.py`](../pc/voice_pipeline.py) | `pipeline(wav)` / `pipeline_text(text)` / `pcm_to_wav()` | ASR → LLM → TTS 可复用流水线，供 `wifi_server.py` 和 `voice_chat.py` 共用 |
| [`asr.py`](../pc/asr.py) | `WhisperASR.transcribe_wav()` | Whisper.cpp 子进程调用 |
| [`llm.py`](../pc/llm.py) | `LLMRouter.chat()` | LLM 路由器（SenseNova / Ollama / Gemini） |
| [`tts.py`](../pc/tts.py) | `TTSEngine.synthesize()` | Edge TTS + pydub 转 WAV |
| [`send_wav.py`](../pc/send_wav.py) | `ESP32Player.play_wav_*()` | WAV/PCM 预处理 + 下行协议发送 |
| [`transport.py`](../pc/transport.py) | `TransportInterface` | 传输层抽象 |
| [`transport_wifi.py`](../pc/transport_wifi.py) | `WifiTransport` (TCP) / `WifiTransportUdp` (调试) | TCP 主链路，PC 主动连 ESP32 时使用 |
| [`transport_serial.py`](../pc/transport_serial.py) | `SerialTransport` | pyserial 实现（无 Wi-Fi / 烧录回退） |
| [`voice_chat.py`](../pc/voice_chat.py) | 主入口 | PC 麦克风 + Wi-Fi 交互模式 |
| [`text_to_speak.py`](../pc/text_to_speak.py) | 独立脚本 | 文本 → TTS → ESP32 播放 |
| [`mic.py`](../pc/mic.py) | `MicrophoneRecorder.record()` / `.record_until_silence()` | 过渡期回退：ESP32 麦克风链路故障时使用 |

### 4.2 固件模块

| 模块 | 位置 | 说明 |
|------|------|------|
| Wi-Fi 凭据 | [`config.local.json`](../config.local.json.example) | 单一真相源（JSON）；`scripts/gen_secrets.py` 生成 `secrets.local.h`，被 [`secrets.h`](../firmware/esp32/src/secrets.h) include |
| 协议常量 | [`protocol/frame.h`](../firmware/esp32/src/protocol/frame.h) | PLAY/RECM/RPTF/ACK + packU16/U32 工具 |
| Wi-Fi 客户端 | [`network/wifi_client.{h,cpp}`](../firmware/esp32/src/network/wifi_client.h) | STA 模式 + TCP 长连接，FreeRTOS 互斥锁，mDNS `esp32-voice.local` |
| 麦克风采集 | [`audio/mic_adc.{h,cpp}`](../firmware/esp32/src/audio/mic_adc.h) | MAX9814 → ADC1 GPIO1，50k→16k 相位累加重采样 + LP + 增益 |
| VAD | [`vad/energy_vad.{h,cpp}`](../firmware/esp32/src/vad/energy_vad.h) | RMS 阈值 + minVoiceMs + silenceMs 状态机 |
| 上行分块 | [`audio/mic_uploader.{h,cpp}`](../firmware/esp32/src/audio/mic_uploader.h) | IDLE→REC→REPORT→IDLE，100 ms 一 chunk |
| I2S 初始化 | [`setupI2S()`](../firmware/esp32/src/main.cpp) | I2S_NUM_1，16 kHz，DMA 8×256 |
| 帧接收 | [`receiveBytes()` / `receiveUint32()`](../firmware/esp32/src/main.cpp) | `#ifdef FIRMWARE_MODE_WIFI` 时走 TCP，否则走串口 |
| 播放 | [`playChunk()` / `playPCM()`](../firmware/esp32/src/main.cpp) | Mono→Stereo + I2S write + ACK |
| 主循环 | [`loop()`](../firmware/esp32/src/main.cpp) | 轮询 `PLAY` + `g_uploader.run()`（Wi-Fi 模式下并行） |

---

## 5. 关键设计决策

### 5.1 为什么 AI 仍在 PC 上处理

- ESP32-S3 内存 / CPU 无法承载 Whisper、7B+ LLM、高质量 TTS
- PC 侧更换模型、迭代算法无需重新烧录固件，交付效率更高
- 通过 Wi-Fi 把 ESP32 定位为「Audio I/O + 网络 IO」终端，边界清晰
- 后续如算力允许，可逐步下沉：先 VAD → 再 Wake Word → 最后可选 LLM

### 5.2 为什么用 Wi-Fi 而非蓝牙 / USB

- Wi-Fi 覆盖 ≥ 10 m，蓝牙 BLE 有效音频距离通常 ≤ 3 m，Wi-Fi 更适合桌面 / 房间级部署
- 带宽：2.4 GHz Wi-Fi 提供 ≥ 20 Mbps 实际吞吐，远超 256 kbps 的音频需求，为双向并发留出余量
- 单向部署成本：ESP32-S3 内置 Wi-Fi，不需要额外模块
- USB Serial 仅作为回退（烧录 / 日志 / 无 Wi-Fi 环境兜底），不再作为主链路
- 不使用 BLE：音频 GATT 协商复杂、吞吐受限，不匹配本项目需求

### 5.3 为什么分块 + ACK

- 16 kHz / 16-bit / Mono 音频，4096 字节 chunk ≈ 128 ms，I2S DMA 实时播放可控
- ACK 让对端确认收到，避免 Wi-Fi 抖动 / 丢包导致的 buffer overflow 或断流
- 每 chunk 一次 ACK，延迟可预测；后续可优化为批量 ACK 或 CRC 校验

### 5.4 为什么发 Mono 而非 Stereo

- MAX98357A 是单扬声器驱动，但 I2S 协议要求两声道
- 由 ESP32 在 `playChunk()` 里做 Mono → Stereo 复制
- 节省 50% 带宽和内存

### 5.5 为什么用 Edge TTS 而非本地 TTS

- 边缘 TTS 免费、无需本地模型
- 中文音色质量高
- 通过 pydub + ffmpeg 转 WAV，兼容性有保障
- 后续可切换到 CosyVoice / GPT-SoVITS 等本地方案

### 5.6 为什么用 Whisper.cpp 而非云端 ASR

- 隐私：语音不出本地（仍从 ESP32 上传至 PC，但不出网）
- 延迟可控
- 无需 API Key
- 模型可选（tiny / base / small / medium / large）

### 5.7 为什么保留串口回退

- 烧录与日志仍需 USB 通道
- Wi-Fi 故障或首次配对时可用串口做链路验证
- 通过 [`transport.py`](../pc/transport.py) 抽象切换，无需改业务代码

---

## 6. 传输层抽象

```text
             ┌──────────────────┐
             │ TransportInterface│  (ABC)
             │ .connect()        │
             │ .send(bytes)      │
             │ .receive(size)    │
             │ .disconnect()     │
             └────────┬──────────┘
                      │
        ┌─────────────┴─────────────┐
        │                           │
┌───────▼──────────┐        ┌───────▼─────────┐
│  WifiTransport   │        │ SerialTransport │
│  TCP（主链路）   │        │   pyserial      │
│  PC → ESP32     │        │ 回退 / 烧录     │
└──────────────────┘        └─────────────────┘

另有：
┌──────────────────────────┐
│ wifi_server.py           │  上行业务：ESP32 → PC
│ socket + threading       │  （不走 TransportInterface，因为 Server
│                          │   端主动 accept，不需要 connect()）
└──────────────────────────┘
```

**收益**：业务层不需要关心底层是 Wi-Fi 还是串口，切换传输只需改一处构造。

```python
# 主链路：PC 主动连 ESP32 播放 TTS
player = ESP32Player(WifiTransport(config.ESP32_WIFI_IP, config.WIFI_TCP_PORT))

# 回退：串口
player = ESP32Player(SerialTransport("/dev/ttyUSB0"))

# 上行 Server 模式（ESP32 主动连过来）
server = AudioServer(host=config.WIFI_HOST_BIND, port=config.WIFI_TCP_PORT)
server.start().serve_forever()
```

**协议约定（Phase 1）**：

- 上行（ESP32 → PC）：`RECM` + `flags` + `chunk_size` + `pcm` × N，`RPTF` 段末触发 ASR 流水线
- 下行（PC → ESP32）：`PLAY` + `size` + `chunks` + `ACKs`
- 详细帧格式见 [`protocol.md`](./protocol.md) v2

### 6.1 aidlux 兼容性

`wifi_server.py`、`voice_pipeline.py`、`transport_wifi.py` 都仅依赖 Python 标准库
（`socket` / `threading` / `struct` / `argparse`），未使用 `asyncio` 或任何第三方
网络库，可直接搬入 aidlux slim Python 环境：

- 阻塞式 I/O：保持 `_recv_exact()` 的显式 `recv()` 循环
- 所有超时常量集中在 [`config.py`](../pc/config.py)，迁移时统一调整
- `voice_pipeline.pipeline()` 需要 aidlux 侧的 ASR / LLM / TTS 提供实现，
  保持 `whisper.cpp` CLI 调用或设备端模型即可

---

## 7. 错误处理与容错

| 环节 | 超时 | 失败行为 |
|------|------|----------|
| Wi-Fi STA 连接 | 10 s / 次 | [`WifiClient::run()`](../firmware/esp32/src/network/wifi_client.cpp) 循环重试，日志打印 RSSI |
| TCP 连接 PC | `TCP_CONNECT_TIMEOUT = 5 s` | `WifiClient::run()` 每 `WIFI_RETRY_INTERVAL_MS` 重试 |
| TCP I/O（读写） | `TCP_IO_TIMEOUT = 5 s` | `WifiClient::read()` 返回 0；server `_recv_exact()` 返回 None，session 关闭 |
| VAD 静音 | `silenceMs = 700 ms`（默认） | 触发 `VAD_END`，`mic_uploader` 发 `RPTF` |
| 单段录音 | 硬上限 4 MB | 超出丢弃，等待下一段 |
| ASR | `ASR_TIMEOUT = 120 s` | 返回空字符串 |
| LLM SenseNova | 60 s | 返回空字符串 |
| LLM Ollama | 120 s | 返回空字符串 |
| LLM Gemini | 60 s | 返回空字符串 |
| TTS | 依赖 edge-tts 内部超时 | 返回 `None` |
| 串口写（回退通道） | 5 s | 返回 `False` |
| 播放完成等待 | `duration + 5 s` | 视为完成，继续 |

**上层策略**：

- `wifi_server.py::ClientSession._handle_rptf()`：pipeline 失败只打印日志，不清除 TCP 会话，等待下一段 `RECM`。
- `wifi_server.py::ClientSession._recv_exact()`：任何 `recv()` 异常（`ConnectionResetError` / `socket.timeout`）都视为连接断开，触发 session 退出，`AudioServer` 主循环继续 accept 新连接。
- `voice_chat.py::voice_chat_round()`：任何一步失败都返回 `False`，交互模式下继续下一轮，不中断会话。
- 串口回退仅在 Wi-Fi 完全不可用时手动切换（通过 `--transport serial` 参数）。

---

## 8. 目录结构

```text
esp32-voice-ai/
├── README.md                    # 入口 + 文档索引（精简版）
├── .gitignore
├── audio/                       # 录音样例
├── docs/                        # 文档集合
│   ├── overview.md              # 项目概述（目标/架构/结构）
│   ├── architecture.md          # ← 你在这里（分层设计）
│   ├── modules.md               # PC 端模块
│   ├── config.md                # 配置与凭证
│   ├── hardware.md              # 硬件与音频
│   ├── firmware.md              # 固件开发
│   ├── build.md                 # 启动流程
│   ├── test.md                  # 测试
│   ├── troubleshooting.md       # 故障排查
│   ├── roadmap.md               # 阶段路线
│   ├── protocol.md              # 协议规范
│   └── wiring.md                # 硬件接线
├── firmware/
│   └── esp32/
│       ├── platformio.ini       # env:esp32-s3-n16r8 + env:esp32-s3-n16r8-wifi
│       ├── boards/
│       │   └── esp32-s3-n16r8.json
│       └── src/
│           ├── main.cpp         # 播放主循环 + setupI2S + receiveBytes
│           ├── secrets.h        # Wi-Fi 凭据回退；include secrets.local.h（由 gen_secrets.py 生成）
│           ├── protocol/
│           │   └── frame.h      # PROTO_PLAY/REC/RPTF/ACK + pack/unpack
│           ├── network/
│           │   ├── wifi_client.h
│           │   └── wifi_client.cpp  # STA + TCP Client + mDNS
│           ├── audio/
│           │   ├── mic_adc.h
│           │   ├── mic_adc.cpp      # MAX9814 → ADC1 GPIO1
│           │   ├── mic_uploader.h
│           │   └── mic_uploader.cpp # IDLE→REC→REPORT 状态机
│           └── vad/
│               ├── energy_vad.h
│               └── energy_vad.cpp   # RMS 阈值 + 静音超时
├── mobile/
│   └── android/                 # (预留，见其 README)
└── pc/                          # Python 项目
    ├── __init__.py
    ├── config.py                # 含 Wi-Fi / TCP / mDNS / RECM / RPTF 常量
    ├── requirements.txt
    ├── .env                     # (gitignored)
    ├── wifi_server.py           # Phase 1 主入口：TCP Server
    ├── voice_pipeline.py        # ASR → LLM → TTS 流水线 + pcm_to_wav()
    ├── voice_chat.py            # 交互入口
    ├── text_to_speak.py         # 独立 TTS 测试
    ├── send_wav.py              # 播放器（下行协议）
    ├── mic.py                   # 过渡期回退：PC 麦克风
    ├── asr.py / llm.py / tts.py
    ├── transport.py             # 传输抽象
    ├── transport_wifi.py        # TCP 主链路 + UDP 调试变体
    ├── transport_serial.py      # 回退通道
    └── esp32_voice_ai_env/      # (gitignored venv)
```

---

## 9. 依赖总览

### 9.1 Python

见 [`pc/requirements.txt`](../pc/requirements.txt)：

- 音频：`numpy`, `soundfile`, `scipy`, `sounddevice`, `pydub`
- 串口：`pyserial`
- TTS：`edge-tts`
- LLM：`requests`
- 配置：`python-dotenv`

### 9.2 系统

- `ffmpeg`（pydub 转码）
- `libasound2-dev`（ALSA）
- `portaudio19-dev`（sounddevice 底层）

### 9.3 外部工具

- `whisper.cpp`（ASR）：路径在 [`config.py::WHISPER_CLI`](../pc/config.py) 配置

### 9.4 固件

- PlatformIO + espressif32 平台
- Arduino 框架
- I2S 驱动（ESP-IDF 原生）

---

## 10. 扩展路线图

对齐 §1.3 的演进方向：

### 10.1 Phase 1 — 主链路（已完成 2026-09-09）

- [x] 固件接入 MAX9814 模拟 MEMS 麦克风（ADC1 GPIO1），后续可换 I2S 数字麦克风
- [x] 固件 Wi-Fi STA + TCP Client（`wifi_client.cpp`），mDNS 广播 `esp32-voice.local`
- [x] 固件 `EnergyVad` 能量阈值 VAD + `MicUploader` 分块状态机
- [x] PC `wifi_server.py` 接收上行 RECM/RPTF，触发 ASR → LLM → TTS
- [x] PC `voice_pipeline.py` 抽出 ASR/LLM/TTS 流水线
- [x] 补齐 [`transport_wifi.py`](../pc/transport_wifi.py) 为 TCP 实现
- [x] 更新 [`protocol.md`](./protocol.md) v2 添加上行 RECM/RPTF 帧格式
- [x] 代码风格对齐 aidlux：仅标准库 + 阻塞式 I/O

### 10.2 Phase 2 — 更强 VAD 与流式 TTS（1–2 周）

- [ ] 替换 `EnergyVad` 为 WebRTC VAD 或 Silero VAD（无 API 变化，直接换实现）
- [ ] 支持流式 TTS（边合成边播），降低首包延迟
- [ ] 上行 chunk 与下行 chunk 分帧并行（当前单 TCP 连接串行）

### 10.3 Phase 3 — Wake Word + TinyML（1 月内）

- [ ] 部署唤醒词模型（WakeNet / Picovoice / 自训练）到 ESP32
- [ ] 免按键触发对话

### 10.4 Phase 4 — 独立设备（长期）

- [ ] 电池供电与功耗管理（Wi-Fi Light-Sleep 与麦克风采样节拍协调）
- [ ] 完全脱离 PC 与 USB，独立 Wi-Fi Voice AI Device

### 10.5 aidlux 迁移预留

- [ ] 把 `wifi_server.py` 原样搬到 aidlux slim Python 环境验证
- [ ] 在 aidlux 上跑 whisper.cpp + 本地 LLM + Edge TTS（或替换为设备端模型）
- [ ] 与 PC 端保持 `config.py` 一致，仅调整路径
- [ ] 网络配置策略（mDNS / NVS / Web UI）见 [`network-config.md`](./network-config.md)

### 10.6 其他支线

- [ ] 落实 [`mobile/android/`](../mobile/android/README.md) 客户端（当前仅有占位 README）

---

## 11. 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1   | 2026-09-08 | 首版，从 README 与源码抽取 |
| v2   | 2026-09-09 | Phase 1 主链路（Wi-Fi/TCP + MAX9814 + RECM/RPTF + VAD）落地；架构图、数据流、模块表、目录结构全面更新；新增 aidlux 兼容性小节 |
| v3   | 2026-09-09 | §10.5 补充网络配置策略链接 → [`network-config.md`](./network-config.md) |
