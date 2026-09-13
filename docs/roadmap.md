# ESP32 Voice AI - 阶段划分与后续路线

> 当前版本职责划分、Phase 1-4 演进、目标期架构、开发阶段总表、常用命令速查、一键检查清单、第一次完整联调、过渡期完成标准与项目下一步。

---


# 51. 当前版本的职责划分

## ESP32-S3

负责（Phase 1 已实现）：

```text
MAX9814 ADC 采集（50 kS/s → 16 kHz）
Energy VAD（静音检测，能量阈值）
Wi-Fi 上行（RECM / RPTF）
音频接收 Buffer（PLAY + ACK）
I2S 播放（MAX98357A → Speaker）
USB Serial 回退链路
```

目标期定位：设备的**唯一物理入口**，负责全部 Audio I/O 与网络 IO。

---

## PC 服务器

负责：

```text
ASR (Whisper.cpp)
LLM (SenseNova / Ollama / Gemini)
TTS (Edge TTS)
音频格式转换
传输控制 (wifi_server.py)
```

目标期定位：**云端服务器**，可替换 / 远程部署 / 关闭。

---

## MAX98357A

负责：

```text
I2S 数字音频
 ↓
功率放大
```

---

## Speaker

负责：

```text
电信号
 ↓
声音
```

---

## 过渡期回退（保留，回退链路）

```text
PC 麦克风 + PC USB Serial 发送 ──► ESP32 播放
```

Phase 1 完成后，Wi-Fi 主链路是主流程；USB Serial 保留作回退 / 烧录 / 日志。

---

# 52. 为什么目标期仍不让 ESP32 跑 LLM

ESP32-S3 的资源有限（8 MB PSRAM、双核 LX7）。

如果在 ESP32 上跑：

```text
ASR
LLM
TTS
```

系统复杂度、功耗、开发成本都会明显增加，且音频质量与响应延迟难以兼顾。

目标期采用：

```text
ESP32
 ↓
Audio I/O + Wi-Fi
```

PC：

```text
AI Processing
```

好处：

* ESP32 边界清晰，固件改动小
* AI 侧迭代无需重烧固件
* 后续可按需下沉：VAD → Wake Word → 可选 LLM

---

# 53. 后续升级路线

## Phase 1 — 目标期主链路（已完成 2026-09-09）

```text
ESP32 MAX9814 ADC (GPIO1)
  ↓
Energy VAD（能量阈值，VAD_IDLE / VAD_SPEAK / VAD_END）
  ↓
Wi-Fi TCP 上行 (RECM + RPTF, port 8888)
  ↓
PC wifi_server.py (Whisper + LLM + Edge TTS)
  ↓
Wi-Fi TCP 下行 (PLAY + size + chunk + ACK)
  ↓
ESP32 I2S DAC → MAX98357A → Speaker
```

关键动作（均已实现）：

