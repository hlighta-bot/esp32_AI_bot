# ESP32 Voice AI - 系统架构

> 本文档描述当前代码的真实架构状态。
> 所有参数、接口、状态机均从实际源码提取，不对未来计划做推测。
> 协议细节见 [`protocol.md`](./protocol.md)，硬件接线见 [`wiring.md`](./wiring.md)。

---

## 1. 项目概述

### 1.1 目标

ESP32-S3 作为语音 AI 的音频终端（麦克风采集 + 扬声器播放 + 网络 I/O），PC 端运行 ASR + LLM + TTS。ESP32 与 PC 之间通过 Wi-Fi/TCP 双向通信。

### 1.2 当前代码状态（2026-09-26）

已完成并**实际验证通过**的功能层：
- **P0** Wi-Fi/TCP 自动重连（`wifi_client.cpp`）— 2026-09-26 实测通过。ESP32 每 5s 低频探针 `_client.connected()`，检测到 PC 端 `wifi_server.py` 被杀后 3s 内自动重连。
- **P1** Wake Word「你好」（`command_router.py` + `wifi_server.py`）— 2026-09-26 实测通过。严格正则匹配，SLEEPING 状态命中 → ACTIVE，ACTIVE 状态命中 → 回复"我在，请说"。
- **P3** Audio Pre-Roll 250ms（`mic_uploader.cpp::pushPreRoll`）— 解决唤醒词尾字被 VAD 截断的问题。
- **Milestone 1** ESP32-CAM 视觉 → ESP32-S3 迎宾语音闭环（2026-09-23 已验收）
- **Config Mode** NVS 持久化配置 + SoftAP + Web UI 配网
- **架构分层** CommandRouter + Session State（SLEEPING/ACTIVE）

当前已实现但**不追求最优**的功能层：
- **P2** Barge-in / 打断（ESP32 能量检测 + PC CommandRouter）— 语义链路正确，但 ESP32 端 `checkPlaybackInterrupt()` 检测频率低（每 128ms 一次）+ 500ms 宽限期，实际用户体验不佳。详见 [`barge-in-known-issues.md`](./barge-in-known-issues.md)，本轮不做优化，作为后续任务。

### 1.3 架构图

```
                         Wi-Fi / TCP :8888
┌──────────────────────────────────────┐ ◄──────────► ┌──────────────────────────────┐
│           PC 端 (Linux / WSL)         │                │       ESP32-S3 N16R8         │
│                                      │                │                              │
│  wifi_server.py                      │  上行 PCM     │  MAX9814 (analog MEMS mic)   │
│  ┌────────────────┐  ┌───────────┐   │  RECM/RPTF    │        │                     │
│  │  RECM 收帧     │  │voice_pipe.│   │                │        ▼                     │
│  │  RPTF 触发     │─►│ ASR→LLM→  │◄──│────────────────│  ADC1 GPIO1 (50k→16k)        │
│  └────────────────┘  │ TTS       │   │                │  (mic_adc.cpp)               │
│                      └───────────┘   │                │        │                     │
│  ┌────────────────┐  ┌───────────┐   │                │        ▼                     │
│  │ CommandRouter  │  │Session S. │   │  下行 PCM     │  EnergyVad                    │
│  │ classify(text) │  │SLEEPING/  │   │  PLAY+chunk   │        │                     │
│  │ WAKE/INT/TXT   │  │ACTIVE     │   │  +ACK         │        ▼                     │
│  └────────────────┘  └───────────┘   │                │  MicUploader                 │
│                                      │                │        │                     │
│  外部服务：                           │                │        ▼                     │
│  Whisper.cpp → LLMRouter → Edge TTS │                │  I2S (GPIO16/17/15)          │
│                                      │                │   → MAX98357A ─► 🔊         │
└──────────────────────────────────────┘                └──────────────────────────────┘

              ↕ ESP32-CAM 支线（独立设备，HTTP POST /robot/event）
              ESP32-CAM → TFLite Person Detection → HTTP POST → ESP32-S3 → playHelloHi()

              ↕ USB Serial 回退（仅 esp32-s3-n16r8 环境，无 FIRMWARE_MODE_WIFI）
              PC voice_chat.py ↔ ESP32 播放（send_wav.py）
```

---

## 2. 硬件架构

### 2.1 ESP32-S3 N16R8

| 属性 | 值 |
|------|-----|
| 主频 | 240 MHz |
| Flash | 16 MB (QIO, 80 MHz flash clock) |
| PSRAM | 8 MB (OPi) |
| 框架 | Arduino (esp32 core) |
| 分区 | default_16MB.csv |
| 上传速度 | 460800 baud |
| 监视速度 | 921600 baud |

### 2.2 MAX9814 模拟 MEMS 麦克风

| 参数 | 值 |
|------|-----|
| GPIO | GPIO1 (ADC1_CH0) |
| 原始采样率 | 50 kHz |
| ADC 位宽 | 12-bit |
| 目标采样率 | 16 kHz |
| 处理链 | ADC → DC removal → LP filter (α=6554/8192≈0.8) → 重采样 → ×2 gain → Ring Buffer (4096 samples) |
| 轮询周期 | 每 2 ms (MIC_POLL_US=2000) |
| DC 偏置跟踪 | MIC_DC_TRACK_SHIFT=8 |

### 2.3 MAX98357A I2S 数字放大器

| 参数 | 值 |
|------|-----|
| I2S 端口 | I2S_NUM_1 |
| BCLK | GPIO16 |
| LRC | GPIO17 |
| DIN | GPIO15 |
| 采样率 | 16 kHz |
| 位深 | 16-bit |
| 模式 | Master + TX |
| Mono→Stereo | 在 `playChunk()` 中左右声道复制 |

### 2.4 其他 GPIO

| GPIO | 功能 | 状态 |
|------|------|------|
| GPIO0 | CONFIG_MODE_BUTTON_GPIO (BOOT 键) | 已定义但当前 disabled（`if (false && ...)`），Config Mode 仅在首次启动（无有效 NVS 配置时）进入 |

