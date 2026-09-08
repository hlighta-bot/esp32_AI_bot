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

- **音频采集在 ESP32**：I2S 麦克风（如 ICS-43434 / INMP441）直接接 ESP32-S3
- **传输走 Wi-Fi**：ESP32 与 PC 之间通过 Wi-Fi 双向通信，不再依赖 USB
- **AI 处理仍在 PC**：Whisper / LLM / TTS 在 PC 侧运行，ESP32 只负责 Audio I/O 与网络
- **最终形态**：ESP32 端接入 VAD 与唤醒词后，PC 可脱离物理按键或常驻录音

### 1.2 过渡期（当前代码状态）

当前代码实现的是「PC 麦克风 + USB Serial」过渡方案：

```text
PC 麦克风 → PC (Whisper + LLM + Edge TTS) → USB Serial → ESP32 → 扬声器
```

**定位**：过渡方案仅用于验证 ASR / LLM / TTS 与 ESP32 I2S 播放的端到端链路，**不作为长期架构**，待 ESP32 I2S 麦克风与 Wi-Fi 双向链路完成后逐步废弃。

### 1.3 演进路线

```text
Phase 1（目标期）: ESP32 I2S 麦克风 + Wi-Fi 双向传输
                   上行：Mic → Wi-Fi → PC
                   下行：PC → Wi-Fi → I2S 播放
Phase 2:           ESP32 端 VAD（静音检测），减少无效上传
Phase 3:           ESP32 部署 Wake Word / TinyML，免按键触发
Phase 4:           电池供电，独立 Wi-Fi Voice AI Device
```

---

## 2. 顶层架构图

目标期架构（ESP32 麦克风 + Wi-Fi 双向）：

```text
                        Wi-Fi (TCP / UDP)
┌─────────────────────────────────────────┐  ◄─────────────────►  ┌──────────────────────────────┐
│              PC 端                        │                        │         ESP32-S3 N16R8        │
│                                          │                        │                              │
│   wifi_server.py                         │                        │   I2S Mic (16 kHz / 16-bit)   │
│   ┌──────────────┐                       │  上行：音频 PCM         │   ┌──────────────┐           │
│   │  接收 WAV    │◄──────────────────────┼───────────────────────│──►│  采集 / VAD    │           │
│   └──────┬───────┘                       │                        │   └──────┬───────┘           │
│          ▼                              │                        │          │                  │
│   asr.py ─► llm.py ─► tts.py            │                        │          ▼                  │
│   Whisper     LLM      Edge TTS         │  下行：音频 PCM         │   I2S DAC ─► MAX98357A ─► 🔊 │
│          │                              │◄───────────────────────┼──────────┤                   │
│          ▼                              │                        │   I2S (GPIO16/17/15)          │
│   wifi_server.py                         │                        │                              │
│   ┌──────────────┐                       │                        │                              │
│   │  发送 WAV    │───┐                   │                        │                              │
│   └──────────────┘   │                   │                        │                              │
└──────────────────────┴───────────────────┘                        └──────────────────────────────┘
```

过渡期旁路（保留用于链路验证，长期废弃）：

```text
PC mic.py ─► asr.py ─► llm.py ─► tts.py ─► send_wav.py
                                      │
                                      │ USB Serial 921600
                                      ▼
                              ESP32 播放 ─► I2S ─► Speaker
```

---

## 3. 数据流

一次完整语音对话的端到端流程（目标期）：

```text
用户说话
    │
    ▼
[1] ESP32 I2S 麦克风采集
    │  16 kHz / Mono / int16 / 连续流
    ▼
[2] ESP32 端 VAD（静音检测）
    │  检测到有效语音片段，打包 WAV header
    ▼
[3] Wi-Fi 上行（PCM 分块）
    │  ESP32 ─► PC
    ▼
[4] PC 端接收 WAV bytes (wifi_server.py)
    │
    ▼
[5] Whisper.cpp 识别 (asr.py)
    │  120s 超时
    ▼
[6] 用户文字
    │
    ▼
[7] LLM 对话 (llm.py → SenseNova / Ollama / Gemini)
    │  60~120s 超时
    ▼
[8] 回复文字
    │
    ▼
[9] Edge TTS 合成 (tts.py)
    │  MP3 → pydub → WAV
    ▼
[10] WAV bytes
    │
    ▼
[11] 预处理
    │  resample_poly → int16 mono PCM
    ▼
[12] 分块 (CHUNK_SIZE=4096)
    │
    ▼
[13] Wi-Fi 下行发送 (PLAY + size + chunks + ACKs)
    │  PC ─► ESP32
    ▼
[14] ESP32 接收 (receiveBytes / wifi recv)
    │
    ▼
[15] Mono → Stereo 复制
    │
    ▼
[16] i2s_write (DMA)
    │
    ▼
[17] MAX98357A 放大
    │
    ▼
[18] 扬声器发声
```

