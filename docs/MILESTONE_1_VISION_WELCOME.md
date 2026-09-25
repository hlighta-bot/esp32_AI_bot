# Milestone 1 · 视觉 → 迎宾语音闭环（已验收）

> 2026-09-23 · 版本 v1
>
> 本文档记录 **Milestone 1（Vision → Welcome）** 的验收结果、代码路径与下一阶段计划。
> 这是本项目第一个**端到端跨设备自动化**里程碑：**摄像头看到人 → 摄像头自己找到机器人主机 → 主机会意打招呼**。

---

## 0. 目录

1. 里程碑定义
2. 端到端验收流程
3. 关键代码路径
4. 关键技术决策（与故障复盘）
5. 开发原则（Milestone 1 之后继续遵守）
6. 下一阶段计划（Phase 2-7）
7. 已知边界与明确不做
8. 相关文档
9. 版本

---

## 1. 里程碑定义

**Milestone 1 = 视觉触发 → 迎宾语音（无人介入，跨设备自动化）**

```text
┌─────────────┐   TFLite Person   ┌────────────────┐   raw UDP mDNS   ┌────────────┐
│  ESP32-CAM  │ ─────────────────► │  person_event  │ ────────────────► │ ESP32-S3   │
│  (OV2640)   │                    │  = HTTP POST   │                   │ Wi-Fi Host │
└─────────────┘                    │  /robot/event  │                   │ .local     │
                                   └────────────────┘                   └─────┬──────┘
                                                                              │
                                                                            Play "你好！"
```

**验收标志（全部满足）**：

1. ESP32-CAM 上电后，无人为干预，能**独立**通过 Wi-Fi 完成：
   * TFLite Person vs NoPerson 推理（`detection_state` 3 帧去抖）
   * 触发一次 person_detected 事件
2. 事件触发后，ESP32-CAM 用 **raw UDP mDNS** 主动查询 `esp32-voice-ai.local` 的 A 记录
3. 得到 S3 的 IP 后，用 HTTP POST 到 `http://<S3_IP>/robot/event`，路径返回 **HTTP 200**
4. ESP32-S3 收到事件后，播放 **"你好！"** 音频
5. 播放完成后，麦克风进入冷却（cooldown），避免误触发下一轮

---

## 2. 端到端验收流程

### 2.1 触发场景

在 ESP32-CAM 摄像头视野中出现一个人（或类人区域候选），TFLite 判定 `person_score >= 阈值` 且连续 3 帧一致，进入 `PERSON_PRESENT` 状态。

### 2.2 触发后立即上报

```cpp
// detection_state::feed() 状态机：
// NO_PERSON → PERSON_PRESENT（第一帧 person_detected）
// 立即调用 robot_event::feed(true)
// → 立即调用 postEvent("person_detected")
```

**关键点**：`person_detected` 事件**不等 3 帧确认**——第一帧命中即上报，因为迎宾语义要求"人一下来就要打招呼"，不能等去抖。3 帧去抖只在**丢失**方向起作用（防止偶发误检触发迎宾，然后立即又发 person_lost）。

### 2.3 事件发送链路

```cpp
postEvent("person_detected")
  ├── resolveRobotIp()         // mDNS 解析 esp32-voice-ai → 192.168.0.10
  │     └── queryMdnsARecord() // raw UDP mDNS A query
  │           ├── udp_tx.begin(0)                    // 打开发送套接字
  │           ├── udp_tx.beginPacket(224.0.0.251, 5353)
  │           ├── udp_tx.write(query)  // TXID=0xC0FF, QTYPE=A, QCLASS=IN
  │           ├── udp_tx.endPacket()
  │           └── udp_tx.stop()                // 关键：先 stop 再 open rx
  │           ├── udp_rx_u.begin(5353)          // 单播接收
  │           ├── udp_rx_m.beginMulticast(224.0.0.251, 5353)
  │           └── readPacketsWithTimeout(2000ms)
  │                 └── findARecord()           // 解析 CLASS=(val&0x7FFF)==0x0001
  └── HTTPClient.post("/robot/event", json)     // 期望 HTTP 200
```

