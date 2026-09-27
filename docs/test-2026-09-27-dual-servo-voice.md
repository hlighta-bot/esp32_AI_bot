# 测试用例 · 2026-09-27 · 双舵机语音控制（SVCO）

> **文档编号**：TC-20260927-DUAL-SERVO-VOICE
> **版本**：v1.0
> **适用代码**：双舵机语音控制（Pan=GPIO4 / Tilt=GPIO5 / SVCO 独立协议）
> **测试目标**：验证「语音 → Whisper → CommandRouter(SERVO_COMMAND) → SVCO → ESP32 ServoController」全链路；确认不干扰既有 Wake Word / VAD / Barge-in / LLM / TTS / TCP 重连路径
> **前置测试**：Phase 1 语音闭环（[`test-2026-09-09-phase1-wifi-voice-loop.md`](./test-2026-09-09-phase1-wifi-voice-loop.md)）通过
>
> **测试时长**：全流程约 40–60 分钟；每个 TC 独立，可按编号跳测
>
> **说明**：PC 端单元测试已在 CI 中通过 18/18（`pc/tests/test_command_router.py`）。本文件聚焦**硬件 + 端到端**验证。

---

## 0. 目录

| 编号 | 测试项 | 时长 | 依赖 |
|------|--------|------|------|
| TC-01 | 硬件接线与供电检查（双舵机） | 5 min | 无 |
| TC-02 | PC 端单元测试回归（18/18） | 2 min | 无 |
| TC-03 | PlatformIO 双 env 编译 | 3 min | TC-02 |
| TC-04 | 固件烧录 + 启动日志（无机械自测） | 3 min | TC-01 + TC-03 |
| TC-05 | 启动双轴回中验证 | 2 min | TC-04 |
| TC-06 | SVCO 帧结构（Wireshark/Netcat 抓包） | 5 min | TC-04 |
| TC-07 | **VOICE：向上 / 上 / 抬头（Tilt +10°）** | 3 min | TC-04 |
| TC-08 | **VOICE：向下 / 下 / 低头（Tilt −10°）** | 3 min | TC-04 |
| TC-09 | **VOICE：向左 / 左 / 左转（Pan −10°）** | 3 min | TC-04 |
| TC-10 | **VOICE：向右 / 右 / 右转（Pan +10°）** | 3 min | TC-04 |
| TC-11 | **VOICE：回中 / 回正 / 回到中间（双轴回中）** | 3 min | TC-04 |
| TC-12 | 边界：连续同向步进触顶（Tilt 120° / 60°） | 3 min | TC-07, TC-08 |
| TC-13 | 边界：连续反向步进触底 | 3 min | TC-12 |
| TC-14 | **端到端回归**：LLM 对话 + 舵机命令混合 | 8 min | TC-04 |
| TC-15 | 状态机：SLEEPING 下 SERVO 命令被忽略 | 3 min | TC-04 |
| TC-16 | 一次 ASR 原则：SERVO 不重复 ASR | 3 min | TC-04 |
| TC-17 | 无 LLM 调用：SERVO 不消耗 API 额度 | 3 min | TC-04 |
| TC-18 | 无 TTS：SERVO 不发声，不触发 Barge-in | 3 min | TC-04 |
| TC-19 | 负样本：非舵机短语不误触发 | 5 min | TC-04 |
| TC-20 | 打断优先级：`"停"` 优先于 `"左"` | 2 min | TC-04 |
| TC-21 | 断线自动重连后 SERVO 仍可用 | 3 min | TC-14 |
| TC-22 | 长时间稳定性（10 分钟连发） | 10 min | TC-14 |

**附录**：
- A · 参数速查
- B · 串口 / PC 日志速查
- C · SVCO 命令编码速查
- D · 常见故障排查
- E · 参考文档
- F · 执行记录模板
- G · 回归对比基线（Phase 1 vs 双舵机）

---

## 通用前置条件

```text
[ ] ESP32-S3 N16R8 主板（Phase 1 已验证可用）
[ ] MAX9814 麦克风 + MAX98357A I2S 功放 + 3W 扬声器（Phase 1 已接线）
[ ] 双 MG90S 舵机（两只）
[ ] 独立舵机电源 5V/6V，额定 ≥ 2A（双舵机同时堵转余量）
[ ] USB 数据线（能传数据）
[ ] PlatformIO 编译环境（VS Code 或 CLI）
[ ] Python 3.9+，已 pip install -r pc/requirements.txt
[ ] Whisper.cpp 模型 + edge-tts 可用
[ ] Wi-Fi 路由器 + config.local.json 已配置
[ ] 串口监视工具（minicom / tio / Putty / VS Code Serial Monitor）
[ ] 可选：Wireshark / nc -l 8888 用于抓包
```

