# ESP32 Voice AI - 项目概述

> 目标、当前系统架构、完整语音闭环与项目结构。

---

# ESP32 Voice AI

基于 **ESP32-S3 N16R8** 的完整语音 AI 系统。

目标期系统实现：

```text
麦克风
  ↓
ESP32-S3  (I2S Mic)
  ↓
Wi-Fi
  ↓
PC 服务器 (ASR + LLM + TTS)
  ↓
Wi-Fi
  ↓
ESP32-S3  (I2S DAC)
  ↓
MAX98357A
  ↓
扬声器
```

系统采用 **ESP32 音频 + PC 服务器** 分工：

* ESP32-S3：负责音频采集、音频播放、Wi-Fi 网络收发
* PC：负责 ASR / LLM / TTS 等 AI 处理
* 传输：Wi-Fi 为主链路（TCP / UDP）
* 演进方向：ESP32 端 VAD → Wake Word / TinyML → 电池供电 → 独立设备

> 过渡期说明：当前代码实现的是「PC 麦克风 + USB Serial」回退路径，仅用于验证链路，不作为长期架构，详见 [`architecture.md`](./architecture.md) 与 [`roadmap.md`](./roadmap.md)。

---

# 1. 项目目标

本项目的最终目标是制作一个独立的 ESP32 Voice AI 设备。

目标期形态（本项目的当前主目标）：

```text
              ┌─────────────────────┐
              │     ESP32-S3        │
              │                     │
  麦克风 ─────►│  I2S Mic / VAD      │
              │         │           │
              │         ▼           │
              │      Wi-Fi          │
              └─────────┬───────────┘
                        │
                        ▼
                 PC 服务器
              (Whisper + LLM + TTS)
                        │
                        ▼
              ┌─────────────────────┐
              │     ESP32-S3        │
              │         │           │
              │         ▼           │
              │     Wi-Fi ─► I2S    │
              └─────────┬───────────┘
                        │
                        ▼
                   MAX98357A
                        │
                        ▼
                    Speaker
```

关键点：

* **音频采集在 ESP32**：I2S 麦克风直接接 ESP32-S3，不再依赖 PC 麦克风
* **传输走 Wi-Fi**：ESP32 与 PC 之间通过 Wi-Fi 双向通信，替代 USB 串口
* **AI 处理仍在 PC**：Whisper / LLM / TTS 在 PC 侧运行
* **PC 是服务器，不是终端**：PC 可关机替换，也可远程部署；设备形态以 ESP32 为中心

过渡期回退方案（当前代码实现，仅用于验证链路）：

```text
PC Mic → Whisper → LLM → Edge TTS → WAV → USB Serial → ESP32 → I2S → Speaker
```

过渡期完成后，回退路径会保留为烧录 / 日志 / Wi-Fi 故障时的兜底方案。

---

# 2. 当前系统架构

目标期架构（主链路）：

```text
                     ┌─────────────────────────────────┐
                     │           ESP32-S3 N16R8          │
   ┌────────────┐    │                                   │
   │  I2S Mic   │───►│  采集 ─► VAD ─► 分块 ─► Wi-Fi ─┼──┐
   └────────────┘    │                                   │  │
                     │  Wi-Fi ◄── 分块 ◄── Mono→Stereo  │  │
                     │         │                         │  │
                     │         ▼                         │  │
                     │  I2S DAC ─► MAX98357A ─► Speaker  │  │
                     └─────────────────────────────────┘  │
                                                            │
                          Wi-Fi (TCP / UDP)                 │
                                                            │
                     ┌─────────────────────────────────┐  │
                     │              PC 端                │◄─┘
                     │                                   │
                     │  wifi_server.py                   │
                     │    │                              │
                     │    ▼                              │
                     │  asr.py ─► llm.py ─► tts.py       │
                     │    │                              │
                     │    ▼                              │
                     │  wifi_server.py ─► 下行 WAV       │
                     └─────────────────────────────────┘
```

过渡期回退架构（当前代码实现，仅用于链路验证）：

```text
┌─────────────────────────────────────────────────────┐
│                     PC 端                           │
│  mic.py → asr.py → llm.py → tts.py → send_wav.py   │
└──────────────────────┬──────────────────────────────┘
                       │ USB Serial 921600 baud
                       ▼
┌─────────────────────────────────────────────────────┐
│                  ESP32-S3 N16R8                     │
│  Serial / Wi-Fi → Buffer → I2S ──► MAX98357A ─► 🔊 │
└─────────────────────────────────────────────────────┘
```

---

# 3. 完整语音闭环

用户说：

```text
你好，你是谁？
```

系统处理（目标期）：

```text
🎤 麦克风 (I2S Mic)
     │
     ▼
ESP32-S3 采集 + VAD
     │
     ▼
Wi-Fi 上行
     │
     ▼
PC wifi_server.py 接收 WAV
     │
     ▼
Whisper.cpp
     │
     ▼
"你好，你是谁？"
     │
     ▼
Gemini / SenseNova / Ollama
     │
     ▼
"你好，我是一个语音 AI 助手。"
     │
     ▼
Edge TTS
     │
     ▼
WAV / PCM
     │
     ▼
Wi-Fi 下行
     │
     ▼
ESP32-S3 接收 + I2S DAC
     │
     ▼
MAX98357A
     │
     ▼
🔊 扬声器
```

过渡期差异：`麦克风采集 + 上行` 由 PC `mic.py` 完成，`下行` 走 USB Serial，其余环节与目标期一致。

---

# 4. 项目结构

推荐项目结构：

```text
esp32-voice-ai/
│
├── README.md
├── .gitignore
│
├── docs/
│   ├── architecture.md
│   ├── protocol.md
│   └── wiring.md
│
├── firmware/
│   └── esp32/
│       │
│       ├── platformio.ini
│       │
│       ├── boards/
│       │   └── esp32-s3-N16R8.json
│       │
│       └── src/
│           └── main.cpp
│
└── pc/
    │
    ├── .env
    ├── config.py
    ├── requirements.txt
    ├── __init__.py
    │
    ├── voice_chat.py
    ├── send_wav.py
    ├── text_to_speak.py
    │
    ├── mic.py
    ├── asr.py
    ├── llm.py
    ├── tts.py
    │
    ├── transport.py
    ├── transport_serial.py
    └── transport_wifi.py
```

---