### 2.5 舵机（MG90S）

| 参数 | 值 |
|------|-----|
| 信号 GPIO | GPIO5 |
| PWM 频率 | 50 Hz |
| PWM 分辨率 | 13-bit |
| 中心角 | 90° |
| 左角 | 60° |
| 右角 | 120° |
| 当前用法 | 仅在 `setup()` 中执行一次自测序列 (`servo_run_test_sequence()`)，**loop() 中未使用** |
| 状态 | 最小验证代码，尚未接入语音控制 |

---

## 3. ESP32 固件架构

### 3.1 PlatformIO 环境

| 环境名 | FIRMWARE_MODE_WIFI | 用途 |
|--------|-------------------|------|
| `esp32-s3-n16r8` | ❌ 未定义 | 串口模式，无 Wi-Fi 代码。用于基础音频播放测试。 |
| `esp32-s3-n16r8-wifi` | ✅ `-DFIRMWARE_MODE_WIFI` | **主链路**。包含 Wi-Fi/TCP、VAD、上行、Web 配置、Robot Event。pre-build hook 运行 `scripts/gen_secrets.py` 从 `config.local.json` 生成 `secrets.local.h`。 |

`FIRMWARE_MODE_WIFI` 是唯一区分宏。所有 Wi-Fi 相关代码（`WifiClient`, `MicAdc`, `EnergyVad`, `MicUploader`, `ConfigWeb`, `RobotEventServer`, `checkPlaybackInterrupt`, `notifyPlaybackDone`）均在 `#ifdef FIRMWARE_MODE_WIFI` 保护下。

### 3.2 初始化序列（setup()）

```
1. Serial.begin(921600)
2. delay(1000)
3. g_config.begin()                    ← NVS 加载或回退 secrets.h
4. pinMode(CONFIG_MODE_BUTTON_GPIO, INPUT_PULLUP)
5. if (!g_config.isConfigValid()):
   → g_inConfigMode = true
   → g_web.begin(g_config)             ← SoftAP + captive portal
   → g_robotEvent.begin(g_web.server())
   else:
   → g_wifi.begin(ssid, pass, host, port)
   → g_mic.begin(MIC_GPIO)
   → g_vad.begin(rms, min_ms, sil_ms)  ← 从 RuntimeConfig 读取
   → g_uploader.begin(&wifi, &mic, &vad)
   → g_web.beginHTTP(g_config)         ← STA 模式下启动 WebServer(80)
   → g_robotEvent.begin(g_web.server())  ← 复用同一 WebServer 注册 /robot/event
6. setupI2S()                          ← 初始化 I2S_NUM_1
7. servo_run_test_sequence()           ← 仅执行一次
8. Serial.println("READY")
```

### 3.3 主循环（loop()）— Wi-Fi 模式

```
1. if (!g_inConfigMode && checkConfigModeButton()):   ← 当前 disabled
2. if (g_inConfigMode): g_web.loop(); return;
3. g_wifi.run()                            ← 非阻塞 Wi-Fi/TCP 重连
4. g_web.loopHTTP()                        ← 异步 HTTP 配置入口
5. g_robotEvent.loop()                     ← 异步 Robot Event 接收
6. if (g_robotEvent.consumePlayTrigger()):  ← 消费播放触发
   → s_helloPending = true; return;
   (下一轮: playHelloHi(); g_uploader.notifyPlaybackDone(); return;)
7. g_mic.poll()                            ← ADC 采集填充 Ring Buffer
8. if (!g_wifi.isConnected()): delay(50); return;
9. g_uploader.run()                        ← VAD 状态机 + 上行
10. if (g_wifi.available() < 4): return;   ← 下行门控，避免阻塞
11. receiveBytes(cmd, 4)
12. if (PLAY): playPCM(dataSize); g_uploader.notifyPlaybackDone(); return;
```

### 3.4 模块清单

| 模块 | 源文件 | 职责 |
|------|--------|------|
| `WifiClient` | `network/wifi_client.{h,cpp}` | STA 模式 + TCP Client，mDNS `esp32-voice-ai`，非阻塞重连 |
| `MicAdc` | `audio/mic_adc.{h,cpp}` | MAX9814 → ADC1 GPIO1，50k→16k 相位累加重采样 + LP + ×2 gain |
| `EnergyVad` | `vad/energy_vad.{h,cpp}` | RMS 阈值 + minVoiceMs + silenceMs 状态机 |
| `MicUploader` | `audio/mic_uploader.{h,cpp}` | VAD 状态机 + 250ms pre-roll + RECM/RPTF 分块上传 |
| `DeviceConfig` | `config/device_config.{h,cpp}` | NVS 持久化配置，RuntimeConfig 结构体 |
| `ConfigWeb` | `web/config_web.{h,cpp}` | SoftAP + captive portal + Web 配置页 + STA HTTP |
| `RobotEventServer` | `web/robot_event_server.{h,cpp}` | 接收 ESP32-CAM HTTP POST `/robot/event`，触发 playHelloHi() |
| `servo_control` | `servo/servo_control.{h,cpp}` | MG90S 舵机最小控制，仅 setup() 自测 |
| `playHelloHi` | `assets/hi_hello.h` | 内嵌 PCM 资源，迎宾语音"你好！" |

---

## 4. Wi-Fi/TCP 架构

### 4.1 Wi-Fi 连接（`wifi_client.cpp`）