过渡期差异（当前代码实现）：

- 步骤 [1]–[3] 由 PC `mic.py` 录音 + USB Serial 传输替代
- 步骤 [13] 使用 USB Serial 而非 Wi-Fi
- 其余环节（ASR / LLM / TTS / I2S 播放）与目标期一致

---

## 4. 模块职责

### 4.1 PC 端模块

| 模块 | 主要接口 | 说明 |
|------|----------|------|
| [`config.py`](../pc/config.py) | 常量 + `.env` 加载 | 配置中心 |
| `wifi_server.py` | `AudioServer.accept() / .send()` | Wi-Fi 服务端，接收 ESP32 上行音频，下发 TTS 结果（目标期新增） |
| [`asr.py`](../pc/asr.py) | `WhisperASR.transcribe_wav()` | Whisper.cpp 子进程调用 |
| [`llm.py`](../pc/llm.py) | `LLMRouter.chat()` | LLM 路由器（SenseNova / Ollama / Gemini） |
| [`tts.py`](../pc/tts.py) | `TTSEngine.synthesize()` | Edge TTS + pydub 转 WAV |
| [`send_wav.py`](../pc/send_wav.py) | `ESP32Player.play_wav_*()` | WAV/PCM 预处理 + 协议发送 |
| [`transport.py`](../pc/transport.py) | `TransportInterface` | 传输层抽象 |
| [`transport_wifi.py`](../pc/transport_wifi.py) | `WifiTransport` | Wi-Fi 传输（目标期主用，TCP 优先） |
| [`transport_serial.py`](../pc/transport_serial.py) | `SerialTransport` | pyserial 实现（过渡期回退） |
| [`voice_chat.py`](../pc/voice_chat.py) | 主入口 | 完整闭环 |
| [`text_to_speak.py`](../pc/text_to_speak.py) | 独立脚本 | 文本 → TTS → ESP32 播放 |
| [`mic.py`](../pc/mic.py) | `MicrophoneRecorder.record()` / `.record_until_silence()` | **过渡期专用**，主线不再使用；仅当 ESP32 麦克风链路故障时作为回退 |

### 4.2 固件模块

| 模块 | 位置 | 说明 |
|------|------|------|
| I2S 初始化 | [`setupI2S()`](../firmware/esp32/src/main.cpp) | I2S_NUM_1，16 kHz，DMA 8×256（当前仅播放端配置） |
| I2S 麦克风采集 | 目标期新增（建议拆分到 `audio/i2s_mic.cpp`） | I2S_NUM_0，采样率 16 kHz，Mono |
| VAD（静音检测） | 目标期新增（建议 `vad.cpp`） | 基于能量阈值 / WebRTC VAD |
| Wi-Fi 客户端 | 目标期新增（建议 `network/wifi_client.cpp`） | 连接 PC server，双向收发 |
| 帧接收 | [`receiveBytes()` / `receiveUint32()`](../firmware/esp32/src/main.cpp) | 阻塞读串口（过渡期）；目标期改为 Wi-Fi 读 |
| 播放 | [`playChunk()` / `playPCM()`](../firmware/esp32/src/main.cpp) | Mono→Stereo + I2S write + ACK |
| 主循环 | [`loop()`](../firmware/esp32/src/main.cpp) | 轮询 `PLAY` 命令；目标期改为事件驱动（收帧 / VAD 触发） |

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
│  TCP 主 / UDP 备 │        │   pyserial      │
│  目标期主用      │        │   过渡期回退    │
└──────────────────┘        └─────────────────┘
```

**收益**：业务层不需要关心底层是 Wi-Fi 还是串口，切换传输只需改一处构造。

```python
# 目标期（主链路）
player = ESP32Player(WifiTransport("192.168.1.100", 9000))

