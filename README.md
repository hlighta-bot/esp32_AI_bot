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
| [`docs/STEP_12_VISION_SERVO_PLAN.md`](./docs/STEP_12_VISION_SERVO_PLAN.md) | Step 12 视觉 + Pan/Tilt 硬件调查与实施计划（ESP32-CAM 支线） |
| [`docs/STEP_12_2_A_FEASIBILITY.md`](./docs/STEP_12_2_A_FEASIBILITY.md) | Step 12-2-A 可行性分析（QQVGA RGB565 + `frame2jpg_cb` + PersonDetector） |
| [`docs/test-2026-09-16-step12-1-cam-base.md`](./docs/test-2026-09-16-step12-1-cam-base.md) | Step 12-1 摄像头基础测试用例（`/`、`/capture`、`/stream`） |
| [`docs/test-2026-09-19-step12-2-a-person-detect.md`](./docs/test-2026-09-19-step12-2-a-person-detect.md) | Step 12-2-A 本地人物检测测试用例（YCbCr 肤色 + 连通域 + Overlay） |
| [`docs/MILESTONE_1_VISION_WELCOME.md`](./docs/MILESTONE_1_VISION_WELCOME.md) | **Milestone 1 · 视觉 → 迎宾语音闭环（已验收）**：端到端流程、代码路径、关键技术决策、10 条开发原则、Phase 2-7 计划 |

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
│   ├── esp32/                # PlatformIO 固件（主工程：ESP32-S3 语音 AI）
│   │   ├── platformio.ini
│   │   ├── boards/
│   │   │   └── esp32-s3-n16r8.json
│   │   └── src/main.cpp
│   │
│   ├── esp32-mic-test/       # 麦克风 ADC 采集测试（独立 env）
│   └── esp32-cam/            # Step 12 支线：ESP32-CAM + OV2640 视觉（独立 env）
│       ├── platformio.ini    # env:esp32-cam
│       ├── boards/
│       │   └── esp32cam.json
│       └── src/
│           ├── main.cpp                # Web 首页 + /capture + /stream + Overlay
│           ├── person_detector.h/cpp   # YCbCr 肤色 + 8-邻域 BFS 连通域
│           └── draw_overlay.h/cpp      # 5×7 bitmap font + 检测框 / 文本绘制
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
| 网络 | Wi-Fi TCP，端口 8888（mDNS: `esp32-voice-ai.local`） |
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

### 当前路线速览

* **Milestone 1 · 视觉 → 迎宾语音闭环**（✅ **已验收 2026-09-23**）：CAM 看到人 → raw UDP mDNS → HTTP POST → S3 迎宾。详见 [`docs/MILESTONE_1_VISION_WELCOME.md`](./docs/MILESTONE_1_VISION_WELCOME.md)。
* **Phase 1 · 目标期主链路**（✅ 已完成 2026-09-09）：Energy VAD + Wi-Fi 双向 + Web 配置基础版
* **Phase 2 · 舵机控制（Pan/Tilt）**（🔜 计划中）：MG90S 单舵机 PWM → `/robot/event` 增加 `pan`/`tilt` 字段
* **Phase 3 · 唤醒词（Hi，大聪明）**（🔜 计划中）：VAD + Whisper 匹配，不部署专用 Wake Word 模型
* **Phase 4 · Audio Pre-Roll**（🔜 计划中）：~300 ms 环形缓冲，解决唤醒词尾字被 VAD 截断
* **Phase 5 · 麦克风调参**（🔜 计划中）：MAX9814 增益、ADC 削波检测、VAD 阈值、cooldown
* **Phase 6 · ASR/LLM 错误诊断**（🔜 计划中）：`[ASR]`/`[LLM]`/`[TTS]`/`[PLAY]`/`[WW]` 分层日志
* **Phase 7 · 多语言**（🔜 计划中）：Whisper 语言自动检测 + TTS 语音路由
* **Step 12 视觉支线**（🟡 并行推进）：详见下节
* **Canonical hostname**：`esp32-voice-ai`（mDNS `.local` 解析用）

后续路线与 Architecture Baseline（2026-09-21）见 [`docs/roadmap.md`](./docs/roadmap.md)；Milestone 1 验收归档与 Phase 2-7 详细计划见 [`docs/MILESTONE_1_VISION_WELCOME.md`](./docs/MILESTONE_1_VISION_WELCOME.md)。

---

## 支线：Step 12 视觉 + Pan/Tilt（Milestone 1 已验收）

与语音主链路**并行**推进，物理上是**独立的第二块 ESP32**（AI-Thinker ESP32-CAM + OV2640，经典 ESP32 芯片，非 ESP32-S3）。**CAM → S3 / PC 视觉数据通信走 Wi-Fi 优先，UART 保留为低延迟 / 备用方案**（两块设备都是 Wi-Fi 节点，均支持 hostname + mDNS）。

- **Step 12-1 · 摄像头基础**（2026-09-16，已通过）：`firmware/esp32-cam` env，Web 首页 + `/capture` + `/stream` MJPEG。测试用例见 [`docs/test-2026-09-16-step12-1-cam-base.md`](./docs/test-2026-09-16-step12-1-cam-base.md)。
- **Step 12-2 · 本地人物区域候选检测**（2026-09-18 起，已并入 Milestone 1）：切换 `PIXFORMAT_RGB565` + `FRAMESIZE_QQVGA`（160×120），用 TFLite Person/NoPerson 推理 + 3 帧去抖做低资源**人物区域候选检测**（**不是 Face Detection，也不是 Face Recognition**）。可行性分析与测试用例见 [`docs/STEP_12_2_A_FEASIBILITY.md`](./docs/STEP_12_2_A_FEASIBILITY.md) / [`docs/test-2026-09-19-step12-2-a-person-detect.md`](./docs/test-2026-09-19-step12-2-a-person-detect.md)。
- **Step 12-3 · CAM → S3 通信**（✅ **Milestone 1 已验收 2026-09-23**）：CAM 主动通过 **raw UDP mDNS** 解析 `esp32-voice-ai.local` → HTTP POST `/robot/event` → S3 迎宾播放。详见 [`docs/MILESTONE_1_VISION_WELCOME.md`](./docs/MILESTONE_1_VISION_WELCOME.md)。
- 硬件调查与总计划见 [`docs/STEP_12_VISION_SERVO_PLAN.md`](./docs/STEP_12_VISION_SERVO_PLAN.md)。
- **Vision 演进**（见 [`docs/roadmap.md`](./docs/roadmap.md) §55）：人物区域候选检测（S12-2）→ Face Detection（S12-4）→ Face Recognition（S12-5，架构决策点）。
- **明确不做**：YOLO / TFLite / 神经网络 / 云端视觉（受经典 ESP32 320 KB SRAM、无 PSRAM 的硬件上限限制）；Face Recognition 属架构决策点，非近期目标。

---

## 独立项目（不属于本仓库）

- **HC-SR04 超声波 + ESP8266 + MG90S 舵机演示**：独立项目，不纳入本 `esp32-voice-ai` 仓库。
