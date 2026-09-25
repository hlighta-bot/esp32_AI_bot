# ESP32 Voice AI - 阶段划分与后续路线

> 当前版本职责划分、Phase 1-4 演进、目标期架构、开发阶段总表、常用命令速查、一键检查清单、第一次完整联调、过渡期完成标准与项目下一步。

---

## Architecture Baseline · 2026-09-21（Milestone 1 已验收 2026-09-23）

本文档自 2026-09-21 起冻结为**架构基线（Architecture Baseline）**，2026-09-23 **Milestone 1（视觉 → 迎宾语音闭环）** 已验收完成。

**Milestone 1 验收要点**：ESP32-CAM 通过 TFLite 检测到人 → raw UDP mDNS 解析 `esp32-voice-ai.local` → HTTP POST `/robot/event` → ESP32-S3 迎宾播放"你好！"。详见 [`MILESTONE_1_VISION_WELCOME.md`](./MILESTONE_1_VISION_WELCOME.md)。

冻结内容：

* **主线 · ESP32-S3 语音**：Phase 1（✅ 已完成 2026-09-09）→ Phase 2 舵机控制（🔜）→ Phase 3 唤醒词（🔜）→ Phase 4 Audio Pre-Roll（🔜）→ Phase 5 麦克风调参（🔜）→ Phase 6 ASR/LLM 诊断（🔜）→ Phase 7 多语言（🔜）→ 低功耗 + 电池 + 独立设备（长期）
* **视觉支线 · ESP32-CAM**：S12-1（✅ 摄像头基础）→ S12-2（✅ 人物区域候选检测）→ S12-3（✅ **CAM → S3 通信，Milestone 1**）→ S12-4（待做：Face Detection）→ S12-5（待做：Face Recognition，架构决策点）→ S12-6（待做：Pan/Tilt）→ S12-7（待做：视觉 + 语音对话闭环）
* **动态 IP / 设备发现**：hostname + mDNS（`esp32-voice-ai` / `esp32-cam` / PC），手工地址作为 fallback
* **Web 配置系统**：基础版已完成（SoftAP / 192.168.4.1 / NVS / Wi-Fi / PC host / PC port / VAD 参数 / reboot / reset / factory-reset），未来扩展为 Dashboard / OTA / 摄像头管理 / 人脸管理 / 日志
* **Wake Word 与 VAD 职责边界**：Wake Word 判断"是否被唤醒"；VAD 判断"用户什么时候开始 / 结束讲话"

冻结含义：**冻结当前架构边界和实施顺序**，不是"以后任何内容都不能修改"。若出现新硬件能力、实际测试结果、重大架构问题，可以在架构评审后重新修改本文件。

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
* ESP32 端 **Energy VAD 已完成**（[`energy_vad.cpp`](../firmware/esp32/src/vad/energy_vad.cpp)，RMS 阈值 + 状态机 VAD_IDLE / VAD_SPEAK / VAD_END）
* ESP32 Web 配置系统（基础版：SoftAP + 192.168.4.1 + NVS + Wi-Fi / PC host / PC port / VAD 参数 / 重启 / 恢复出厂）

目标：

> **ESP32 麦克风 + Wi-Fi 双向语音闭环跑通（Phase 1 完成）。**

> **重要更正：** 早期版本曾把 Phase 2 标为"ESP32 端 VAD"。Energy VAD 已在 Phase 1 完成，Phase 2 现重定义为**语音交互质量与稳定性优化**。

---

## Phase 2 — 语音交互质量与稳定性优化（进行中）

Phase 2 不再"补 VAD"。Energy VAD 已在 Phase 1 完成（[`energy_vad.cpp`](../firmware/esp32/src/vad/energy_vad.cpp)），本阶段围绕**已跑通主链路的稳定性、延迟与体验**打磨：