# 过渡期 / 回退
player = ESP32Player(SerialTransport("/dev/ttyUSB0"))
```

**协议约定（目标期）**：

- 上行（ESP32 → PC）：`START` + `duration` + `chunks`，PC 收到 `END` 后触发 ASR 流水线
- 下行（PC → ESP32）：复用当前 `PLAY` + `size` + `chunks` + `ACKs` 协议
- 详细帧格式与命令码在 [`protocol.md`](./protocol.md) 中扩展，本版仅先约定思路，尚未最终敲定
- Wi-Fi 实现目前只做 UDP send/recv，**ACK 处理与双向并发尚未实现**，需在固件和 PC 两侧补齐后才能替换 USB Serial 作为主链路

---

## 7. 错误处理与容错

| 环节 | 超时 | 失败行为 |
|------|------|----------|
| Wi-Fi 连接 | 10 s（目标期新增） | 自动重试 3 次，仍失败则回退到串口 |
| ESP32 Mic 采集 | 目标期新增，VAD 5 s 无有效音帧视为静音 | 不上传，避免无效流量 |
| ASR | `ASR_TIMEOUT = 120 s` | 返回空字符串 |
| LLM SenseNova | 60 s | 返回空字符串 |
| LLM Ollama | 120 s | 返回空字符串 |
| LLM Gemini | 60 s | 返回空字符串 |
| TTS | 依赖 edge-tts 内部超时 | 返回 `None` |
| 串口写（回退通道） | 5 s | 返回 `False` |
| 播放完成等待 | `duration + 5 s` | 视为完成，继续 |

**上层策略**：`voice_chat.py::voice_chat_round()` 在任何一步失败都返回 `False`，交互模式下继续下一轮，不中断会话；Wi-Fi 连续失败超过阈值时自动切换到 `SerialTransport` 回退路径。

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
│       ├── platformio.ini
│       ├── boards/
│       │   └── esp32-s3-n16r8.json
│       └── src/
│           └── main.cpp         # 固件源码
├── mobile/
│   └── android/                 # (预留，见其 README)
└── pc/                          # Python 项目
    ├── __init__.py
    ├── config.py
    ├── requirements.txt
    ├── .env                     # (gitignored)
    ├── voice_chat.py            # 主入口
    ├── text_to_speak.py         # 独立 TTS 测试
    ├── send_wav.py              # 播放器
    ├── wifi_server.py           # 目标期：接收 ESP32 上行音频（待实现）
    ├── mic.py                   # 过渡期回退：PC 麦克风
    ├── asr.py / llm.py / tts.py
    ├── transport.py             # 传输抽象
    ├── transport_wifi.py        # Wi-Fi 主链路（TCP / UDP）
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

### 10.1 Phase 1 — 目标期主链路（近期）

- [ ] 固件接入 I2S 麦克风（ICS-43434 / INMP441）
- [ ] 固件新增 Wi-Fi 客户端，向 PC server 上行 PCM
- [ ] PC 新增 `wifi_server.py`，接收上行音频并触发 ASR 流水线
- [ ] 补齐 [`transport_wifi.py`](../pc/transport_wifi.py) 的 ACK 逻辑与双向并发
- [ ] 增加 `--transport serial|wifi` CLI 参数，默认切到 `wifi`
- [ ] 更新 [`protocol.md`](./protocol.md) 添加上行帧格式与命令码

### 10.2 Phase 2 — ESP32 端 VAD（1–2 周）

- [ ] 实现静音检测（能量阈值 / WebRTC VAD）
- [ ] VAD 触发上行开始 / 结束，避免持续上传
- [ ] 支持流式 TTS（边合成边播）

### 10.3 Phase 3 — Wake Word + TinyML（1 月内）

- [ ] 部署唤醒词模型（WakeNet / Picovoice / 自训练）到 ESP32
- [ ] 免按键触发对话

### 10.4 Phase 4 — 独立设备（长期）

- [ ] 电池供电与功耗管理
- [ ] 完全脱离 PC 与 USB，独立 Wi-Fi Voice AI Device

### 10.5 其他支线

- [ ] 落实 [`mobile/android/`](../mobile/android/README.md) 客户端（当前仅有占位 README）

---

## 11. 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1 | 2026-09-08 | 首版，从 README 与源码抽取 |