---

## TC-01 · 硬件接线与供电检查（双舵机）

**目的**：确保两只 MG90S 各自接线正确、共用独立电源、共地无问题，避免运行时 ESP32 被拉低复位。

### 步骤

1. 目视检查接线（参考 [`docs/wiring.md`](./wiring.md)）：

   | 舵机 | 信号 | 电源 | 地 |
   |------|------|------|----|
   | **Pan（左右）** | GPIO 4 | 独立 5V/6V + | 共地 |
   | **Tilt（上下）** | GPIO 5 | 独立 5V/6V + | 共地 |

2. 万用表电压档：
   - ESP32 3V3 对 GND = 3.2–3.4 V
   - 舵机 5V 对 GND = 4.8–5.3 V
3. 万用表蜂鸣档：
   - 舵机 GND、ESP32 GND、MAX9814 GND、MAX98357A GND 全部互连
4. 断电状态下确认 GPIO 4 / GPIO 5 未错接到电源正极（否则上电瞬间会烧掉 ESP32）
5. 舵机机械结构：Pan 与 Tilt 分别能自由转到 60°~120° 范围（用手工推一下，无卡滞）

### 期望结果

```text
- ESP32 3V3 = 3.2–3.4 V
- 舵机 5V = 4.8–5.3 V
- 所有 GND 互连
- 断电状态下 GPIO4 / GPIO5 与 5V 无连通
- 两只舵机自由转动无机械阻力
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| ESP32 3V3 掉到 < 3 V | 舵机电源共地错误 / 未用独立电源 / 舵机接线虚接 |
| ESP32 上电复位 | 舵机 VCC 直接接到 ESP32 3V3（禁止） |
| 舵机完全不动 | 信号线接错 GPIO；或独立电源未打开 |
| 舵机上电抖动 | 未共地；或电源电流不够 |
| 舵机上电就跳到极限 | 固件未回中；应通过 TC-05 验证 |

---

## TC-02 · PC 端单元测试回归

**目的**：验证 `command_router.py` 的 SERVO 分类不会破坏既有 Wake Word / Interrupt / USER_TEXT 逻辑。

### 步骤

```bash
cd ~/projects/esp32-voice-ai/pc
python -m py_compile command_router.py config.py wifi_server.py
python tests/test_command_router.py
```

### 期望结果

```text
============================================================
Results: 18 passed, 0 failed, 18 total
============================================================
```

关键 SERVO 用例：
- `test_servo_command_basic` — 16 条短语全部命中 SERVO_COMMAND
- `test_servo_command_with_punct_tone` — "上啊"、"向左！" 等尾缀场景
- `test_servo_command_no_false_positive` — 22 条负样本不误触
- `test_servo_command_priority_vs_interrupt` — "停" 优先于 "左"
- `test_servo_active_state_flow` — ACTIVE 状态发 SVCO
- `test_servo_sleeping_state_ignored` — SLEEPING 状态丢弃 SERVO

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| `test_servo_command_basic` 失败 | `_SERVO_PATTERNS` 顺序错乱（长短语必须在前） |
| 唤醒词测试失败 | 检查 `_SUFFIX` 正则：应为 `({_PUNCT}|{_TAIL})*`（不是 `({_PUNCT}({_TAIL})?)*`） |
| `test_no_duplicate_asr` 失败 | SERVO 分支误调用了 `_handle_user_text()` |

---

## TC-03 · PlatformIO 双 env 编译

**目的**：确认固件能同时通过 Wi-Fi 主链路与串口回退两种构建。

### 步骤

```bash
cd ~/projects/esp32-voice-ai/firmware/esp32
pio run -e esp32-s3-n16r8-wifi
pio run -e esp32-s3-n16r8
```

### 期望结果

```text
[esp32-s3-n16r8-wifi] RAM:   [=       ]  24.8%   (used 53408 / 214680)
[esp32-s3-n16r8-wifi] Flash: [==      ]  13.6%   (used 711792 / 5242880)
[esp32-s3-n16r8-wifi] SUCCESS