```
run() 每轮 loop 调用：
  1. WiFi.status() != WL_CONNECTED → tryConnectWifi()
  2. tryConnectWifi():
     - if (_nextWifiRetryMs > millis()): return (节流)
     - WiFi.begin(ssid, password)     ← 非阻塞，仅触发连接
     - _nextWifiRetryMs = millis() + WIFI_RETRY_INTERVAL_MS (3000ms)
  3. if (WiFi.status() == WL_CONNECTED):
     - if (!_mdnsStarted): MDNS.begin("esp32-voice-ai"); _mdnsStarted = true
     - tryConnectTcp()
  4. tryConnectTcp():
     - if (_nextTcpRetryMs > millis()): return (节流)
     - _client.connect(pcIp, pcPort, WIFI_CONNECT_TIMEOUT_MS=5000)
     - 成功: _tcpEstablished = true
     - 失败: _nextTcpRetryMs = millis() + WIFI_RETRY_INTERVAL_MS
```

**关键设计**：
- Wi-Fi 连接使用 `WiFi.begin()` 非阻塞调用，不阻塞 loop
- TCP 连接使用 `_client.connect()` 阻塞调用（最长 5 秒），但受 `_nextTcpRetryMs` 节流
- `_tcpEstablished` 标志管理 TCP 连接状态，**不使用 `_client.connected()`** 作为主判断
- `isConnected()` = `WiFi.status() == WL_CONNECTED && _tcpEstablished`
- `read()`/`write()` 失败时设置 `_tcpEstablished = false`
- FreeRTOS `SemaphoreHandle_t _mutex` 保护 TCP I/O

**超时参数**：

| 常量 | 值 |
|------|-----|
| `WIFI_CONNECT_TIMEOUT_MS` | 5000 ms |
| `WIFI_RETRY_INTERVAL_MS` | 3000 ms |
| `WIFI_LOCK_TIMEOUT_MS` | 100 ms |
| `TCP_READ_TIMEOUT_MS` | 100 ms |

### 4.2 mDNS

- hostname: `esp32-voice-ai`
- `WiFi.setHostname("esp32-voice-ai")` 在 `begin()` 中调用
- `MDNS.begin("esp32-voice-ai")` 在 `tryConnectWifi()` 中 WiFi 连上后首次调用
- `_mdnsStarted` 标志确保整个生命周期只启动一次
- ESP32-CAM 通过 `MDNS.queryHost("esp32-voice-ai")` 解析到 ESP32-S3 IP

### 4.3 Robot Event Server（ESP32-CAM 支线）

- 复用 ConfigWeb 的同一个 `WebServer(80)` 实例注册 `/robot/event` 路由
- 不创建独立 WebServer，避免端口冲突
- 状态机：`PersonNotPresent` ↔ `PersonPresent`
- 首次 `person_detected` → `consumePlayTrigger()` 返回 true → `s_helloPending = true`
- 下一轮 loop：`playHelloHi()` (约 1.87 秒) → `g_uploader.notifyPlaybackDone()`
- `s_helloPending` 设计避免 HTTP 回调与播放的同一轮 loop 阻塞冲突

---

## 5. PC 软件架构

### 5.1 模块清单

| 模块 | 文件 | 职责 |
|------|------|------|
| `config` | `pc/config.py` | 配置中心：路径、API Key、LLM 引擎、TTS、ASR、协议常量、会话控制 |
| `wifi_server` | `pc/wifi_server.py` | **Phase 1 主入口**：TCP Server，接收 ESP32 上行，下发 TTS |
| `voice_pipeline` | `pc/voice_pipeline.py` | ASR → LLM → TTS 流水线，`pipeline()` 和 `pipeline_text()` |
| `command_router` | `pc/command_router.py` | 会话控制层：ASR 文本分类，WAKE_WORD/INTERRUPT/USER_TEXT |
| `asr` | `pc/asr.py` | Whisper.cpp 子进程调用 |
| `llm` | `pc/llm.py` | LLMRouter：SenseNova / Ollama / Gemini |
| `tts` | `pc/tts.py` | Edge TTS + pydub 转 WAV |
| `voice_chat` | `pc/voice_chat.py` | PC 麦克风 + Wi-Fi 交互模式 |
| `send_wav` | `pc/send_wav.py` | WAV/PCM 预处理 + 下行协议 |
| `transport` | `pc/transport.py` | 传输层抽象接口 |
| `transport_wifi` | `pc/transport_wifi.py` | TCP 主链路 |
| `transport_serial` | `pc/transport_serial.py` | pyserial 回退通道 |

### 5.2 配置加载顺序（`config.py`）

```
1. 硬编码默认值（保底）
2. config.local.json（网络配置单一真相源，与固件共享）
3. .env / 环境变量（LLM API Key）
```

`config.local.json` 同时被 `scripts/gen_secrets.py` 读取，生成固件端 `secrets.local.h`。PC 和固件共享同一份网络配置。

### 5.3 wifi_server.py 架构

```
AudioServer
├── socketserver.TCPServer 子类
├── serve_forever(): accept 循环
└── ClientSession (threading.Thread, daemon)
    ├── __init__: CommandRouter, _wake_activated=False
    ├── run(): _recv_exact() 循环
    ├── _read_one_frame(): 解析 RECM / RPTF
    ├── _handle_rec(): 缓存 RECM PCM
    ├── _handle_rptf(): ASR → CommandRouter → 事件处理
    ├── _send_reply(): WAV → PCM → resample → PLAY+chunk+ACK
    ├── _send_empty_play(): PLAY | u32 size=0（解锁 ESP32）
    └── _send_play_chunks(): 分块发送
```

---

## 6. CommandRouter / Session State

### 6.1 CommandRouter

位置：`pc/command_router.py`

```python
class CommandType(Enum):
    WAKE_WORD  = "wake_word"
    INTERRUPT  = "interrupt"
    USER_TEXT  = "user_text"

class CommandRouter:
    def classify(self, text: Optional[str]) -> CommandType:
```

**分类规则**（包含匹配，无大小写处理）：
1. `text` 为 None 或空 → `USER_TEXT`
2. `WAKE_WORD in text` → `WAKE_WORD`（优先检测）
3. `any(w in text for w in INTERRUPT_WORDS)` → `INTERRUPT`
4. 其他 → `USER_TEXT`