* **Energy VAD 真机调参**：`minVoiceMs` / `silenceMs` / 能量阈值在不同环境噪声下微调，减少漏检 / 误触发 / 长静音被截断
* **更强 VAD 评估（可选）**：WebRTC VAD 或 Silero VAD 作为 Energy VAD 的候选升级路径，不阻塞主线
* **流式 TTS / 分块 PLAY**：Edge TTS 边合成边推送，压低首字延迟（当前是一次合成完再播）
* **TCP 连接与音频缓冲稳定性**：RECM 分块、PLAY 分块 ACK、断线重连、心跳、超时
* **端到端延迟压测**：说话开始 → 首字播放的 wall-clock，分块 ACK 与上行下行并行化

目标：

> **让 Phase 1 主链路在日常使用下稳定、低延迟、可长时间运行。**

---

## Phase 3 — Wake Word + TinyML（对话入口演进）

Phase 3 引入 **Wake Word**，作为对话入口。**Wake Word 与 VAD 不是一回事**：

```text
Wake Word ≠ VAD
  Wake Word：回答"是否被唤醒"（例如说"小爱同学 / Hey ESP32"）
  VAD：回答"用户什么时候开始 / 结束讲话"（唤醒之后才需要）
```

Phase 3 完成后的最终语音链路：

```text
持续监听（低功耗）
  ↓
Wake Word 命中
  ↓
VAD 触发开始（VAD_SPEAK）
  ↓
Wi-Fi 上行 ASR
  ↓
LLM
  ↓
TTS 合成
  ↓
Wi-Fi 下行播放
  ↓
VAD 触发结束（VAD_END）
  ↓
回到持续监听
```

可选部署：WakeNet / Picovoice / 自训练关键词模型（MLTK / TFLite Micro）。

Phase 3 不改变 Phase 1 主链路：无 Wake Word 时，按键 / VAD 直触仍然可用。

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

语音主线（ESP32-S3，独立节点）与视觉支线（ESP32-CAM，独立节点）在物理上是**两块 ESP32**，通过 Wi-Fi 与 PC 服务器连接。**Vision 推理位置（ESP32-S3 / PC / 其他边缘设备）是未来架构决策点，当前未定**。

```text
        ┌───────────────────────────────────┐
        │            ESP32-S3               │
        │  (Audio 主节点，独立)              │
        │                                   │
        │  MAX9814 ADC (GPIO1)              │
        │         │                         │
        │         ▼                         │
        │  Wake Word (TinyML)  [Phase 3]    │
        │         │  命中                    │
        │         ▼                         │
        │  VAD（开始/结束讲话）  [Phase 1 已] │
        │         │                         │
        │         ▼                         │
        │  Wi-Fi TCP 上行 (RECM/RPTF)       │
        └──────────────┬────────────────────┘
                       │
                       ▼
        ┌───────────────────────────────────┐
        │         PC 服务器 / 云             │
        │  ASR + LLM + TTS + 视觉推理(?)     │
        │  （当前只做 ASR+LLM+TTS；          │
        │   Vision 推理位置待定）             │
        └──────┬──────────────────────┬─────┘
               │                      │
               ▼ Wi-Fi 下行 PLAY+ACK  ▼ (可选)
        ┌──────────────┐      ┌───────────────────────┐
        │  ESP32-S3    │      │      ESP32-CAM        │
        │  I2S DAC     │      │  (Vision 节点，独立)   │
        │    ↓         │      │                       │
        │  MAX98357A   │      │  OV2640 → JPEG → 网络  │
        │    ↓         │      │  hostname / mDNS      │
        │  🔊 Speaker  │      └───────────┬───────────┘
        └──────────────┘                  │
                                          │ (数据通信：Wi-Fi 优先，
                                          │  UART 保留为低延迟 / 备用)
                                          ▼
                                   ESP32-S3 / PC
                                   （接收人物坐标 → Pan/Tilt）
```

要点：