### 2.4 S3 端处理

```cpp
// robot_event_server.cpp
POST /robot/event
  ├── parse JSON: {"event":"person_detected"}
  ├── state machine:
  │    NOT_PRESENT → PRESENT  → play "你好！" → cooldown
  │    PRESENT     → PRESENT  → 忽略（已迎宾）
  │    PRESENT → cooldown → NOT_PRESENT
  └── return 200
```

### 2.5 实机验证日志（已归档）

**ESP32-CAM 端**：

```text
[PERSON] score=79 → PERSON_PRESENT
[ROBOT] PERSON detected (immediate)
[ROBOT] event=person_detected
[ROBOT] mDNS query start: esp32-voice-ai.local
[ROBOT] mDNS TX: 38 bytes
[ROBOT] mDNS listening: unicast=OK multicast=OK
[ROBOT] mDNS RX: 48 bytes from 192.168.0.10:5353
[ROBOT] mDNS answer: 192.168.0.10 (TTL=120 RDLEN=4, at offset 12)
[ROBOT] mDNS resolved: esp32-voice-ai -> 192.168.0.10
[ROBOT] HTTP POST /robot/event → 200
```

**ESP32-S3 端**：

```text
[WIFI] connected, IP=192.168.0.10
[MDNS] service added: esp32-voice-ai
[ROBOT-EVT] POST /robot/event body={"event":"person_detected"}
[ROBOT-EVT] state: NOT_PRESENT → PRESENT
[PLAY] playing hello.wav (3512 bytes, 219 ms)
[PLAY] playback done
[MIC] cooldown start
```

---

## 3. 关键代码路径

### 3.1 ESP32-CAM 侧（`third_party/esp32cam_tflite_test`，独立 git 仓库）

| 文件 | 职责 |
|---|---|
| `src/person_detection_ESP32-Camera.cpp` | TFLite 推理主循环 + 3 状态去抖 + 触发 `robot_event::feed()` |
| `src/detection_state.cpp` / `.h` | NO_PERSON / PERSON_PRESENT / LOST_CANDIDATE 三态机 |
| `src/robot_event.cpp` | mDNS 解析 + HTTP POST + IP 缓存（60s TTL） |
| `src/robot_event.h` | `ROBOT_SERVER_HOST="esp32-voice-ai"`, `ROBOT_MDNS_TIMEOUT_MS=2000` |
| `src/web_server.cpp` | 独立 HTTP 服务（供调试用） |
| `src/camera_mutex.h` | 摄像头互斥锁 |

**注意**：`third_party/esp32cam_tflite_test` 是一个**独立的 git 仓库**，通过 gitlink 挂载到本项目（不是 submodule，未配置 `.gitmodules`）。Milestone 1 的 CAM 侧代码修改需要在该子仓库内单独 commit。

### 3.2 ESP32-S3 侧（本项目 `firmware/esp32/`）

| 文件 | 职责 |
|---|---|
| `src/web/robot_event_server.cpp` | HTTP 服务器，`POST /robot/event` 端点 |
| `src/web/robot_event_server.h` | 状态机：`NOT_PRESENT` / `PRESENT` / `COOLDOWN` |
| `src/main.cpp` | 状态机驱动 → 播放 `hi_hello.h` 内嵌音频 |
| `src/assets/hi_hello.h` | "你好！" PCM 音频资源（编译时嵌入） |
| `src/network/wifi_client.cpp` | Wi-Fi STA + `MDNS.addService()` |

### 3.3 PC 侧

Milestone 1 中 PC **不参与**迎宾链路（CAM 直接找 S3）。PC 仅在调试期提供串口监视与日志汇聚。

---

## 4. 关键技术决策（与故障复盘）

### 4.1 为什么 CAM 用 raw UDP mDNS 而不是 `MDNS.queryHost()`

- ESP32-CAM 用的是经典 ESP32（非 S3），lwIP 版本较老，`MDNS.queryHost()` 存在解析不稳定的问题
- 实机日志显示 `MDNS.queryHost()` 在 Stage 1 诊断中稳定返回 FAILED，但同一时间 raw UDP 直发 A query 能收到应答
- 因此把 mDNS A query 完全用 raw UDP 手写（见 §3.1 `robot_event.cpp`）
- 优点：可控、日志详细、跨 lwIP 版本稳定