**关键约束**：
- 本模块不依赖任何 LLM
- 唤醒词检测在打断词之前，避免唤醒词包含打断词时误判
- 切换 LLM Provider 不影响唤醒/打断逻辑

### 6.2 Session State（`ClientSession._wake_activated`）

| 状态 | `_wake_activated` | 行为 |
|------|-------------------|------|
| SLEEPING | `False` | WAKE_WORD → 激活 + 回复确认语；INTERRUPT/USER_TEXT → 丢弃 |
| ACTIVE | `True` | WAKE_WORD → 回复确认语；INTERRUPT → 无 TTS；USER_TEXT → LLM Router → TTS |

### 6.3 _handle_rptf() 事件处理流程

```
1. 合并 PCM → pcm_to_wav()
2. transcribe(wav_bytes)          ← ASR（Whisper.cpp）
3. CommandRouter.classify(user_text)
4. SLEEPING:
   → WAKE_WORD: _wake_activated=True, reply_wav = synthesize(WAKE_WORD_REPLY)
   → 其他: 丢弃
5. ACTIVE:
   → INTERRUPT: 无 TTS
   → WAKE_WORD: reply_wav = synthesize(WAKE_WORD_REPLY)
   → USER_TEXT: reply_wav = pipeline_text(user_text)
6. if reply_wav: _send_reply(reply_wav)
   else: _send_empty_play()       ← 解锁 ESP32 WAITING_FOR_PLAYBACK
7. 清理 _session_pcm = None
```

---

## 7. Wake Word

**当前唤醒词：`"你好"`（2026-09-26 从"大聪明"切换，实测通过）**

| 参数 | 值 | 来源 |
|------|-----|------|
| 唤醒词 | `"你好"` | `config.py::WAKE_WORD` |
| 匹配方式 | **严格正则前缀匹配** | `command_router.py::_is_wake_word()` |
| 匹配规则 | `^你好\s*[，。！？!?，、~～.·;；:：\-–—]*(\s*[啊呀呢吧哦]\s*[，。！？!?，、~～.·;；:：\-–—]*)*$` | — |
| ✅ 触发 | `"你好"`, `"你好。"`, `"你好！"`, `"你好？"`, `"你好啊"`, `"你好呀"`, `"你好呢"`, `"你好吧"`, `"你好哦"`, `"你好啊！"` | — |
| ❌ 不触发 | `"你好，帮我查天气"`, `"你好，今天天气怎么样"`, `"你好帮我查天气"`, `"帮我查天气"` | — |
| 唤醒回复语 | `"我在，请说"` | `config.py::WAKE_WORD_REPLY` |
| 触发行为（SLEEPING） | `_wake_activated = True`，回复确认语，不经过 LLM | `wifi_server.py::_handle_rptf()` L735-743 |
| 触发行为（ACTIVE） | 回复确认语，不经过 LLM | 同上 L776-786 |
| 是否经过 LLM | ❌ | — |
| 状态 | ✅ 2026-09-26 实测通过 | 12 条单元测试 10 通过（2 因环境无 edge_tts 跳过，与本次改动无关） |

**设计动机**：
- "大聪明"在中文日常语境中容易出现在非唤醒场景（如聊天中提及），改为"你好"更符合中文自然交互入口。
- "你好"是超短词，容易与后续问题黏连（如"你好，帮我查天气"），因此不能简单包含匹配。
- 严格正则保证：只匹配"你好"本体 + 可选语气词，一旦出现实质性内容（"帮我查"、"今天"等）就落到 USER_TEXT。

---

## 8. Interrupt / "停"

### 8.1 双层设计

```
第一层（ESP32 端，实时）：
  checkPlaybackInterrupt() — 能量检测
  → 播放期间持续监测麦克风 RMS
  → RMS >= INTERRUPT_RMS_THRESHOLD (1200) → 立即停止 TTS 播放
  → 宽限期 INTERRUPT_GRACE_MS (500ms) 内跳过检测
  → 整数平方根算法（二分搜索，无浮点）
  → 返回 true → playPCM() 写入静音并退出

第二层（PC 端，ASR 后确认）：
  CommandRouter.classify(user_text) → INTERRUPT
  → 不调用 LLM，不发 TTS
  → 发送 _send_empty_play() 解锁 ESP32
```

### 8.2 参数

| 参数 | 值 | 来源 |
|------|-----|------|
| 打断词列表 | `["停", "停止", "别说了", "等一下", "闭嘴"]` | `config.py::INTERRUPT_WORDS` |
| 匹配方式 | 包含匹配 | `command_router.py::classify()` |
| ESP32 能量阈值 | `INTERRUPT_RMS_THRESHOLD = 1200` | `main.cpp` |
| ESP32 宽限期 | `INTERRUPT_GRACE_MS = 500` | `main.cpp` |
| ESP32 检测算法 | AC RMS，每批 256 样本，整数二分搜索 sqrt | `checkPlaybackInterrupt()` |
| 实测依据 | 静默≈100~140，回声≈100~500，正常说话≈200~900，喊"停"≈800~2000+ | `main.cpp` 注释 |

### 8.3 完整流程

```
用户说"停" →
  [ESP32] checkPlaybackInterrupt() 检测到 RMS >= 1200 →
    playPCM() 写入静音到 I2S → 停止播放 →
    notifyPlaybackDone() → 500ms cooldown →
    新一轮录音开始 →
  [PC]   ASR 识别出"停" → CommandRouter.classify → INTERRUPT →
    不发 TTS → _send_empty_play() → 解锁 ESP32
```

### 8.4 当前状态：**可用性不好，暂不优化**

**实测问题**：用户希望 AI 播放 TTS 时说"停"能立刻打断，但当前实现的响应明显滞后。

**根因排序**（详见 [`barge-in-known-issues.md`](./barge-in-known-issues.md)）：