* **Wake Word ≠ VAD**：Wake Word 判断"是否唤醒"，VAD 判断"用户什么时候开始 / 结束讲话"。
* **ESP32-CAM 是独立节点**，物理上不是 ESP32-S3 的从机。它只负责把摄像头画面通过 Wi-Fi 传出去；**Vision 推理位置是未来架构决策点**（可能落在 ESP32-S3 / PC / 其他边缘设备），当前不背 Face Recognition。
* **数据通信方式默认 Wi-Fi**：ESP32-S3 与 ESP32-CAM 都是 Wi-Fi 节点，都支持 mDNS / hostname。UART 保留作为低延迟 / 备用方案，不作为默认方案。
* **动态 IP / 设备发现**：所有设备（ESP32-S3、ESP32-CAM、PC）都可能拿到变化中的 DHCP IP，必须走 hostname + mDNS 发现；手工地址配置作为 fallback。

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

# 54A. Web 配置系统（现状 vs 未来）

**基础 Web 配置已实现**（Phase 1 已完成）：

* SoftAP 配网模式（无 Wi-Fi 时启动热点）
* 固定访问地址 `http://192.168.4.1`
* NVS 持久化存储
* Wi-Fi SSID / password 配置
* PC host / PC port 配置
* VAD 参数（阈值、minVoiceMs、silenceMs）配置
* 重启 / 恢复出厂 / 出厂复位

**未来 Web 管理面板（未做，属 Phase 2 及以后）**：

* Dashboard（连接状态、CPU / 内存 / 电池）
* OTA 升级
* 摄像头管理（ESP32-CAM 状态、快照预览）
* 人脸管理（Face Recognition 阶段才需要）
* 日志查看与导出
* Web 端音频回放

---

# 55. 开发阶段总表

**主线路径（Phase 1-4）**：ESP32-S3 语音 AI 设备。
**支线（Step 12）**：ESP32-CAM + OV2640 视觉 + Pan/Tilt 舵机，与语音主链路**并行推进，互不阻塞**。物理上是**独立的第二块 ESP32**（经典 ESP32-WROOM-32，非 ESP32-S3，无 PSRAM）。

> **注意：** HC-SR04 超声波距离 + ESP8266 + MG90S 舵机演示是**独立项目**，不属于本 `esp32-voice-ai` 仓库范围，未列入本表。