[esp32-s3-n16r8]     RAM:   [=       ]   9.9%
[esp32-s3-n16r8]     Flash: [=       ]   5.4%
[esp32-s3-n16r8]     SUCCESS
```

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| `SERVO_H_PWM_CHANNEL` 未定义 | `servo_control.h` 头文件被旧版覆盖 |
| 链接失败 `undefined: servo_center_all` | `servo_control.cpp` 未编入；检查 platformio.ini `src_dir` |
| Flash > 100% | `hi_hello.h` 内嵌 PCM 未使用 `-Os` |

---

## TC-04 · 固件烧录 + 启动日志

**目的**：确认启动时执行 `servo_init() → servo_center_all()`（**不再执行**机械自测序列）。

### 步骤

1. 烧录：
   ```bash
   cd ~/projects/esp32-voice-ai/firmware/esp32
   pio run -e esp32-s3-n16r8-wifi --target upload
   ```
2. 打开串口监视（921600 baud）
3. 上电 ESP32，观察日志

### 期望结果

关键行（顺序）：
```text
[config] ...
[servo] init: LEDC ch0 on GPIO5 (Tilt), ch1 on GPIO4 (Pan), 50Hz 13bit
[servo] axis TILT center angle=90 duty=200
[servo] axis PAN  center angle=90 duty=200
[wifi] connecting to <SSID> ...
[wifi] connected, ip=192.168.x.x
TCP connected to <PC_IP>:8888
READY
```

**必须看不到**（自测序列已移除）：
```text
❌ [servo] TEST: 60°
❌ [servo] TEST: 90°
❌ [servo] TEST: 120°
❌ [servo] test_sequence done
```

**两只舵机物理现象**：上电后 500 ms 内同时回到中间位置，之后不再动作。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 无 `[servo] init` 日志 | `servo_init()` 未被 setup() 调用；检查 main.cpp |
| 只出现 TILT 中心，无 PAN | `SERVO_H_SIGNAL_GPIO` 常量错或被注释 |
| 上电仍看到 TEST 序列 | main.cpp 里 `servo_run_test_sequence()` 未替换为 `servo_init()` |
| 舵机不动 | GPIO4/GPIO5 接线错；或 LEDC 通道冲突 |

---

## TC-05 · 启动双轴回中验证

**目的**：验证启动后 Pan / Tilt 均处于 90°（正中位），为后续步进提供基准。

### 步骤

1. 上电，等 500 ms
2. 目视观察：Pan 舵机（水平臂）应指向"正前方"，Tilt 舵机（垂直臂）应"水平朝前"
3. 串口日志应有两条 `[servo] axis TILT center` + `[servo] axis PAN center`

### 期望结果

- 两只舵机均处于 90°（各自机械结构的中位）
- 串口日志出现两条 center 打印
- **无**任何自测摆动

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 一只到位、一只偏离 | 舵机机械安装位不一致；或信号线接反（可交换 GPIO4/5 测试） |
| 上电后舵机抖动多次 | `servo_init()` 里 LEDC 未 attach 就写了 duty |

---

## TC-06 · SVCO 帧结构（抓包验证）

**目的**：确认 PC 发出的 SVCO 帧符合协议规范（`SVCO | u8 cmd | i16 param (LE)`，共 7 字节）。

### 步骤

**方案 A：nc -l 抓包（最快）**
```bash
nc -l -p 8888 | xxd
```
然后在 ESP32 端用 telnet 或专用脚本发一条 SVCO 让 ESP32 回读（或直接走语音触发）。

**方案 B：Wireshark**
1. 抓 TCP port 8888
2. 触发一次语音 "向上"
3. 在 Wireshark 里过滤 `tcp.port == 8888`
4. 找到 `SVCO...` 帧，查看 payload hex

### 期望结果

**期望字节序列**（以"向上"为例，SERVO_CMD_VERTICAL_UP = 0x01）：
```text
53 56 43 4F  01  00 00
S  V  C  O    cmd param(LE)
```

对每条语音命令：
| 语音 | cmd hex | 完整 SVCO 帧 |
|------|---------|-------------|
| 向上 / 抬头 | 0x01 | `SVCO\x01\x00\x00` |
| 向下 / 低头 | 0x02 | `SVCO\x02\x00\x00` |
| 向左 / 左转 | 0x11 | `SVCO\x11\x00\x00` |
| 向右 / 右转 | 0x12 | `SVCO\x12\x00\x00` |
| 回中 / 回正 / 回到中间 | 0x2F | `SVCO\x2F\x00\x00` |

**SVCO 后紧跟**一个 `PLAY | u32 size=0` 空帧（8 字节，用于解锁 ESP32 WAITING_FOR_PLAYBACK）。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| SVCO 后无空 PLAY | `wifi_server.py::_handle_rptf()` 未继续走 else 分支 |
| param 不是 0x00 0x00 | `struct.pack("<Bh", command, 0)` 参数错 |
| SVCO 塞进了 RPTF payload | 违反"独立协议"约束；SVCO 应在 ASR 之后独立 send，不能附到 RECM |

---

## TC-07 · VOICE：向上（Tilt 方向已修正 · 2026-09-27）

**目的**：核心功能验证——语音说"向上"后 Tilt 舵机朝上偏 10°。

> ⚠️ 硬件实测结论：Vertical (Tilt, GPIO5) 舵机的接线方向与 Pan 相反，
> 因此 ESP32 端 `handleServoCommand()` 中对 VERTICAL_UP / VERTICAL_DOWN
> 采用**反向步进**（UP → `-SERVO_STEP_DEG`，DOWN → `+SERVO_STEP_DEG`），
> Pan 保持原符号。这样最终物理方向才能满足"向上=朝天花板、向下=朝地"。
> 详见 `firmware/esp32/src/main.cpp::handleServoCommand()` 中的注释。

### 步骤

1. 唤醒："你好" → 系统回复"我在，请说"
2. 说：`向上` 或 `上` 或 `抬头`
3. 观察 Tilt 舵机

### 期望结果

**PC 端日志**：
```text
[asr] transcribe: "向上"
[router] type=SERVO_COMMAND servo_command=0x01
[wifi_server] SVCO cmd=0x01
[wifi_server] sending empty PLAY (size=0) to unlock ESP32
```

**ESP32 端串口日志**：
```text
[servo-cmd] VERTICAL_UP
[servo] vertical 90 -> 80
```

**现场验证结果（2026-09-27）**：
- ✅ **方向已修正**：说"向上"→ 舵机朝天花板偏转
- ✅ CommandRouter / SVCO / wifi_server 链路正常

**物理现象**：
- Tilt 舵机向上（朝天花板）偏转约 10°
- 扬声器**无声音**（不触发 TTS）
- 系统处于 ACTIVE 状态，可继续下一句语音

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 无 `[router] type=SERVO_COMMAND` | Whisper 识别成别的字；检查 `_SERVO_PATTERNS` 是否包含识别结果 |
| 有 SVCO 但舵机不动 | GPIO4/GPIO5 接反（此时说"向上"可能动 Pan） |
| 舵机朝下而非朝上 | `main.cpp::handleServoCommand()` 中 UP/DOWN 的 `+/-` 符号又反了 |
| 舵机动了但系统回复了 TTS 内容 | SERVO 分支误进入 USER_TEXT；检查 `wifi_server._handle_rptf()` |

---

## TC-08 · VOICE：向下（Tilt 方向已修正 · 2026-09-27）

### 步骤

1. 先让 Tilt 处于 90°（回中）
2. 说：`向下` 或 `下` 或 `低头`
3. 观察 Tilt

### 期望结果

- 串口：`[servo-cmd] VERTICAL_DOWN` / `[servo] vertical 90 -> 100`
- 物理：Tilt 朝下偏 10°（朝地）
- 无 TTS

### 排查思路

参见 TC-07。反向步进与正向步进应完全对称。

---

## TC-09 · VOICE：向左（Pan −10°）

### 步骤

1. 唤醒
2. 说：`向左` 或 `左` 或 `左转`
3. 观察 Pan

### 期望结果

- SVCO cmd=`0x11`
- 串口：`[servo-cmd] HORIZONTAL_LEFT` / `[servo] axis PAN step -10 -> 80`
- 物理：Pan 向左偏 10°（从上方俯视逆时针）
- 无 TTS

---

## TC-10 · VOICE：向右（Pan +10°）

### 期望结果

- SVCO cmd=`0x12`
- 串口：`[servo-cmd] HORIZONTAL_RIGHT` / `[servo] axis PAN step +10 -> 100`
- 物理：Pan 向右偏 10°

---

## TC-11 · VOICE：回中（双轴回 90°）

### 步骤

1. 先让 Pan、Tilt 均偏离中位（比如先说"向上"再"向右"）
2. 说：`回中` 或 `回正` 或 `回到中间`
3. 观察两只舵机

### 期望结果

- SVCO cmd=`0x2F`
- 串口：
  ```text
  [servo-cmd] CENTER_ALL
  [servo] axis TILT set 90
  [servo] axis PAN  set 90
  ```
- 物理：两只舵机**同时**回到 90° 中位
- 无 TTS

---

## TC-12 · 边界：连续同向步进触顶

**目的**：验证 ESP32 的角度状态机硬限幅（60°~120°），防止超范围 PWM 脉冲损坏舵机。

### 步骤

1. 先"回中"到 90°
2. 连续 10 次快速说"向上"（或直接发 SVCO 测试）
3. 观察串口日志

### 期望结果

Tilt 角度序列：`100 → 110 → 120 → 120 → 120 → 120 → ...`

日志会出现：
```text
[servo] axis TILT step +10 -> 100
[servo] axis TILT step +10 -> 110
[servo] axis TILT step +10 -> 120
[servo] vertical limit reached: 120
[servo] vertical limit reached: 120
...
```

**物理**：舵机在 120° 后停止运动（可能微微"啃一下"是正常舵机行为）；**不**继续向 130° 推进。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 连续步进后舵机继续超范围 | `servo_step_vertical()` 未 clamp；应写 `newAngle = min(newAngle, SERVO_MAX_ANGLE)` |
| 出现 duty > 275 | `angleToDuty()` 溢出 |

---

## TC-13 · 边界：连续反向步进触底

### 步骤

- 连续 10 次"向左"（Pan −10°）

### 期望结果

Pan 序列：`80 → 70 → 60 → 60 → ...`，触发 `[servo] horizontal limit reached: 60`

---

## TC-14 · 端到端回归：LLM 对话 + 舵机命令混合

**目的**：验证 SERVO 命令加入后，未破坏既有 LLM 对话、TTS、Barge-in、Wake Word 全套流程。

### 步骤

1. 唤醒 "你好"
2. 说 "今天天气怎么样" → 期望 LLM 回复 + TTS 播放
3. 说 "向左" → 期望 Pan 动 + 无 TTS
4. 说 "停" → 期望打断 TTS（如上一轮还在播）
5. 说 "向右" → 期望 Pan 反向
6. 说 "你好" → 期望再次确认 "我在，请说"
7. 说 "回中" → 期望双轴回中

### 期望结果

所有 7 步按预期响应；无卡死、无重复 ASR、无 TTS 误触发、无 LLM 无端调用。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 步骤 3 后 TTS 也响应了"向左" | SERVO 分支误走 `pipeline_text()` |
| 步骤 5 卡住 | SVCO 分支未 return；下一帧 RECM 被误解析 |
| 步骤 6 唤醒无效 | 上一次 SVCO 之后未 `_send_empty_play()` 解锁 ESP32 |

---

## TC-15 · 状态机：SLEEPING 下 SERVO 命令被忽略

**目的**：验证未唤醒时不会误触发舵机（安全约束）。

### 步骤

1. 上电后不唤醒
2. 说 "向上"

### 期望结果

- PC 日志：`[router] type=SERVO_COMMAND`（分类成功）→ **被 SLEEPING 分支丢弃**
- **不发送** SVCO 帧
- 扬声器无反应，舵机不动

---

## TC-16 · 一次 ASR 原则：SERVO 不重复 ASR

### 步骤

1. 触发一条 "向上"
2. 观察 PC 日志中 `transcribe(` 调用次数

### 期望结果

**恰好 1 次** ASR 调用；SVCO 分支**不**再触发一次 Whisper（因为不需要文本再送 LLM）。

---

## TC-17 · 无 LLM 调用

### 步骤

1. 记录触发前的 LLM API 调用计数（比如 Ollama `/api/ps` 或 Gemini quota）
2. 触发 5 次 "向上"、3 次 "向右"
3. 再看计数

### 期望结果

LLM 调用次数 = **0**。

---

## TC-18 · 无 TTS、无 Barge-in 触发

### 步骤

1. 说 "向右"
2. 在舵机动作期间立即说 "继续" 或直接说话

### 期望结果

- 无 TTS 声音
- 无 `[i2s] play` 日志
- 因没有 PLAY，ESP32 的 barge-in 检测路径不激活
- 下一次 RECM 上传正常

---

## TC-19 · 负样本：非舵机短语不误触发

**目的**：验证不会因相似短语（"左上角"、"向左看窗外"）误触发舵机。

### 步骤

在 ACTIVE 状态下逐句说（每句之间等 3s）：

| 应忽略 | 期望 |
|--------|------|
| "左上角" | USER_TEXT，进入 LLM |
| "右上" | USER_TEXT |
| "左下方" | USER_TEXT |
| "向左看窗外的树" | USER_TEXT |
| "向上看" | USER_TEXT（**"向上看" 不在短语表**） |
| "往左走" | USER_TEXT |
| "上面是什么" | USER_TEXT |
| "下个月工资" | USER_TEXT |
| "回头告诉我" | USER_TEXT（**"回头" ≠ "回中"**） |
| "回公司" | USER_TEXT |

### 期望结果

以上 10 句**均**分类为 `USER_TEXT`，**不**发出 SVCO 帧，**不**驱动舵机。

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| "回头告诉我" 触发了 CENTER_ALL | `_SERVO_PATTERNS` 里 `回到中间` 正则太宽松；应确保短语以 `^` 开头且用严格后缀 |
| "向上看" 触发了 VERTICAL_UP | `_TAIL` 里可能多了 `看` 或类似字；应只保留 `[啊呀呢吧哦]` |

---

## TC-20 · 打断优先级：`"停"` 优先于 `"左"`

### 步骤

1. 唤醒
2. 说 "停"（无上下文）
3. 说 "左"

### 期望结果

- "停" → INTERRUPT（不触 TTS，不触 SERVO）
- "左" → SERVO_COMMAND (HORIZONTAL_LEFT)

如果用户说一句 `"停向左"`（连读），CommandRouter 应先匹配 INTERRUPT（因为优先级 INTERRUPT > SERVO），且**不**发出 SVCO。

---

## TC-21 · 断线自动重连后 SERVO 仍可用

### 步骤

1. 正常唤醒 + 说 "向右" 验证 SVCO 生效
2. 拔网线（ESP32 端） 30 秒
3. 插回网线，等 TCP 重连日志
4. 说 "你好"（唤醒）
5. 说 "向左"

### 期望结果

- 步骤 3 出现 `[wifi] TCP reconnected`
- 步骤 5 正常发出 SVCO 且舵机动

---

## TC-22 · 长时间稳定性（10 分钟连发）

### 步骤

在 10 分钟内循环发以下序列（每 15 秒一轮）：

```text
向上 → 向下 → 向左 → 向右 → 回中 → 你好 → 向右 → 回中
```

约 40 轮。观察串口日志 + PC 日志 + 舵机表现。

### 期望结果

- 无 TCP 断连
- 无内存泄漏迹象（`heap` 稳定）
- 舵机动作准确，无累积漂移
- 无随机 TTS 触发
- 每轮"你好"都能唤醒

### 排查思路

| 现象 | 可能原因 |
|------|----------|
| 5 分钟后 TCP 断开 | SVCO 分支消耗了 TCP 缓冲未 ACK |
| 舵机开始漂移 | `s_horizontalAngle` 溢出；检查 int 类型 |
| 内存持续上涨 | `_session_pcm` 未清理 |

---

## 附录 A · 参数速查

| 项 | 值 | 位置 |
|----|----|------|
| Pan GPIO | 4 | `SERVO_H_SIGNAL_GPIO` |
| Tilt GPIO | 5 | `SERVO_V_SIGNAL_GPIO` |
| Pan LEDC 通道 | 1 | `SERVO_H_PWM_CHANNEL` |
| Tilt LEDC 通道 | 0 | `SERVO_V_PWM_CHANNEL` |
| PWM 频率 | 50 Hz | `SERVO_PWM_FREQ_HZ` |
| PWM 分辨率 | 13-bit | `SERVO_PWM_RES_BITS` |
| 中心角 | 90° | `SERVO_CENTER_ANGLE` |
| 最小角 | 60° | `SERVO_MIN_ANGLE` |
| 最大角 | 120° | `SERVO_MAX_ANGLE` |
| 步进 | 10° | `SERVO_STEP_DEG` |
| PWM pulse min | 125 (0.6 ms) | 60° 对应 |
| PWM pulse max | 275 (1.4 ms) | 120° 对应 |
| SVCO 载荷长度 | 3 字节 | `SVCO_PAYLOAD_LEN` |
| TCP 端口 | 8888 | `config.WIFI_TCP_PORT` |

---

## 附录 B · 串口 / PC 日志速查

### ESP32 端（921600 baud）

关键 tag：

| 日志 | 含义 |
|------|------|
| `[servo] init: LEDC ch0 on GPIO5 ...` | 双舵机 LEDC 初始化成功 |
| `[servo] axis TILT center angle=90 duty=200` | Tilt 回中 |
| `[servo] axis PAN center angle=90 duty=200` | Pan 回中 |
| `[servo-cmd] VERTICAL_UP` | 收到 SVCO 并分派 |
| `[servo] axis TILT step +10 -> 100` | Tilt 步进结果 |
| `[servo] vertical limit reached: 120` | 触顶 |
| `[servo] horizontal limit reached: 60` | 触底 |

### PC 端

| 日志 | 含义 |
|------|------|
| `[asr] transcribe: "向上"` | Whisper 结果 |
| `[router] type=SERVO_COMMAND servo_command=0x01` | CommandRouter 分类 |
| `[wifi_server] SVCO cmd=0x01` | 已发 SVCO |
| `[wifi_server] sending empty PLAY (size=0)` | 解锁 ESP32 |

---

## 附录 C · SVCO 命令编码速查

| 语音短语 | cmd hex | cmd name | ESP32 动作 |
|----------|---------|----------|-----------|
| 向上 / 上 / 抬头 | 0x01 | `SERVO_CMD_VERTICAL_UP` | `servo_step_vertical(-10)`（Tilt 硬件反向） |
| 向下 / 下 / 低头 | 0x02 | `SERVO_CMD_VERTICAL_DOWN` | `servo_step_vertical(+10)`（Tilt 硬件反向） |
| 向左 / 左 / 左转 | 0x11 | `SERVO_CMD_HORIZONTAL_LEFT` | `servo_step_horizontal(-10)` |
| 向右 / 右 / 右转 | 0x12 | `SERVO_CMD_HORIZONTAL_RIGHT` | `servo_step_horizontal(+10)` |
| 竖直回中 | 0x21 | `SERVO_CMD_VERTICAL_CENTER` | `servo_center_vertical()` |
| 水平回中 | 0x22 | `SERVO_CMD_HORIZONTAL_CENTER` | `servo_center_horizontal()` |
| 回中 / 回正 / 回到中间 | 0x2F | `SERVO_CMD_CENTER_ALL` | `servo_center_all()` |

> 注：`0x21`（竖直回中）和 `0x22`（水平回中）当前无对应语音短语，作为协议扩展预留；未来可以映射"抬头回中"、"平视"等短语。

---

## 附录 D · 常见故障速查

| 现象 | 定位 | 修法 |
|------|------|------|
| 上电自测序列仍在跑 | main.cpp setup() | 把 `servo_run_test_sequence()` 替换为 `servo_init()` |
| 说"向上"没反应 | 1) `_SERVO_PATTERNS` 缺项 2) Whisper 识别错 | 检查 `test_command_router.py` 用例；加更严格的短语 |
| 说"上"触发了 LLM | `_SERVO_PATTERNS` 排序错 | 短语必须"长在前"（`"向上"` 在 `"上"` 之前） |
| SVCO 发送后 ESP32 卡住 | 未发空 PLAY | 检查 `_handle_rptf()` 的 else 分支是否继续走 `_send_empty_play()` |
| 舵机朝反方向动 | 步进符号错 | Pan：`servo_step_horizontal(+10)` 应等于向右；Tilt 硬件方向与 Pan 相反，`handleServoCommand()` 已做反向处理，改前先确认接线方向 |
| 两只舵机一起动 | GPIO 冲突 | Pan=4 / Tilt=5；不要共用 LEDC channel |
| 舵机供电掉线复位 | 独立电源 | 加 5V/2A 独立电源 + 共地 |
| SLEEPING 也触发舵机 | wifi_server 状态机错 | `_wake_activated == False` 时应直接丢弃 SERVO_COMMAND |
| "停向左" 触发了 SERVO | INTERRUPT 优先级 | 应 `INTERRUPT` 优先于 `SERVO_COMMAND`（当前实现已保证） |

---

## 附录 E · 参考文档

- [`docs/protocol.md`](./protocol.md) — SVCO 帧规范（§4.3）
- [`docs/hardware.md`](./hardware.md) — 双舵机接线（§4.7 / §4.8）
- [`docs/architecture.md`](./architecture.md) — CommandRouter + Session State（§6）
- [`docs/test-2026-09-09-phase1-wifi-voice-loop.md`](./test-2026-09-09-phase1-wifi-voice-loop.md) — 主链路前置测试
- [`pc/command_router.py`](../pc/command_router.py) — 短语表 + 正则
- [`pc/wifi_server.py`](../pc/wifi_server.py) — `_send_servo_command()` 实现
- [`firmware/esp32/src/servo/servo_control.h`](../firmware/esp32/src/servo/servo_control.h) — 常量 + API
- [`firmware/esp32/src/servo/servo_control.cpp`](../firmware/esp32/src/servo/servo_control.cpp) — 双轴状态机
- [`firmware/esp32/src/main.cpp`](../firmware/esp32/src/main.cpp) — `handleServoCommand()`
- [`firmware/esp32/src/protocol/frame.h`](../firmware/esp32/src/protocol/frame.h) — SVCO 常量

---

## 附录 F · 执行记录模板

```text
执行日期：_______________
测试人：_________________
固件版本：env=esp32-s3-n16r8-wifi, commit=_______________
PC Python 版本：________
Whisper 模型：__________

[ ] TC-01 硬件接线（双舵机）
[ ] TC-02 PC 单元测试 18/18
[ ] TC-03 PlatformIO 双 env 编译
[ ] TC-04 启动日志无自测
[ ] TC-05 启动双轴回中
[ ] TC-06 SVCO 帧结构抓包
[ ] TC-07 VOICE 向上（Tilt +10°）
[ ] TC-08 VOICE 向下（Tilt −10°）
[ ] TC-09 VOICE 向左（Pan −10°）
[ ] TC-10 VOICE 向右（Pan +10°）
[ ] TC-11 VOICE 回中
[ ] TC-12 边界 触顶
[ ] TC-13 边界 触底
[ ] TC-14 LLM+舵机混合端到端
[ ] TC-15 SLEEPING 忽略 SERVO
[ ] TC-16 一次 ASR
[ ] TC-17 无 LLM 调用
[ ] TC-18 无 TTS / 无 Barge-in
[ ] TC-19 负样本拒识
[ ] TC-20 打断优先级
[ ] TC-21 断线重连
[ ] TC-22 10 分钟稳定性

关键观察：
----------------------------------------

故障记录：
----------------------------------------

结论：PASS / FAIL / BLOCKED
```

---

## 附录 G · 回归对比基线（Phase 1 vs 双舵机）

用于验证"未破坏既有功能"。执行 Phase 1 原测试文件时若结果不同，说明本文件引入了回归。

| 场景 | Phase 1 期望 | 双舵机期望 | 差异 |
|------|--------------|------------|------|
| 唤醒"你好" | 激活 + 回复 | 激活 + 回复 | 无 |
| LLM 问答 | 完整走 ASR→LLM→TTS | 完整走 ASR→LLM→TTS | 无 |
| "停" | 打断 TTS | 打断 TTS | 无 |
| 断线重连 | 自动 | 自动 | 无 |
| setup() 时长 | ~1s | ~1.5s（多了回中） | 预期 |
| Flash 使用 | ~10% | 13.6% | +3.6%（可接受） |
| 舵机动作 | 无（setup 自测 3 次） | 有（SVCO 驱动） | 预期 |

**回归红线**（任一出现必须停测）：
1. Wake Word 匹配失效
2. 长句被拆错
3. LLM 出现重复请求
4. TTS 无声或声音异常
5. TCP 反复断连
6. Barge-in 失效（TTS 播放中无法打断）

---

## 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-09-27 | 首版：22 个测试用例覆盖硬件接线、单元回归、编译、烧录、SVCO 抓包、单条语音、边界、混合回归、状态机、性能与稳定性 |