### 4.2 为什么 CLASS 匹配用 `(classV & 0x7FFF) == 0x0001`

- 实机抓包显示 mDNS 应答里 A 记录的 CLASS 字段为 `0x8001`（cache-flush bit 置位），不是普通的 `0x0001`
- 简单 `== 0x0001` 会漏掉所有合法 mDNS 应答
- 正确处理：屏蔽最高 bit 后比较，即 `(classV & 0x7FFF) == 0x0001`

### 4.3 为什么 UDP socket 顺序是 tx → stop → rx

- lwIP 在同一端口同时 open tx + rx 时，某些版本会出现 bind 冲突
- 验证通过的顺序：
  1. `udp_tx.begin(0)` → send → `udp_tx.stop()`
  2. **然后** 再 open `udp_rx_u.begin(5353)` 与 `udp_rx_m.beginMulticast(224.0.0.251, 5353)`
- 这套顺序已在 Stage 2 诊断中稳定复现

### 4.4 为什么 QNAME 编码要用 `snprintf("%s.local")`

**曾踩坑**：早期实现把 `host="esp32-voice-ai"` 和 `suffix=".local"` 分成两段循环编码，遇到 `.local` 时 `strchr(s, '.')` 返回指针到 index 0，导致 `seg_len=0`，直接 `return false`，**没有任何 UDP 包被发出**，日志里也没有任何 TX 记录。

**正确做法**：先 `snprintf(qname, "%s.local", host)` 拼成完整字符串，然后用同一个 `while (strchr(q, '.'))` 循环编码，天然处理多 label 名。

### 4.5 60s IP 缓存的意义

- mDNS 查询 + 应答 + 解析 ≈ 100-500 ms，如果每次 person_detected 都查一次，会占用宝贵的摄像头帧预算
- 60s 内重复事件直接复用缓存 IP
- TTL 到点自动清理，避免 S3 换 IP 后旧值不释放

### 4.6 状态机为什么"人一下来就迎宾，走掉才 debounce"

- 迎宾语义要求**低延迟**：不能等 3 帧再迎宾，用户会觉得"反应慢"
- 3 帧去抖只在**丢失**方向起作用：防止偶发误检触发迎宾，然后 100ms 内又发 `person_lost`
- 因此 `feed()` 内部：`is_person==true` 时第一帧命中就 fire；`is_person==false` 时进入 `LOST_CANDIDATE` 计数，连续 3 帧 false 才确认 lost

---

## 5. 开发原则（Milestone 1 之后继续遵守）

以下 10 条原则来自 Milestone 1 的开发经验教训，**继续有效，下一阶段（Phase 2-7）继续遵守**：

1. **不要修改已经验收通过的 TFLite kernel**
   - `third_party/esp32cam_tflite_test/.pio/libdeps/...` 下的模型内核代码视为只读
   - 应用层可以改（`src/`），模型层禁止改

2. **不要修改已经工作正常的 raw UDP mDNS**
   - 除非实机日志**明确指出**当前实现坏了
   - 优先在 Stage 1/2 诊断代码基础上增加新测试，不重构已有可用路径

3. **不要因为想消除测试日志就扩大改动范围**
   - Stage 1 / Stage 2 诊断代码保留在源文件中，作为未来故障排查的历史记录
   - 用条件宏 `#ifdef` 或运行时开关决定是否启用，不做删除

4. **每完成一个 Phase 独立实现 / 独立编译 / 独立测试 / 独立验收**
   - 不允许"一个 commit 顺便改 5 个 Phase"
   - 每个 Phase 结束前跑一次 `pio run` 确认无回归

5. **舵机路径：先独立测试 → 再机器人事件 → 再 LLM 意图**
   - 不要一开始就把舵机接进 LLM 对话回路
   - 分三步：
     1. 单测 MG90S：PWM 直驱、90° / 180° 行程、静态保持
     2. 机器人事件：`/robot/event` 增加 `pan=<deg>` / `tilt=<deg>` 字段
     3. LLM 意图：让 LLM 输出结构化指令，映射到舵机位置

