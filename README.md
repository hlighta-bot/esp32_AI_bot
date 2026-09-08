# ESP32 Voice AI

基于 **ESP32-S3 N16R8** 的完整语音 AI 系统。

目标期系统闭环：

```text
麦克风 → ESP32 → Wi-Fi → PC(ASR + LLM + TTS) → Wi-Fi → ESP32 → I2S → MAX98357A → 扬声器
```

采用 **ESP32 音频 + PC 服务器** 分工：

* ESP32-S3：负责 I2S 麦克风采集、Wi-Fi 收发、I2S 播放
* PC：负责 ASR、LLM、TTS 等 AI 处理
* PC 与 ESP32：通过 Wi-Fi 双向通信
* USB 串口保留为烧录 / 日志 / Wi-Fi 故障时的回退通道
* 演进方向：ESP32 端 VAD → Wake Word / TinyML → 电池供电 → 独立设备

> 过渡期说明：当前代码实现的是「PC 麦克风 + USB Serial」回退路径，仅用于验证链路，详见 [`docs/architecture.md`](./docs/architecture.md) 与 [`docs/roadmap.md`](./docs/roadmap.md)。

---

## 快速开始

一次性走完最小验证路径（首次运行前请确保 ESP32 已通过 USB 连上 PC）：

```bash
# 1. 编译并烧录固件（只需第一次跑）
cd ~/projects/esp32-voice-ai/firmware/esp32
pio run
pio run --target upload

# 2. 启动语音 AI（在 pc 目录下）
cd ~/projects/esp32-voice-ai/pc
source esp32_voice_ai_env/bin/activate
pip install -r requirements.txt      # 首次跑需要
python voice_chat.py --engine sensenova
```

启动后屏幕会依次出现：
```text
[text-input mode] 在 > 后直接输入文字，回车发送；q/exit 退出
> 你好
[bot] 嗨，我在这呢，有啥事儿？
(ESP32 扬声器播放回复)
>
```

### 输入模式（默认 vs 语音）

| 命令 | 输入方式 | 说明 |
|------|----------|------|
| `python voice_chat.py` | 打字输入 | **默认**。直接键入文字，回车发送；适合 WSL / 无麦克风环境 |
| `python voice_chat.py --voice-input` | 语音输入 | 需要 PC 麦克风；`>` 后按 Enter 开始录音，静音约 2 秒自动结束 |

> WSL2 无原生声卡（`aplay -l` 显示 "no soundcards found"），此时用打字模式。要真跑语音输入，请在 Windows 侧或带音频的 Linux 主机上启动。

### 常用参数

```bash
python voice_chat.py --engine sensenova      # LLM 引擎：sensenova / ollama / gemini
python voice_chat.py --transport serial      # 传输方式：serial（默认）/ wifi
python voice_chat.py --voice-input           # 切回语音输入（默认是打字输入）
python voice_chat.py --list-devices          # 仅列出音频设备（无设备时可能无输出）
```

### 调整 LLM 回复风格

系统提示词集中在 [`pc/config.py`](./pc/config.py) 的 `SYSTEM_PROMPT` 常量，默认让回复"口语化、简短、无 Markdown、最多 5 句"。临时覆盖：

```bash
SYSTEM_PROMPT="只用一句话回答。" python voice_chat.py --engine sensenova
```

详细步骤见 [`docs/build.md`](./docs/build.md)，常见问题见 [`docs/troubleshooting.md`](./docs/troubleshooting.md)。

---

## 文档索引

| 文档 | 内容 |
|------|------|
| [`docs/overview.md`](./docs/overview.md) | 项目目标、系统架构、完整语音闭环、项目结构 |
| [`docs/architecture.md`](./docs/architecture.md) | 分层设计与数据流（模块职责视角） |
| [`docs/modules.md`](./docs/modules.md) | PC 端模块（`mic` / `asr` / `llm` / `tts` / `send_wav` / Transport） |
| [`docs/config.md`](./docs/config.md) | `config.py` 配置项、`.env` 示例、Gemini / Ollama / SenseNova / Whisper.cpp 接入 |
| [`docs/hardware.md`](./docs/hardware.md) | ESP32、MAX98357A、I2S 接线、音频格式、串口协议、ACK 机制 |
| [`docs/wiring.md`](./docs/wiring.md) | ESP32 ↔ MAX98357A 物理接线表 |
| [`docs/protocol.md`](./docs/protocol.md) | PC ↔ ESP32 二进制通信协议详解 |
| [`docs/firmware.md`](./docs/firmware.md) | PlatformIO、编译烧录、串口监视、Python 环境、系统依赖 |
| [`docs/build.md`](./docs/build.md) | 完整启动流程（Step 1-10）、最简启动命令 |
| [`docs/test.md`](./docs/test.md) | Test 1-6 分步验证 |
| [`docs/troubleshooting.md`](./docs/troubleshooting.md) | 常见故障与定位方法 |
| [`docs/roadmap.md`](./docs/roadmap.md) | 阶段划分（Phase 1-4）、常用命令速查、检查清单、下一阶段 |

