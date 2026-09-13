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

### 路径 A：Wi-Fi 主链路（Phase 1 已完成，推荐）

```bash
# 1. 配置 Wi-Fi（只需一次，配置单一真相源：config.local.json）
cd ~/projects/esp32-voice-ai
cp config.local.json.example config.local.json
# 编辑 config.local.json：wifi_ssid / wifi_pass / pc_host / pc_port / esp32_hostname
# 编辑完无需手动跑 gen_secrets.py；pio run 会自动调用

# 2. 编译并烧录 Wi-Fi 版固件
cd ~/projects/esp32-voice-ai/firmware/esp32
pio run -e esp32-s3-n16r8-wifi
pio run -e esp32-s3-n16r8-wifi --target upload

# 3. 启动 Wi-Fi Server
cd ~/projects/esp32-voice-ai/pc
source ~/projects/esp32-voice-ai/pc/esp32_voice_ai_env/bin/activate
pip install -r requirements.txt      # 首次跑需要
python wifi_server.py --port 8888 --engine sensenova
```

ESP32 上电后自动连 Wi-Fi → TCP 到 `PC_HOST:8888`；对着麦克风说话，VAD 触发 → RECM 上行 → Whisper → LLM → Edge TTS → PLAY 下行 → 扬声器播出。

### 路径 B：USB Serial 回退（打字模式，验证链路 / 无 Wi-Fi）

```bash
# 1. 编译并烧录串口版固件
cd ~/projects/esp32-voice-ai/firmware/esp32
pio run
pio run --target upload

# 2. 启动语音 AI（打字输入）
cd ~/projects/esp32-voice-ai/pc
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
| [`docs/network-config.md`](./docs/network-config.md) | 网络配置与配对方案（mDNS / NVS / Web UI / SoftAP、反向发现、aidlux 迁移） |
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
│   ├── wifi_server.py        # Wi-Fi 主链路 Server（stdlib only，可迁移 aidlux）
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
| 麦克风 | MAX9814 模拟 MEMS（GPIO1 = ADC1_CH0，50 kS/s → 16 kHz） |
| I2S 播放 GPIO | BCLK=16, LRCLK=17, DIN=15 |
| 采样率 | 16000 Hz |
| 位深 / 声道 | 16-bit / Mono |
| 分块大小 | 4096 字节 |
| 功放 | MAX98357A |
| 网络 | Wi-Fi TCP，端口 8888（mDNS: `esp32-voice.local`） |
| 串口波特率 | 921600（仅用于烧录 / 日志 / 回退） |
| VAD | 能量阈值（`VadState`：IDLE / SPEAK / END） |

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
[ ] Wi-Fi 版固件编译烧录成功（env:esp32-s3-n16r8-wifi）
[ ] config.local.json 已配置（wifi_ssid / wifi_pass / pc_host / pc_port / esp32_hostname）
[ ] PC / aidlux 静态 IP 已配置并与 ESP32 同网段
[ ] MAX9814 已接线：GND→GND / VDD→3V3 / Out→GPIO1 / GAIN 悬空 (+50 dB) / AR 悬空 (DC-coupled)
[ ] MAX98357A + Speaker 已接线并共地
[ ] PC Python 环境已创建，`requirements.txt` 已安装
[ ] ffmpeg 已安装
[ ] Whisper.cpp + 模型已配置
[ ] `.env` 已配置（含 API Key）
[ ] wifi_server.py 已启动，日志显示 listening on 0.0.0.0:8888
```

完整清单与分步联调见 [`docs/roadmap.md`](./docs/roadmap.md)。

---

## 完成标准

### 主链路（Phase 1，已完成 2026-09-09）

```text
ESP32 MAX9814 ADC (GPIO1)
  → Energy VAD → Wi-Fi RECM/RPTF 上行
  → PC wifi_server.py (Whisper + LLM + Edge TTS)
  → Wi-Fi PLAY + ACK 下行
  → ESP32 I2S → MAX98357A → 扬声器
```

Server 只用 Python 标准库、无 asyncio、阻塞 I/O，可直接迁移到安卓 aidlux（见 [`docs/architecture.md`](./docs/architecture.md) §10.5）。

### 回退（USB Serial）

```text
键盘输入 / PC 麦克风 → LLM → Edge TTS → USB Serial → ESP32 → I2S → 扬声器
```

保留用于链路验证与烧录日志，不作为长期架构。

后续路线见 [`docs/roadmap.md`](./docs/roadmap.md)。