6. **音频路径：先做 pre-roll，再做麦克风调参**
   - pre-roll 是**功能正确性**问题（不修好，唤醒词永远抓不全）
   - 麦克风调参是**体验质量**问题（可以后期迭代）
   - 顺序不能反

7. **ASR / LLM 错误靠日志区分**
   - 所有失败点日志格式统一为：
     * `[ASR] ...` / `[LLM] ...` / `[TTS] ...` / `[PLAY] ...`
   - 每个环节有独立的失败日志，不要只输出 "pipeline failed"

8. **不要同时修改多个核心模块**
   - 核心模块定义：`wifi_client.cpp` / `mic_uploader.cpp` / `robot_event_server.cpp` / `main.cpp`
   - 每次 commit 只触及 1-2 个核心模块

9. **每个 Phase 完成后单独 Git commit**
   - commit message 格式：`<type>: <scope> - <summary>`
   - 类型：`feat` / `fix` / `docs` / `refactor` / `test` / `chore`

10. **保持项目可回滚到 Milestone 1**
    - 每个 Phase 完成后打一个 tag（如 `v0.2-phase2-servo`）
    - 回滚路径：`git checkout v0.2-phase2-servo` 或 revert 相关 commit

---

## 6. 下一阶段计划（Phase 2-7）

按依赖关系与风险优先级排序。**每个 Phase 独立实现、独立编译、独立测试、独立验收、独立 commit**。

### Phase 2 · 舵机控制（Pan/Tilt）— 预计 1 周

**目标**：ESP32-S3 通过 MG90S 舵机实现机械头 Pan / Tilt。

**范围（先做前 3 项，第 4 项延后到 Phase 5+）**：

1. 独立硬件验证：MG90S 单舵机 PWM 驱动测试（GPIO4 / GPIO5，50Hz PWM）
2. 硬件稳定性：静态保持 30 秒、连续动作 100 次、电源塌陷测试
3. 协议扩展：`/robot/event` 增加 `{"event":"pan","deg":90}` / `{"event":"tilt","deg":45}`
4. ~~接入 LLM 意图路由~~ → 延后（见 Phase 5）

**不做**：

- 不接 LLM（LLM 输出结构化指令是 Phase 5 的事）
- 不做视觉追踪闭环（那是 S12-6 + 后续）

**关键文件（预计新增）**：

- `firmware/esp32/src/servo/pan_tilt_servo.cpp` / `.h`
- `firmware/esp32/src/servo/mg90s.h`

**验收标准**：

- 单独 PWM 测试代码编译烧录后，串口发送 `PAN=90` 舵机到 90°，`PAN=180` 舵机到 180°
- `/robot/event` POST `{"event":"pan","deg":45}` → HTTP 200 → 舵机到 45°

### Phase 3 · 唤醒词（Hi，大聪明）— 预计 2 周

**目标**：语音入口从"VAD 触发"进化为"唤醒词触发"。

**范围**：

1. **不做专用 Wake Word 模型**（不做 TinyML WakeNet / Picovoice）
2. 使用现有 VAD + Whisper.cpp 链路：
   * Energy VAD 抓到一段音频
   * 上传到 PC，Whisper ASR 识别
   * PC 端字符串匹配 `"Hi，大聪明"` / `"Hi 大聪明"` / `"嗨，大聪明"` 等变体
   * 命中 → 触发完整 ASR / LLM / TTS 流水线
   * 未命中 → 丢弃音频（可选：只保留唤醒词本身）
3. 优点：不引入新模型、不引入新依赖、复用现有 Whisper.cpp
4. 缺点：功耗稍高（每次 VAD 触发都要上传 + 识别）—— 可接受，Phase 4 优化

**不做**：

- 不部署 WakeNet / Picovoice 模型到 ESP32
- 不训练自定义 Wake Word 数据集

**关键文件（预计修改）**：

- `pc/wifi_server.py`（增加唤醒词匹配前置步骤）
- `pc/voice_pipeline.py`（增加 `match_wake_word(text)` 函数）
- `pc/config.py`（增加 `WAKE_WORD_PHRASES` 配置项）