| # | 问题 | 严重程度 |
|---|------|----------|
| 1 | `checkPlaybackInterrupt()` 每 128ms 才调用一次（挂在 `playPCM()` chunk 循环末尾） | 🔴 高 |
| 2 | `INTERRUPT_GRACE_MS = 500` 过长，前 500ms 完全不打断 | 🔴 高 |
| 3 | 阈值 `INTERRUPT_RMS_THRESHOLD = 1200` 高于用户正常说话幅度（rms 200-900） | 🟠 中 |
| 4 | `i2s_write(... portMAX_DELAY)` 阻塞，无法在 mid-chunk 打断 | 🟠 中 |
| 5 | 无 AEC（回声消除），TTS 回声与人声混叠 | 🟡 长期 |

**语义链路状态**：PC 端 `wifi_server.py::_handle_rptf()` 的 INTERRUPT 分支和 `CommandRouter` 的包含匹配逻辑均正确（12 条单元测试中 `test_tc04_active_interrupt` / `test_stop_never_goes_to_llm` / `test_prolonged_speaking_no_false_interrupt` 均通过）。问题在 ESP32 端检测漏检，导致 PC 端往往根本收不到打断后的录音。

**本轮 commit 决定**：不做 barge-in 优化，作为后续任务。推荐的最小修改组合（未在本 commit 应用）：
- `INTERRUPT_RMS_THRESHOLD: 1200 → 900`
- `INTERRUPT_GRACE_MS: 500 → 150`
- 增加 grace 期内每 100ms 的 RMS 日志用于现场确认

详见 [`barge-in-known-issues.md`](./barge-in-known-issues.md) 获取完整诊断报告和后续任务清单。

---

## 9. LLM Router

位置：`pc/llm.py`

```python
class LLMRouter:
    def __init__(self, engine=None):
        # engine 为 None 时使用 config.DEFAULT_LLM_ENGINE
    def chat(self, text) -> Optional[str]
```

### 9.1 可用引擎

| 引擎 | 默认模型 | URL | 超时 |
|------|---------|-----|------|
| SenseNova | `sensenova-6.8-flash-lite` | `https://token.sensenova.cn/v1/chat/completions` | 60s |
| Ollama | `qwen2.5:7b` | `http://localhost:11434` | 120s |
| Gemini | `Gemini 3.1 Flash Lite` | `https://generativelanguage.googleapis.com/v1beta` | 60s |

**默认引擎**：`DEFAULT_LLM_ENGINE = "gemini"`（可通过环境变量覆盖）

### 9.2 系统提示词

`SYSTEM_PROMPT` 在 `config.py` 中定义，可通过环境变量 `SYSTEM_PROMPT` 覆盖。核心要求：回复简短口语化（1-5 句），不使用 Markdown，像朋友聊天。

---

## 10. ASR / TTS

### 10.1 ASR（Whisper.cpp）

| 参数 | 值 | 来源 |
|------|-----|------|
| 工具 | whisper.cpp CLI | `config.py::WHISPER_CLI` |
| 模型 | `ggml-base.bin` | `config.py::WHISPER_MODEL` |
| 超时 | 120 秒 | `config.py::ASR_TIMEOUT` |
| 预处理 | `_trim_wav_for_asr()` | `voice_pipeline.py` |
| 预处理参数 | PRETRIM_MIN_RMS=260, PRETRIM_MAX_DURATION_SEC=6.0 | `config.py` |
| 调用方式 | 子进程 | `asr.py::WhisperASR._run_whisper()` |

### 10.2 TTS（Edge TTS）

| 参数 | 值 | 来源 |
|------|-----|------|
| 音色 | `zh-CN-XiaoxiaoNeural` | `config.py::TTS_VOICE` |
| 语速 | `-10%` | `config.py::TTS_RATE` |
| 音调 | `-1Hz` | `config.py::TTS_PITCH` |
| 输出格式 | MP3 → WAV (pydub + ffmpeg) | `tts.py::_mp3_to_wav()` |
| 调用方式 | edge-tts WebSocket | `tts.py::TTSEngine._text_to_mp3()` |

### 10.3 流水线

```python
# 完整流水线（包含 ASR）
def pipeline(wav_bytes, llm_engine=None):
    user_text = transcribe(wav_bytes)       # Whisper
    llm = LLMRouter(engine=llm_engine)
    reply = llm.chat(user_text)
    return synthesize(reply)                # Edge TTS

# 纯文本流水线（跳过 ASR，由 wifi_server 预调用 transcribe）
def pipeline_text(user_text, llm_engine=None):
    llm = LLMRouter(engine=llm_engine)
    reply = llm.chat(user_text)
    return synthesize(reply)
```

**重要**：`pipeline_text()` 内部**不调用** `transcribe()`。`wifi_server.py::_handle_rptf()` 中已先调用 `transcribe(wav_bytes)` 获取 `user_text`，再传入 `pipeline_text(user_text)` 跳过重复 ASR。

---

## 11. TCP 协议

### 11.1 下行（PC → ESP32）

```
PLAY | u32 size | chunk₁ | ACK | chunk₂ | ACK | ... | chunkₙ
```

| 字段 | 大小 | 说明 |
|------|------|------|
| `PLAY` | 4 bytes | `{'P','L','A','Y'}` |
| `size` | 4 bytes | u32 little-endian，PCM 总字节数 |
| `chunk` | ≤4096 bytes | PCM 数据块 |
| `ACK` | 3 bytes | `{'A','C','K'}`，每 chunk 后回发 |

### 11.2 上行（ESP32 → PC）

```
RECM | u16 flags | u32 chunk_size | PCM (≤4096)  ×N
RPTF | u32 total_size
```

| 字段 | 大小 | 说明 |
|------|------|------|
| `RECM` | 4 bytes | `{'R','E','C','M'}` |
| `flags` | 2 bytes | u16 little-endian |
| `chunk_size` | 4 bytes | u32 little-endian |
| `PCM` | ≤4096 bytes | int16 mono 16kHz |
| `RPTF` | 4 bytes | `{'R','P','T','F'}`，段末触发 |
| `total_size` | 4 bytes | 本段录音 PCM 总字节数 |