* 固件接入 MAX9814 模拟 MEMS（[`mic_adc.cpp`](../firmware/esp32/src/audio/mic_adc.cpp)，50 kS/s → 16 kHz 重采样 + 单极 LP + DC offset）
* 固件 Wi-Fi 客户端（[`wifi_client.cpp`](../firmware/esp32/src/network/wifi_client.cpp)，TCP client + mDNS）
* PC `wifi_server.py`（[`wifi_server.py`](../pc/wifi_server.py)，stdlib only，可迁移到 aidlux）
* 协议扩展上行帧 RECM / RPTF（[`protocol.md`](./protocol.md#42-上行esp32--pc)）
* ESP32 端 Energy VAD（[`energy_vad.cpp`](../firmware/esp32/src/vad/energy_vad.cpp)）

目标：

> **ESP32 麦克风 + Wi-Fi 双向语音闭环跑通（Phase 1 完成）。**

---

## Phase 2 — ESP32 端 VAD

```text
持续采集
 ↓
VAD 触发
 ↓
开始上传
 ↓
静音判定结束
 ↓
停止上传
```

目标：减少无效流量，降低 PC 侧唤醒延迟。

---

## Phase 3 — Wake Word + TinyML

```text
持续采集（低功耗）
 ↓
Wake Word 命中
 ↓
开始录音 / 上传
 ↓
对话结束
 ↓
回到低功耗
```

可选部署：WakeNet / Picovoice / 自训练关键词模型。

---

## Phase 4 — 独立设备

```text
Battery
 ↓
Power Management
 ↓
ESP32-S3
 ↓
Wi-Fi
 ↓
AI (PC / 云 / 可选本地)
```

重点优化：

```text
Wi-Fi 功耗
CPU 功耗
麦克风功耗
待机功耗
唤醒机制
```

目标：

> **独立 Wi-Fi Voice AI Device，可电池供电，可脱离 PC 使用。**

---

# 54. 最终目标架构

```text
                 ┌──────────────────────┐
                 │      ESP32-S3        │
                 │                      │
                 │   I2S Microphone     │
                 │          │           │
                 │          ▼           │
                 │      TinyML          │
                 │    Wake Word         │
                 │          │           │
                 │          ▼           │
                 │        Wi-Fi         │
                 └──────────┬───────────┘
                            │
                            ▼
                  PC 服务器 / 云
                (ASR + LLM + TTS)
                            │
                            ▼
                        Wi-Fi 下行
                            │
                            ▼
                 ┌──────────────────────┐
                 │      ESP32-S3        │
                 │          │           │
                 │          ▼           │
                 │         I2S          │
                 └──────────┬───────────┘
                            │
                            ▼
                       MAX98357A
                            │
                            ▼
                        Speaker
```

最终设备可以脱离：

```text
USB（除烧录 / 日志外）
PC 麦克风
PC 扬声器
按键触发
```

变成真正的：

> **独立 Wi-Fi Voice AI Device**

---

# 55. 开发阶段总表

| 阶段 | 功能 | 状态 |
| -- | ------------------ | -------------- |
| 1  | ESP32 I2S 播放 | 已完成（过渡期） |
| 2  | PC → ESP32 WAV（USB） | 已完成（过渡期） |
| 3  | Edge TTS → ESP32 | 已完成（过渡期） |
| 4  | Whisper ASR | 已完成（过渡期） |
| 5  | LLM 对话 | 已完成（过渡期） |
| 6  | 完整 Voice Chat（PC Mic + USB） | 已完成（过渡期） |
| 7  | ESP32 MAX9814 ADC 麦克风 | 已完成（Phase 1，2026-09-09） |
| 8  | Wi-Fi TCP Transport（双向 + ACK） | 已完成（Phase 1） |
| 9  | PC `wifi_server.py`（stdlib only） | 已完成（Phase 1） |
| 10 | 协议扩展（RECM / RPTF 上行帧） | 已完成（Phase 1） |
| 11 | ESP32 端 Energy VAD（能量阈值） | 已完成（Phase 1，基础版） |
| 12 | 更强 VAD（WebRTC / Silero） | Phase 2 |
| 13 | 流式 TTS / 分块推送 | Phase 2 |
| 14 | Wake Word | Phase 3 |
| 15 | TinyML（Wake Word 模型） | Phase 3 |
| 16 | 电池供电 / 低功耗 | Phase 4 |
| 17 | 独立 Voice AI | 最终目标 |
| 18 | aidlux 迁移（Server → Android slim Python） | 迁移预留（见 [`architecture.md`](./architecture.md#105-aidlux-迁移预留)） |

---

# 56. 常用命令速查

## ESP32

编译：

```bash
cd firmware/esp32
pio run
```

烧录：

```bash
pio run --target upload
```

查看设备：

```bash
pio device list
```

串口：

```bash
pio device monitor -b 921600
```

---

## Python

进入 PC：

```bash
cd pc
```

激活环境：

```bash
source esp32_voice_ai_env/bin/activate
```

安装依赖：

```bash
pip install -r requirements.txt
```

---

## 音频播放测试

```bash
python send_wav.py
```

---

## TTS 测试

```bash
python text_to_speak.py "你好，这是语音测试"
```

---

## ASR 测试

```bash
python asr.py audio.wav
```

---

## LLM 测试

```bash
python llm.py "你好"
```

---

## 完整语音 AI

Gemini：

```bash
python voice_chat.py --engine gemini
```

Ollama：

```bash
python voice_chat.py --engine ollama
```

SenseNova：

```bash
python voice_chat.py --engine sensenova
```

Wi-Fi：

```bash
python voice_chat.py --transport wifi
```

---

# 57. 一键检查清单

第一次联调之前确认（Phase 1 主链路）：

```text
[ ] ESP32-S3 N16R8 已连接
[ ] USB 数据线正常（用于烧录 / 日志）
[ ] PlatformIO 已安装
[ ] pio device list 可以看到 ESP32
[ ] firmware 可以编译
[ ] firmware 可以烧录（env:esp32-s3-n16r8-wifi）
[ ] MAX9814 已接线：Out→GPIO1 / VDD→3V3 / GND→GND / GAIN 悬空 (+50 dB) / AR 悬空 (DC-coupled)
[ ] MAX98357A 已接线
[ ] Speaker 已连接
[ ] Wi-Fi 已配网（config.local.json 中 wifi_ssid + wifi_pass）
[ ] PC / aidlux 已配置静态 IP
[ ] config.local.json 中 pc_host 指向该静态 IP
[ ] PC 与 ESP32 在同一 Wi-Fi 网段
[ ] PC Python 环境已创建
[ ] requirements.txt 已安装
[ ] ffmpeg 已安装
[ ] Whisper.cpp 已配置 + 模型下载完成
[ ] .env 已配置（GEMINI / SENSENOVA / OLLAMA）
[ ] wifi_server.py 已启动：python wifi_server.py --port 8888
[ ] ESP32 上电后串口日志显示已连 Wi-Fi + TCP 到 PC_HOST:8888
```

---

# 58. 第一次完整联调（Phase 1 主链路）

当前代码已支持 Wi-Fi 主链路，按下面执行以验证端到端语音 AI 闭环。

### Step 1 — 硬件接线

```text
ESP32-S3
   │
   ├── USB ──► PC（烧录 / 日志 / 过渡期回退）
   │
   └── I2S ──► MAX98357A ──► Speaker
```

### Step 2 — 编辑网络配置（`config.local.json`）

```bash
cd ~/projects/esp32-voice-ai
cp config.local.json.example config.local.json
# 用你喜欢的编辑器打开 config.local.json，填 5 个字段：
#   wifi_ssid / wifi_pass / pc_host / pc_port / esp32_hostname
```

### Step 3 — 编译固件（Wi-Fi 版）

```bash
cd ~/projects/esp32-voice-ai/firmware/esp32
pio run -e esp32-s3-n16r8-wifi
# PIO pre-build 会自动跑 scripts/gen_secrets.py
# 把 config.local.json 生成到 firmware/esp32/src/secrets.local.h
```

### Step 4 — 烧录固件

```bash
pio run -e esp32-s3-n16r8-wifi --target upload
```

### Step 5 — 检查串口日志

```bash
pio device monitor -b 921600
```

看到：

```text
Ready
```

后按 `Ctrl+C` 退出。

### Step 6 — 准备 PC 环境

```bash
cd ~/projects/esp32-voice-ai/pc
source esp32_voice_ai_env/bin/activate
pip install -r requirements.txt
```

### Step 7 — 启动 Server 与 Voice Chat

启动 Wi-Fi Server：

```bash
python wifi_server.py --port 8888 --engine gemini
```

（可选）本地回环验证走 USB 回退：

```bash
python voice_chat.py --engine gemini
```

### Step 7 — 观察链路

```text
ESP32 上电 → 连 Wi-Fi → TCP 到 PC_HOST:8888
      ↓
麦克风采集（MAX9814 ADC，VAD_IDLE）
      ↓
VAD 触发（VAD_SPEAK）→ RECM chunk 上行
      ↓
静音 → VAD_END → RPTF
      ↓
Transcribing...
      ↓
LLM Responding...
      ↓
TTS (Edge TTS → WAV)
      ↓
PLAY + size + chunk + ACK (Wi-Fi 下行)
      ↓
ESP32 I2S → MAX98357A
      ↓
🔊 Speaker
```

---

# 59. 完成标准

## 59.1 过渡期完成标准（当前代码）

如果可以做到：

```text
用户说话
   ↓
PC 麦克风录音
   ↓
Whisper 正确识别
   ↓
LLM 正确回答
   ↓
Edge TTS 合成
   ↓
PC 通过 USB Serial 发送
   ↓
ESP32 接收
   ↓
I2S 输出 → MAX98357A → 扬声器
```

则说明：**过渡期链路验证完成**，具备继续做目标期改造的基础。

## 59.2 目标期完成标准（Phase 1）

Phase 1 完成后，应该能连续做到：

```text
用户说话
   ↓
ESP32 MAX9814 ADC 采集 → Energy VAD 触发（VAD_SPEAK）
   ↓
Wi-Fi TCP 上行 RECM chunk（port 8888）
   ↓
静音后发 RPTF → wifi_server 触发 pipeline
   ↓
Whisper 识别
   ↓
LLM 回答
   ↓
Edge TTS 合成
   ↓
Wi-Fi TCP 下行 PLAY + size + chunk + ACK
   ↓
I2S 输出 → MAX98357A → 扬声器
```

则说明：**ESP32 麦克风 + Wi-Fi 双向主链路完成**，可脱离 PC 麦克风与 USB 运行。当前代码已达标（2026-09-09）。

## 59.3 aidlux 迁移预留

未来将 Server 迁移到安卓 aidlux 时，只需满足：

* `wifi_server.py` 只依赖 Python 标准库（已满足）
* 无 `asyncio`（已满足）
* 无第三方框架 / `pydantic-settings` / `.env` 依赖（配置走 [`config.py`](../pc/config.py) 常量）
* 阻塞 I/O + 线程池即可工作（`AudioServer` / `ClientSession` 已按此设计）
* ASR / TTS 用设备端二进制（`whisper.cpp` 或本地 TTS 推理）

---

# 60. 项目下一步

Phase 1 主链路已跑通，按优先级推进：

```text
① 稳定调参：Energy VAD 阈值、chunk size、TCP 超时（Phase 2）
      ↓
② 更强 VAD：WebRTC / Silero（Phase 2）
      ↓
③ 流式 TTS：分块 PLAY，压低首字延迟（Phase 2）
      ↓
④ Wake Word：始终监听但不上传（Phase 3）
      ↓
⑤ TinyML：WakeNet / 自训练关键词模型（Phase 3）
      ↓
⑥ aidlux 迁移：Server 迁到安卓 slim Python（迁移预留）
      ↓
⑦ 电池供电 / 低功耗（Phase 4）
      ↓
⑧ 独立设备
```

最终形态：

```text
        🎤
     I2S Mic
         │
         ▼
     ESP32-S3
         │
    Wake Word
         │
         ▼
       Wi-Fi
         │
         ▼
     PC 服务器
    (ASR / LLM / TTS)
         │
         ▼
       Wi-Fi
         │
         ▼
     ESP32-S3
         │
         ▼
        I2S
         │
         ▼
    MAX98357A
         │
         ▼
         🔊
       Speaker
```

**目标：一个可以长期电池供电、通过 Wi-Fi 连接 AI、支持语音唤醒并进行连续语音对话的独立 ESP32 Voice AI 设备。**