**验收标准**：

- 说 "Hi，大聪明" → PC 日志出现 `[WW] wake word matched` → 进入对话
- 说其他话 → PC 日志出现 `[WW] wake word miss` → 丢弃

### Phase 4 · Audio Pre-Roll（前置音频缓冲）— 预计 1 周

**目标**：解决"唤醒词尾字被 VAD 截断"、"用户第一句话被吞"的问题。

**范围**：

1. 在 `mic_uploader.cpp` 增加环形缓冲区（ring buffer）
2. VAD 触发时，回读触发前 ~300 ms 的音频，与当前音频拼接后上传
3. 参数化：`PREROLL_MS=300`，可配置文件 / 编译宏调整
4. 与 Phase 3 唤醒词联动：唤醒词匹配命中 → 触发 pre-roll + 完整对话

**不做**：

- 不做双向流式 ASR（Phase 4 只做本地预缓冲，不做云端流式）
- 不做自适应 pre-roll（Phase 4 用固定 300ms）

**关键文件（预计修改）**：

- `firmware/esp32/src/audio/mic_uploader.cpp`
- `firmware/esp32/src/audio/mic_uploader.h`

**验收标准**：

- 说 "Hi，大聪明..." 时，PC 端收到的音频**包含**"Hi，大聪明"完整字样
- 对比 pre-roll 前后 Whisper 输出，命中率提升明显

### Phase 5 · 麦克风调参 — 预计 1 周

**目标**：提升实际使用体验，减少误触发 / 漏触发。

**范围**：

1. MAX9814 硬件增益选择（GAIN=11 / 18 / 26 / 34 dB，选 26 dB 起步）
2. ADC 峰值裁剪检测（防止削波导致 ASR 完全失败）
3. Energy VAD 阈值调整（基于实机数据，非拍脑袋）
4. 播放后 cooldown 时长调整（当前 500 ms，可能需要 800-1000 ms）
5. 静默期 / 语音期判定参数（Energy VAD 内部）

**顺序**（按风险从高到低）：

1. 先做 ADC 峰值裁剪检测（防止硬件问题掩盖软件问题）
2. 再做增益选择
3. 最后调 VAD 阈值

**关键文件（预计修改）**：

- `firmware/esp32/src/audio/mic_adc.cpp`（增益 + 峰值检测）
- `firmware/esp32/src/vad/energy_vad.cpp`（阈值）
- `firmware/esp32/src/main.cpp`（cooldown 时长）

**验收标准**：

- 说话距离 30 cm / 60 cm / 100 cm，Whisper 都能识别
- 环境噪音（空调、风扇）下不出现连续误触发

### Phase 6 · ASR / LLM 错误诊断 — 预计 3 天

**目标**：把"pipeline failed"从模糊日志变成精确日志。

**范围**：

1. 统一日志前缀：`[ASR]` / `[LLM]` / `[TTS]` / `[PLAY]` / `[WW]`
2. 每个环节独立的失败日志（不只是 "failed"，要写"为什么失败"）
3. 每次 ASR 记录：输入音频时长、Whisper 版本、识别耗时
4. 每次 LLM 记录：输入 token 数、引擎（SenseNova / Ollama / Gemini）、响应耗时
5. 每次 TTS 记录：文本长度、Edge TTS 语音、MP3→WAV 转换耗时
6. 增加 `[PIPELINE]` 顶层汇总日志

**不做**：

- 不做外部监控面板（Phase 6 只做日志，不做 Dashboard）
- 不做异步日志（Phase 6 保持同步阻塞式打印）

**关键文件（预计修改）**：

- `pc/asr.py`
- `pc/llm.py`
- `pc/tts.py`
- `pc/voice_pipeline.py`
- `pc/wifi_server.py`

**验收标准**：

- 一次失败能通过日志直接定位到 `[ASR] timeout` / `[LLM] api error` / `[TTS] network error` 等具体环节

### Phase 7 · 多语言 — 预计 1-2 周

**目标**：中英双语（未来可扩展日韩）。