**RECM flags**：
- `REC_FLAG_FIRST` = 0x0001：该段录音首包
- `REC_FLAG_VAD_TRIGGER` = 0x0002：VAD 触发点

### 11.3 播放完成信号

- ESP32 播放完全部 PLAY chunk 后，由 `main.cpp::loop()` 调用 `g_uploader.notifyPlaybackDone()`
- 如果无 TTS 回复，PC 发送 `PLAY | u32 size=0`（空 PLAY），ESP32 的 `playPCM(0)` 不播放任何内容，直接完成并解锁

### 11.4 字节序

全部 little-endian。`frame.h` 中 `packU16()` / `packU32()` 实现。

### 11.5 分块大小

`FRAME_CHUNK_SIZE` = `CHUNK_SIZE` = 4096 bytes（两端一致）。

---

## 12. 错误处理与状态恢复

### 12.1 ESP32 端

| 场景 | 行为 |
|------|------|
| Wi-Fi 断开 | `WifiClient::run()` 每 3 秒重试 `WiFi.begin()` |
| TCP 连接被 PC 侧关闭（FIN） | **P0 修复**：`WifiClient::run()` 每 5s 调用 `_client.connected()` 做低频探针，检测到异常后 `_tcpEstablished=false` + `stop()`，3s 内触发重连 |
| TCP 读写错误 | `WifiClient::read()/write()` 返回异常时 `_tcpEstablished=false`，`run()` 每 3 秒重试 `connect()` |
| VAD 静音超时 | `silenceMs=700ms` 后触发 `VAD_END`，发送 RPTF |
| 播放中断（用户说"停"） | `checkPlaybackInterrupt()` 返回 true → 写入静音 → 退出 playPCM。**当前可靠性不佳**，详见 §8.4 |
| 播放期间 | `MicUploader._waitingForPlayback=true`，丢弃所有麦克风数据 |
| 播放后 cooldown | 500ms 内丢弃数据，`clearPreRoll()` 清空 pre-roll 缓冲 |
| 下行门控 | `g_wifi.available() < 4` 时不调用 `receiveBytes()`，避免阻塞 VAD |
| 未知命令 | 打印日志，`delay(1)` 返回 |

### 12.2 PC 端

| 场景 | 行为 |
|------|------|
| TCP 连接断开 | `ClientSession._recv_exact()` 捕获异常 → session 退出 → `AudioServer` 继续 accept |
| ASR 失败 | `transcribe()` 返回 None → 丢弃，不发 LLM |
| LLM 失败 | `pipeline_text()` 返回 None → `_send_empty_play()` 解锁 ESP32 |
| TTS 失败 | `synthesize()` 返回 None → `_send_empty_play()` 解锁 ESP32 |
| SLEEPING 非唤醒词 | 丢弃 → `_send_empty_play()` |
| 所有无 TTS 路径 | 均调用 `_send_empty_play()`，确保 ESP32 不进入死锁 |

### 12.3 ESP32 录音状态机

```
IDLE
  ↓ (VAD_IDLE: pushPreRoll，积累 250ms)
RECORDING (VAD_SPEAK)
  ↓ (首次: 排空 pre-roll with FIRST+VAD_TRIGGER flag，clearPreRoll)
  ↓ (每 100ms: sendRecChunk with accumulation buffer)
RPTF (VAD_END)
  ↓ (发送尾 chunk + RPTF)
WAITING_FOR_PLAYBACK
  ↓ (discardMicSamples)
PLAYBACK (由 main.cpp::playPCM 执行)
  ↓
COOLDOWN (500ms)
  ↓ (discardMicSamples + clearPreRoll)
IDLE
```

### 12.4 Pre-roll 机制

| 参数 | 值 |
|------|-----|
| 时长 | `VAD_PRE_ROLL_MS = 250` ms |
| 样本数 | `VAD_PRE_ROLL_SAMPLES = 4000` (16000 × 250 / 1000) |
| 缓冲区 | `preRollRing[4000]` 静态数组 |
| 写入时机 | `VAD_IDLE` 状态：`pushPreRoll(samples, n)` |
| 排空时机 | `VAD_SPEAK` 首次 session：`fillPreRollSlice()` 一次性排空 |
| 清除时机 | 1. 排空后 `clearPreRoll()`；2. cooldown 完成后 `clearPreRoll()` |
| 首包标志 | `REC_FLAG_FIRST \| REC_FLAG_VAD_TRIGGER` |

**关键保证**：
- Pre-roll 数据只发送一次（排空后立即 `clearPreRoll()`）
- 不会跨 session 残留（cooldown 结束后再次 `clearPreRoll()`）
- VAD_IDLE 期间持续填充环形缓冲，最近 250ms 的音频不会丢失

---

## 13. 测试状态

### 13.1 单元测试

文件：`pc/tests/test_command_router.py`

12 个测试函数覆盖会话控制层的核心场景。**本地执行 `python tests/test_command_router.py`** 时，25/25 内嵌断言全部通过。

| 测试组 | 说明 | 状态 |
|--------|------|------|
| TC-01: SLEEPING + 唤醒词 | "你好" → WAKE_WORD → activate | ✅ PASS |
| TC-02: SLEEPING + 普通文本 | "帮我查天气" → 忽略 | ✅ PASS |
| TC-03: ACTIVE + 正常提问 | USER_TEXT → LLM → TTS | ✅ PASS |
| TC-04: ACTIVE + 打断词 | "停" → INTERRUPT → 无 TTS | ✅ PASS |
| TC-05: ACTIVE + 唤醒词 | "你好" → 回复确认语 | ✅ PASS |
| TC-06: ACTIVE + 普通文本 | 一般问答 | ✅ PASS |
| TC-07: 无自触发 | AI 回声不误触发新录音 | ⚠️ SKIP（环境无 `edge_tts`，与本次改动无关） |
| TC-08: 空 PLAY 路径 | 无 TTS 时解锁 ESP32 | ✅ PASS |
| TC-09: classify 边界情况 | 覆盖 17 条文本 → 11 WAKE_WORD + 6 USER_TEXT 边界 | ✅ PASS |
| TC-10: 无重复 ASR | `pipeline_text` 不重复调用 Whisper | ⚠️ SKIP（环境无 `edge_tts`） |
| TC-11: 打断词不进 LLM | "停" / "停止" 从未经过 `llm.chat()` | ✅ PASS |
| TC-12: 长时间说话不误触发 | 正常长句不误判为打断 | ✅ PASS |