## 主线 · ESP32-S3 语音

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
| 11 | ESP32 端 **Energy VAD（能量阈值，已完成）** | 已完成（Phase 1，基础版） |
| 12 | Web 配置系统（SoftAP + 192.168.4.1 + NVS + Wi-Fi/PC/VAD 参数 + 重启/恢复出厂） | 已完成（Phase 1 基础版） |
| 13 | Energy VAD 真机调参 + 更强 VAD 评估（WebRTC / Silero） | Phase 2 |
| 14 | 流式 TTS / 分块 PLAY（压低首字延迟） | Phase 2 |
| 15 | TCP 稳定性 + 端到端延迟压测 | Phase 2 |
| 16 | Dashboard / OTA / 日志 / 摄像头管理（Web 面板扩展） | Phase 2-3 |
| 17 | Wake Word | Phase 3 |
| 18 | TinyML（Wake Word 模型：WakeNet / Picovoice / 自训练） | Phase 3 |
| 19 | 电池供电 / 低功耗 | Phase 4 |
| 20 | 独立 Voice AI | 最终目标 |
| 21 | aidlux 迁移（Server → Android slim Python） | 迁移预留（见 [`architecture.md`](./architecture.md#105-aidlux-迁移预留)） |

> **注**：Milestone 1 之后，Phase 2 从"语音质量优化总称"进一步拆分为 Phase 2-7（舵机控制 / 唤醒词 / Audio Pre-Roll / 麦克风调参 / ASR-LLM 诊断 / 多语言），详见 [`MILESTONE_1_VISION_WELCOME.md`](./MILESTONE_1_VISION_WELCOME.md) §6。上表 13-20 保留，作为原始粗粒度阶段划分的历史参考。

## 视觉支线 · Step 12（ESP32-CAM，独立设备）

Step 12 是**独立的第二块 ESP32**（ESP32-CAM + OV2640）。**Vision 推理位置是未来架构决策点**（可能在 ESP32-S3、PC 或其他边缘设备），当前未定；ESP32-CAM 只负责通过 Wi-Fi 把画面传出去，**不背 Face Recognition**。

视觉 AI 演进的三阶段（概念区分）：

* **人物区域候选检测**：肤色 + 连通域等简单启发式，输出的是"疑似人物区域"，不是人脸
* **Face Detection**：真正的人脸框检测（Haar / MTCNN / YOLO-Face / InsightFace SCRFD 等）
* **Face Recognition**：人脸识别（ArcFace / InsightFace embedding + 身份比对），涉及隐私与合规

| 阶段 | 功能 | 状态 |
| -- | ------------------ | -------------- |
| S12-1 | ESP32-CAM 摄像头基础（Web 首页 + `/capture` + `/stream`） | 已完成（2026-09-16，测试见 [`test-2026-09-16-step12-1-cam-base.md`](./test-2026-09-16-step12-1-cam-base.md)） |
| S12-2 | **人物区域候选检测**（TFLite Person/NoPerson 推理 + 3 帧去抖，**不是人脸检测**） | 已完成（2026-09-18 起；可行性见 [`STEP_12_2_A_FEASIBILITY.md`](./STEP_12_2_A_FEASIBILITY.md)，测试见 [`test-2026-09-19-step12-2-a-person-detect.md`](./test-2026-09-19-step12-2-a-person-detect.md)；已并入 Milestone 1） |
| S12-3 | **ESP32-CAM → ESP32-S3 视觉数据通信**：**Wi-Fi 网络协议（HTTP POST + raw UDP mDNS）已验收**，UART 保留为低延迟 / 备用方案。**Milestone 1 视觉 → 迎宾语音闭环已达成**（2026-09-23，详见 [`MILESTONE_1_VISION_WELCOME.md`](./MILESTONE_1_VISION_WELCOME.md)） | 已完成（Milestone 1，2026-09-23） |
| S12-4 | **Face Detection**（真正的人脸框检测，替代或补充 S12-2 的肤色区域候选） | 待做 |
| S12-5 | **Face Recognition**（人脸识别，需明确隐私 / 合规边界） | 待做（架构决策点，非近期目标） |
| S12-6 | ESP32-S3 Pan/Tilt 舵机（GPIO 4 / 5，接收 S12-3 坐标后追踪） | 待做 |
| S12-7 | ESP32-CAM ↔ ESP32-S3 ↔ PC 视觉 + 语音对话闭环 | 待做 |

## 网络与设备发现（跨主线 / 支线）

* **动态 IP / mDNS**：ESP32-S3、ESP32-CAM、PC 都是 DHCP 客户端，IP 会变化。必须通过 **hostname + mDNS** 相互发现，Web 配置页支持主机名配置。
* **hostname 统一**：固件实际使用 `esp32-voice-ai`（见 [`wifi_client.cpp`](../firmware/esp32/src/network/wifi_client.cpp:96) 与 [`main.cpp`](../firmware/esp32/src/main.cpp:612)）。**历史文档中出现的 `esp32-voice` 是旧名**，应统一到 `esp32-voice-ai`；配置示例 [`config.local.json.example`](../config.local.json.example) 也使用 `esp32-voice-ai`。
* **ESP32-CAM hostname**：Step 12-1 起默认 `esp32-cam`，后续应统一到项目命名规范。
* **手工地址 fallback**：mDNS 不可用时允许在 Web 配置里填静态 IP。

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

**Phase 1 主链路**（2026-09-09）与 **Milestone 1 视觉 → 迎宾语音闭环**（2026-09-23）已验收。**主线（语音）与支线（Step 12 视觉 + Pan/Tilt）并行推进，互不阻塞**。

Milestone 1 详细验收归档与后续 Phase 2-7 计划见 [`MILESTONE_1_VISION_WELCOME.md`](./MILESTONE_1_VISION_WELCOME.md)。

## 主线 · 语音（Phase 1-7）

```text
Phase 1 ✅（已完成 2026-09-09）
   Energy VAD + Wi-Fi 双向 + Web 配置（基础）
      ↓
Phase 2 🔜 舵机控制（Pan/Tilt）
   MG90S 单舵机 PWM → /robot/event 增加 pan/tilt 字段
      ↓
Phase 3 🔜 唤醒词（Hi，大聪明）
   VAD + Whisper 匹配（不做专用 Wake Word 模型）
      ↓
Phase 4 🔜 Audio Pre-Roll
   ~300 ms 环形缓冲，解决唤醒词尾字被 VAD 截断
      ↓
Phase 5 🔜 麦克风调参
   MAX9814 增益、ADC 削波检测、VAD 阈值、cooldown
      ↓
Phase 6 🔜 ASR/LLM 错误诊断
   [ASR]/[LLM]/[TTS]/[PLAY]/[WW] 分层日志
      ↓
Phase 7 🔜 多语言
   Whisper 语言自动检测 + TTS 语音路由
      ↓
长期    🔜 电池供电 / 低功耗 → 独立 Wi-Fi Voice AI Device
```

## 视觉支线 · Step 12（ESP32-CAM，独立设备）

```text
S12-1 ✅ 摄像头基础（Web + /capture + /stream，2026-09-16）
      ↓
S12-2 ✅ 人物区域候选检测（TFLite Person/NoPerson + 3 帧去抖，不是人脸检测）
      ↓
S12-3 ✅ CAM → S3 通信（raw UDP mDNS + HTTP POST /robot/event，Milestone 1，2026-09-23）
      ↓
S12-4 🔜 Face Detection（真正的人脸框检测）
      ↓
S12-5 🔜 Face Recognition（人脸识别，架构决策点，涉及隐私与合规）
      ↓
S12-6 🔜 ESP32-S3 Pan/Tilt 舵机（接收坐标后追踪；并入主线 Phase 2）
      ↓
S12-7 🔜 视觉 + 语音对话闭环
```

## 跨线基础设施（贯穿全程）

* **动态 IP / 设备发现**：hostname + mDNS（`esp32-voice-ai` / `esp32-cam` / PC），手工地址作为 fallback
* **hostname 统一**：固件 `esp32-voice-ai`，历史文档中的 `esp32-voice` 是旧名，需统一
* **Web 配置扩展**：基础版已完成；未来扩展为 Dashboard / OTA / 日志 / 摄像头管理 / 人脸管理
* **aidlux 迁移**：Server 迁到安卓 slim Python（迁移预留，不阻塞主线）

## 明确不做（或不属于本项目）

* **HC-SR04 + ESP8266 + MG90S 舵机演示**：独立项目，不纳入本仓库
* **ESP32 本地跑 LLM**：见 §52，目标期不做
* **Face Recognition**（S12-5）：属于架构决策点，暂不作为近期目标

最终形态：

```text
        🎤
     I2S Mic
         │
         ▼
     ESP32-S3
         │
    Wake Word
         │  命中
         ▼
        VAD
         │  开始/结束
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

    ┌───────────────┐         Wi-Fi         ┌──────────────────┐
    │  ESP32-CAM    │ ────────────────────►  │  ESP32-S3 / PC   │
    │ (Vision 节点)  │        hostname+mDNS  │  (Vision 推理     │
    │   OV2640      │                        │   位置待定)       │
    └───────────────┘                        └──────────────────┘
       数据通信：Wi-Fi 优先，UART 保留为低延迟 / 备用
```

**目标：一个可以长期电池供电、通过 Wi-Fi 连接 AI、支持语音唤醒并进行连续语音对话的独立 ESP32 Voice AI 设备。**
