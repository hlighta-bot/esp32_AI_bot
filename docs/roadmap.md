# ESP32 Voice AI - 阶段划分与后续路线

> 当前版本职责划分、Phase 1-4 演进、目标期架构、开发阶段总表、常用命令速查、一键检查清单、第一次完整联调、过渡期完成标准与项目下一步。

---


# 51. 当前版本的职责划分

## ESP32-S3

负责：

```text
I2S 麦克风采集
VAD（静音检测，Phase 2）
Wi-Fi 上行 / 下行
音频接收 Buffer
I2S 播放
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

## 过渡期回退（当前代码状态）

```text
PC 麦克风 + PC USB Serial 发送 ──► ESP32 播放
```

仅用于链路验证，不作为长期架构。

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

## Phase 1 — 目标期主链路（近期）

```text
ESP32 I2S Mic
 ↓
ESP32 VAD（可选，能量阈值即可）
 ↓
Wi-Fi 上行
 ↓
PC (Whisper + LLM + TTS)
 ↓
Wi-Fi 下行
 ↓
ESP32 I2S DAC
 ↓
Speaker
```

关键动作：

* 固件接入 I2S 麦克风（ICS-43434 / INMP441）
* 固件新增 Wi-Fi 客户端
* PC 新增 `wifi_server.py` 接收上行音频
* 补齐 [`transport_wifi.py`](../pc/transport_wifi.py) 的 ACK 与双向并发
* 更新 [`protocol.md`](./protocol.md) 添加上行帧格式

目标：

> **ESP32 麦克风 + Wi-Fi 双向语音闭环跑通。**

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
| 7  | ESP32 I2S 麦克风 | 目标期 Phase 1 |
| 8  | Wi-Fi Transport（ACK + 双向） | 目标期 Phase 1 |
| 9  | PC wifi_server.py | 目标期 Phase 1 |
| 10 | 协议扩展（上行帧） | 目标期 Phase 1 |
| 11 | ESP32 端 VAD | Phase 2 |
| 12 | Wake Word | Phase 3 |
| 13 | TinyML | Phase 3 |
| 14 | 电池供电 / 低功耗 | Phase 4 |
| 15 | 独立 Voice AI | 最终目标 |

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

第一次联调之前确认（目标期主线）：

```text
[ ] ESP32-S3 N16R8 已连接
[ ] USB 数据线正常（用于烧录 / 日志）
[ ] PlatformIO 已安装
[ ] pio device list 可以看到 ESP32
[ ] firmware 可以编译
[ ] firmware 可以烧录
[ ] Serial = 921600（仅日志 / 回退）
[ ] Wi-Fi 已配网（SSID + 密码）
[ ] PC 与 ESP32 在同一 Wi-Fi 网段
[ ] ESP32 I2S 播放 GPIO 已确认
[ ] ESP32 I2S 麦克风（目标期新增）
[ ] MAX98357A 已接线
[ ] Speaker 已连接
[ ] PC Python 环境已创建
[ ] requirements.txt 已安装
[ ] ffmpeg 已安装
[ ] Whisper.cpp 已配置
[ ] Whisper 模型已配置
[ ] .env 已配置
[ ] GEMINI_API_KEY / SENSENOVA_API_KEY / OLLAMA_URL 已配置
[ ] ESP32_VID / ESP32_PID 已配置
```

---

# 58. 第一次完整联调

当前代码仍是过渡期路径，先按下面执行以验证链路；Wi-Fi 主链路补齐后再切换到 `--transport wifi`。

### Step 1 — 硬件接线

```text
ESP32-S3
   │
   ├── USB ──► PC（烧录 / 日志 / 过渡期回退）
   │
   └── I2S ──► MAX98357A ──► Speaker
```

### Step 2 — 编译固件

```bash
cd ~/projects/esp32-voice-ai/firmware/esp32
pio run
```

### Step 3 — 烧录固件

```bash
pio run --target upload
```

### Step 4 — 检查串口日志

```bash
pio device monitor -b 921600
```

看到：

```text
Ready
```

后按 `Ctrl+C` 退出。

### Step 5 — 准备 PC 环境

```bash
cd ~/projects/esp32-voice-ai/pc
source esp32_voice_ai_env/bin/activate
pip install -r requirements.txt
```

### Step 6 — 启动语音 AI（过渡期路径）

```bash
python voice_chat.py --engine gemini
```

Wi-Fi 主链路补齐后切换到：

```bash
python voice_chat.py --engine gemini --transport wifi
```

### Step 7 — 观察链路

```text
Recording...        (ESP32 I2S Mic 或 PC Mic 采集)
      ↓
Uploading via Wi-Fi (目标期) / Sending via USB (过渡期)
      ↓
Transcribing...
      ↓
LLM Responding...
      ↓
TTS
      ↓
Sending...
      ↓
ESP32
      ↓
I2S
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

如果可以做到：

```text
用户说话
   ↓
ESP32 I2S 麦克风采集
   ↓
Wi-Fi 上行到 PC
   ↓
Whisper 识别
   ↓
LLM 回答
   ↓
Edge TTS 合成
   ↓
Wi-Fi 下行到 ESP32
   ↓
I2S 输出 → MAX98357A → 扬声器
```

则说明：**ESP32 麦克风 + Wi-Fi 双向主链路完成**，可脱离 PC 麦克风与 USB 运行。

---

# 60. 项目下一步

过渡期完成后，按优先级推进：

```text
① ESP32 I2S 麦克风接入
      ↓
② Wi-Fi Transport（ACK + 双向并发）
      ↓
③ PC wifi_server.py 接收上行音频
      ↓
④ protocol.md 扩展上行帧
      ↓
⑤ ESP32 端 VAD
      ↓
⑥ Wake Word
      ↓
⑦ TinyML
      ↓
⑧ 电池供电 / 低功耗
      ↓
⑨ 独立设备
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