**Wake Word「你好」专项覆盖**（TC-09）：
- WAKE_WORD 应触发（11 条）：`你好` / `你好。` / `你好！` / `你好？` / `你好啊` / `你好呀` / `你好呢` / `你好吧` / `你好哦` / `你好啊！` / `你好呀。`
- USER_TEXT 应触发（6 条）：`你好，帮我查天气` / `你好，今天天气怎么样` / `你好，我想问个问题` / `你好帮我查天气` / `帮我查天气` / `今天怎么样`

### 13.2 编译验证

| 环境 | RAM 使用 | Flash 使用 | 状态 |
|------|---------|-----------|------|
| `esp32-s3-n16r8` | 9.9% | 5.4% | ✅ SUCCESS |
| `esp32-s3-n16r8-wifi` | 24.8% | 13.5% | ✅ SUCCESS |

### 13.3 未验证项

- ESP32 硬件联调（需要物理硬件）
- Wi-Fi 实际连接稳定性
- VAD 实际触发/静音判定
- ASR 识别准确率
- TTS 输出质量
- 端到端语音闭环

---

## 14. 当前折衷

### 14.1 能量 VAD（而非 WebRTC/Silero）

- **原因**：无第三方库依赖，ESP32 上纯 C++ 实现
- **代价**：在噪声环境下可能误触发
- **缓解**：250ms pre-roll + 700ms silence timeout + 双层打断

### 14.2 非流式 TTS（Edge TTS）

- **原因**：免费、无需本地模型、中文质量好
- **代价**：需要先完成整个 TTS 合成才能开始播放，首包延迟较高
- **替代方案**：后续可切换到 CosyVoice / GPT-SoVITS 本地方案

### 14.3 串口回退保留

- **原因**：烧录/日志/无 Wi-Fi 环境兜底
- **代价**：两个 PlatformIO 环境维护成本
- **架构影响**：通过 `FIRMWARE_MODE_WIFI` 宏隔离，Wi-Fi 代码不增加串口固件体积

### 14.4 CONFIG_MODE_BUTTON 当前 disabled

- **现象**：`loop()` 中 `if (false && !g_inConfigMode && checkConfigModeButton())` 被禁用
- **原因**：未确认原因。当前 Config Mode 仅在首次启动（无有效 NVS 配置时）自动进入
- **状态**：BOOT 键长按 3 秒进入 Config Mode 的代码路径完整存在，但被运行时禁用

### 14.5 舵机未接入语音控制

- **现象**：`servo_control` 模块仅在 `setup()` 中执行一次自测序列
- **状态**：`loop()` 中未调用任何舵机函数
- **计划**：属于未来功能（Pan/Tilt 视觉联动），当前未实现

### 14.6 LCD 显示

- **状态**：当前代码中无任何 LCD 模块或 OLED 支持
- **计划**：属于未来功能，当前未实现

---

## 15. 文件结构

```
esp32-voice-ai/
├── README.md                         # 入口 + 文档索引
├── config.local.json                 # 网络配置单一真相源（gitignored）
├── docs/
│   ├── architecture.md               # ← 你在这里
│   ├── overview.md                   # 项目概述
│   ├── protocol.md                   # 通信协议规范
│   ├── wiring.md                     # 硬件接线
│   ├── barge-in-known-issues.md      # Barge-in 已知问题与后续任务
│   ├── firmware.md                   # 固件开发
│   ├── network-config.md             # 网络配置与配对方案
│   ├── roadmap.md                    # 阶段路线
│   └── ...                           # 其他测试文档
├── firmware/
│   └── esp32/
│       ├── platformio.ini            # env:esp32-s3-n16r8 + env:esp32-s3-n16r8-wifi
│       ├── boards/
│       │   └── esp32-s3-n16r8.json
│       └── src/
│           ├── main.cpp              # 主循环 + setupI2S + playPCM + checkPlaybackInterrupt
│           ├── secrets.h             # Wi-Fi 凭据回退；include secrets.local.h
│           ├── protocol/
│           │   └── frame.h           # PROTO_PLAY/REC/RPTF/ACK + packU16/U32
│           ├── network/
│           │   ├── wifi_client.h
│           │   └── wifi_client.cpp   # STA + TCP Client + mDNS
│           ├── audio/
│           │   ├── mic_adc.h
│           │   ├── mic_adc.cpp       # MAX9814 → ADC1 GPIO1
│           │   ├── mic_uploader.h
│           │   └── mic_uploader.cpp  # VAD 状态机 + pre-roll + RECM/RPTF
│           ├── vad/
│           │   ├── energy_vad.h
│           │   └── energy_vad.cpp    # RMS 阈值 + 静音超时
│           ├── config/
│           │   ├── device_config.h
│           │   └── device_config.cpp # NVS 持久化
│           ├── web/
│           │   ├── config_web.h
│           │   ├── config_web.cpp    # SoftAP + captive portal
│           │   ├── robot_event_server.h
│           │   └── robot_event_server.cpp  # ESP32-CAM → /robot/event
│           ├── servo/
│           │   ├── servo_control.h
│           │   └── servo_control.cpp # MG90S 最小控制
│           └── assets/
│               └── hi_hello.h        # 内嵌 PCM 迎宾音频
├── pc/
│   ├── config.py                     # 配置中心
│   ├── wifi_server.py                # TCP Server（主入口）
│   ├── voice_pipeline.py             # ASR → LLM → TTS 流水线
│   ├── command_router.py             # 会话控制层
│   ├── asr.py / llm.py / tts.py     # ASR / LLM / TTS 实现
│   ├── voice_chat.py                 # 交互入口
│   ├── send_wav.py                   # 下行播放器
│   ├── transport.py / transport_wifi.py / transport_serial.py
│   ├── mic.py                        # PC 麦克风（回退）
│   ├── tests/
│   │   ├── __init__.py
│   │   └── test_command_router.py    # 12 单元测试
│   └── support/                      # 辅助脚本
├── scripts/
│   ├── gen_secrets.py                # config.local.json → secrets.local.h
│   ├── gen_hi_hello_pcm.py           # 生成迎宾 PCM
│   └── piopre.py                     # PlatformIO pre-build hook
└── third_party/                      # 第三方参考代码
    └── esp32cam_tflite_test/
```