**范围**：

1. Whisper 侧：ASR 自动检测语言（whisper.cpp 支持 `--language auto`）
2. LLM 侧：根据 ASR 识别出的语言动态切换 prompt 风格
3. TTS 侧：根据用户语言动态切换 Edge TTS 语音（`zh-CN-XiaoxiaoNeural` / `en-US-AriaNeural`）
4. 唤醒词：中/英两套唤醒词，任一命中都触发对话

**不做**：

- 不做语音翻译（不做 ASR 中文 → LLM 英文 → TTS 中文）
- 不做语言混合（一句话里中英夹杂暂不处理）

**关键文件（预计修改）**：

- `pc/config.py`（`LANGUAGE = "auto"` / `WAKE_WORD_PHRASES` 多语言）
- `pc/asr.py`（Whisper `--language auto`）
- `pc/tts.py`（根据语言路由到不同 Edge TTS 语音）
- `pc/llm.py`（根据语言注入不同 system prompt）

**验收标准**：

- 说中文 → 中文回复（Whisper 输出中文，LLM 中文回复，Edge TTS 中文语音）
- 说英文 → 英文回复（Whisper 输出英文，LLM 英文回复，Edge TTS 英文语音）

---

## 7. 已知边界与明确不做

### 7.1 硬件边界

- **ESP32-CAM 是经典 ESP32**（非 S3），320 KB SRAM，无 PSRAM，无法跑 YOLO / Face Detection 模型
- **ESP32-CAM 只做人形区域候选检测**，不做真正的人脸检测（Face Detection）、不做人脸识别（Face Recognition）
- **ESP32-S3 MAX9814 是模拟 MEMS 麦克风**，模拟信号，非 I2S 数字麦，后续可替换但当前保留

### 7.2 明确不做（不属于本项目）

- **HC-SR04 超声波 + ESP8266 + MG90S 舵机演示**：独立项目，不纳入本仓库
- **ESP32 本地跑 LLM**：见 [`roadmap.md`](./roadmap.md) §52，目标期不做
- **Face Recognition（S12-5）**：涉及隐私与合规，属于架构决策点，非近期目标
- **专用 Wake Word 模型（WakeNet / Picovoice）**：Phase 3 明确用 Whisper.cpp 匹配代替

### 7.3 Milestone 1 的边界（下一阶段不要破坏的）

- ✅ CAM → S3 通信走 Wi-Fi 优先，UART 备用（当前未启用 UART）
- ✅ hostname 统一为 `esp32-voice-ai`
- ✅ raw UDP mDNS 已验证可用，不要重新实现
- ✅ TFLite 3 帧去抖逻辑已验证，不要重构

---

## 8. 相关文档

- [`roadmap.md`](./roadmap.md) — 项目阶段划分与后续路线（Phase 2-7 详细）
- [`architecture.md`](./architecture.md) — 系统架构（新增 §10.7 描述 Milestone 1 架构）
- [`STEP_12_VISION_SERVO_PLAN.md`](./STEP_12_VISION_SERVO_PLAN.md) — 视觉支线硬件调查与实施计划
- [`STEP_12_2_A_FEASIBILITY.md`](./STEP_12_2_A_FEASIBILITY.md) — Step 12-2-A 可行性分析
- [`test-2026-09-16-step12-1-cam-base.md`](./test-2026-09-16-step12-1-cam-base.md) — Step 12-1 摄像头基础测试
- [`test-2026-09-19-step12-2-a-person-detect.md`](./test-2026-09-19-step12-2-a-person-detect.md) — Step 12-2-A 人物检测测试
- [`test-2026-09-09-phase1-wifi-voice-loop.md`](./test-2026-09-09-phase1-wifi-voice-loop.md) — Phase 1 Wi-Fi 语音闭环测试
- [`protocol.md`](./protocol.md) — 通信协议
- [`network-config.md`](./network-config.md) — 网络配置与 mDNS
- [`README.md`](../README.md) — 项目入口

---

## 9. 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1   | 2026-09-23 | 首版。Milestone 1 验收归档，Phase 2-7 计划与 10 条开发原则 |