---

## 项目结构

```text
esp32-voice-ai/
├── README.md                 # 本文件：入口与索引
├── .gitignore
│
├── docs/                     # 全部技术文档
│   ├── architecture.md
│   ├── overview.md
│   ├── modules.md
│   ├── config.md
│   ├── hardware.md
│   ├── wiring.md
│   ├── protocol.md
│   ├── firmware.md
│   ├── build.md
│   ├── test.md
│   ├── troubleshooting.md
│   └── roadmap.md
│
├── firmware/
│   └── esp32/                # PlatformIO 固件
│       ├── platformio.ini
│       ├── boards/
│       │   └── esp32-s3-n16r8.json
│       └── src/main.cpp
│
├── pc/                       # Python 端
│   ├── .env                  # 不入库
│   ├── config.py
│   ├── requirements.txt
│   ├── __init__.py
│   │
│   ├── voice_chat.py         # 主入口
│   ├── send_wav.py
│   ├── text_to_speak.py
│   │
│   ├── wifi_server.py        # 目标期：接收 ESP32 上行音频（待实现）
│   ├── mic.py                # 过渡期回退：PC 麦克风
│   ├── asr.py
│   ├── llm.py
│   ├── tts.py
│   │
│   ├── transport.py
│   ├── transport_wifi.py     # Wi-Fi 主链路（TCP / UDP）
│   └── transport_serial.py   # 回退通道（烧录 / 日志 / 无 Wi-Fi）
│
├── audio/                    # 测试音频
└── mobile/                   # 移动端（预留）
    └── android/
```

---

## 关键参数

| 项 | 值 |
|----|----|
| ESP32 板子 | ESP32-S3 N16R8 |
| I2S 播放 GPIO | BCLK=16, LRCLK=17, DIN=15 |
| I2S 麦克风 GPIO | 目标期新增（如 ICS-43434 / INMP441） |
| 采样率 | 16000 Hz |
| 位深 / 声道 | 16-bit / Mono |
| 分块大小 | 4096 字节 |
| 功放 | MAX98357A |
| 网络 | Wi-Fi（TCP 优先，UDP 备选；端口待定） |
| 串口波特率 | 921600（仅用于烧录 / 日志 / 回退） |

---

## 环境变量

`pc/.env`（不入库）：

```env
SENSENOVA_API_KEY=你的_SENSENOVA_KEY
GEMINI_API_KEY=你的_GEMINI_KEY

OLLAMA_URL=http://localhost:11434
OLLAMA_MODEL=qwen2.5:7b

DEFAULT_LLM_ENGINE=sensenova
# SYSTEM_PROMPT=可选：覆盖 config.py 里的默认系统提示词
```

详见 [`docs/config.md`](./docs/config.md)。

---

## 一键检查清单

第一次联调之前确认：

```text
[ ] ESP32-S3 N16R8 已连接（USB 用于烧录 / 日志）
[ ] PlatformIO 已安装，`pio device list` 可见 ESP32
[ ] firmware 编译、烧录成功
[ ] Serial = 921600（日志 / 回退通道）
[ ] Wi-Fi 已配网，ESP32 与 PC 同一网段（目标期）
[ ] I2S 播放 GPIO 与固件一致
[ ] I2S 麦克风已接线（目标期）
[ ] MAX98357A + Speaker 已接线并共地
[ ] PC Python 环境已创建，`requirements.txt` 已安装
[ ] ffmpeg 已安装
[ ] Whisper.cpp + 模型已配置
[ ] `.env` 已配置（含 API Key）
```

完整清单与分步联调见 [`docs/roadmap.md`](./docs/roadmap.md)。

---

## 完成标准

### 过渡期（当前代码，打字输入模式）

```text
键盘输入 → LLM → Edge TTS → USB Serial → ESP32 → I2S → 扬声器
```

（加 `--voice-input` 可切换为「PC 麦克风 → Whisper → LLM → Edge TTS → USB Serial → ESP32 → I2S → 扬声器」）

### 目标期（Phase 1）

```text
ESP32 I2S 麦克风 → Wi-Fi → PC (Whisper + LLM + TTS) → Wi-Fi → ESP32 → I2S → 扬声器
```

达成目标期闭环即为项目当前阶段完成。后续路线见 [`docs/roadmap.md`](./docs/roadmap.md)。