---

## 16. 架构原则

### 16.1 ESP32 是 Audio I/O 终端，不是 AI 处理器

- 麦克风采集、VAD、分块上传在 ESP32
- ASR / LLM / TTS 在 PC
- Wi-Fi/TCP 是双向主链路，USB Serial 仅回退

### 16.2 会话控制层独立于 LLM

- `CommandRouter` 不依赖任何 LLM 实现
- 唤醒/打断逻辑在 LLM 调用之前完成
- 切换 LLM Provider（Ollama / SenseNova / Gemini）不影响唤醒/打断

### 16.3 一次 ASR 原则

- `wifi_server.py::_handle_rptf()` 中先调用 `transcribe()` 获取文本
- `CommandRouter.classify()` 对文本分类
- `USER_TEXT` 通过 `pipeline_text(user_text)` 调用 LLM（跳过重复 ASR）
- 整个对话轮次中 ASR 只执行一次

### 16.4 ESP32 状态机防死锁

- ESP32 发送 RPTF 后进入 `WAITING_FOR_PLAYBACK`
- PC 必须发送 PLAY（有内容或空 PLAY=0）解锁
- 所有"无 TTS"路径均调用 `_send_empty_play()`
- 不存在 ESP32 进入 `WAITING_FOR_PLAYBACK` 后无法解锁的路径

### 16.5 Pre-roll 不跨 session

- VAD_IDLE 期间持续填充 250ms 环形缓冲
- VAD_SPEAK 首次排空后立即 `clearPreRoll()`
- cooldown 结束后再次 `clearPreRoll()`
- 确保每次录音只包含当前语句，不残留前一轮数据

### 16.6 非阻塞优先

- `WifiClient::run()` 使用节流重试，不阻塞 loop
- `g_web.loopHTTP()` 和 `g_robotEvent.loop()` 异步非阻塞
- 下行门控 `g_wifi.available() < 4` 避免阻塞 VAD/录音
- 播放中断检测在 chunk 之间执行，不阻塞 I2S 写入

### 16.7 共享单一配置源

- `config.local.json` 同时被 PC `config.py` 和固件 `gen_secrets.py` 读取
- 网络配置修改只需改一处，无需手动同步

---

## 17. 开发规则

### 17.1 新增功能

1. **必须**先确认是否影响现有协议（`protocol.md`）
2. ESP32 端新增硬件模块时，确认 GPIO 不冲突
3. PC 端新增模块时，在 `config.py` 中添加对应配置项
4. 新增 LLM Provider 时，在 `llm.py::LLMRouter` 中添加 `_engine_chat()` 方法

### 17.2 修改现有功能

1. **禁止**修改已验证通过的 250ms pre-roll 参数
2. **禁止**在未更新 `protocol.md` 的情况下修改协议常量
3. **禁止**在未更新 `wiring.md` 的情况下修改 GPIO 分配
4. **禁止**在未更新测试的情况下修改 `CommandRouter.classify()` 逻辑
5. 修改 VAD 参数时，同步更新 `device_config.h` 中的默认值

### 17.3 编译验证

```bash
# Python 语法检查
python -m py_compile pc/config.py pc/wifi_server.py pc/voice_pipeline.py pc/command_router.py

# 单元测试
cd pc && python -m pytest tests/test_command_router.py -v

# ESP32 串口模式
pio run -e esp32-s3-n16r8

# ESP32 Wi-Fi 模式（主链路）
pio run -e esp32-s3-n16r8-wifi
```

### 17.4 不做的功能（当前阶段）

- LCD 显示（未实现）
- 舵机语音控制（仅自测，未接入语音链路）
- ESP32 端 LLM（算力不足，未实现）
- 流式 TTS（Edge TTS 不支持，未实现）
- WebRTC VAD（当前使用能量 VAD，未替换）
- BLE 通信（不使用）

### 17.5 文档维护

- 协议变更 → 更新 `docs/protocol.md`
- 硬件接线变更 → 更新 `docs/wiring.md`
- 固件编译/烧录流程变更 → 更新 `docs/firmware.md`
- 架构/模块变更 → 更新本文件
- 网络配置变更 → 更新 `docs/network-config.md`
- 阶段路线变更 → 更新 `docs/roadmap.md`

---

## 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1 | 2026-09-08 | 首版，从 README 与源码抽取 |
| v2 | 2026-09-09 | Phase 1 主链路（Wi-Fi/TCP + MAX9814 + RECM/RPTF + VAD）落地 |
| v3 | 2026-09-09 | 补充网络配置策略链接 |
| v4 | 2026-09-23 | 新增 Milestone 1 · 视觉 → 迎宾语音闭环归档 |
| v5 | 2026-09-26 | 基于实际代码状态全面重写：新增 P0/P1/P2 架构、CommandRouter/Session State、CommandRouter/打断双层设计、Pre-roll 机制、Wi-Fi/TCP 重连细节、错误处理与状态恢复、测试状态、当前折衷、开发规则 |
