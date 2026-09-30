# 测试用例 · 2026-09-27 · LCD MVP（ST7735S 160×80）

> **文档编号**：TC-20260927-LCD-MVP
> **版本**：v1.0
> **适用代码**：`firmware/esp32/src/display/display.{h,cpp}` + `main.cpp` 集成 + `platformio.ini` 双库依赖
> **测试目标**：验证「硬件接线 → SPI 初始化 → 8 状态 / 5 表情正确显示 → 状态变化才刷新 → 非阻塞主链路」全链路；确认 LCD 不干扰既有 Voice / VAD / TCP / Wake Word / LLM / TTS / Servo 路径
> **前置测试**：Phase 1 语音闭环（[`test-2026-09-09-phase1-wifi-voice-loop.md`](./test-2026-09-09-phase1-wifi-voice-loop.md)）与 双舵机语音控制（[`test-2026-09-27-dual-servo-voice.md`](./test-2026-09-27-dual-servo-voice.md)）均已通过
>
> **测试时长**：全流程约 30–40 分钟；每个 TC 独立，可按编号跳测
>
> **说明**：LCD MVP 无 PC 端单元测试（纯硬件渲染），本文件聚焦**硬件 + 端到端**验证。

---

## 0. 目录

| 编号 | 测试项 | 时长 | 依赖 |
|------|--------|------|------|
| TC-01 | 硬件接线与供电检查（LCD） | 5 min | 无 |
| TC-02 | PlatformIO 双 env 编译 | 3 min | 无 |
| TC-03 | 固件烧录 + 启动日志（含 display_init） | 3 min | TC-01 + TC-02 |
| TC-04 | 上电状态：STARTING → WIFI → READY 三段递进 | 3 min | TC-03 |
| TC-05 | 静态帧正确性（表情 + 文字 + 颜色） | 5 min | TC-03 |
| TC-06 | 状态刷新策略：同状态 no-op，异状态重绘 | 5 min | TC-03 |
| TC-07 | Voice 触发 LISTENING 状态 | 3 min | TC-03 |
| TC-08 | Voice 触发 THINKING 状态 | 3 min | TC-03 |
| TC-09 | TTS 触发 SPEAKING → 回 READY | 3 min | TC-03 |
| TC-10 | Servo 命令触发 SERVO → 回 READY | 3 min | TC-03 |
| TC-11 | 非阻塞验证：LCD 故障模拟（拔线）不阻塞主链路 | 5 min | TC-03 |
| TC-12 | 边界：连续切换 8 状态 10 轮无卡顿 | 3 min | TC-03 |
| TC-13 | 长时间稳定性（30 分钟混合场景） | 30 min | TC-03 |
| TC-14 | 回归：语音闭环 / Barge-in / 舵机命令 / Wake Word 均不受影响 | 8 min | TC-03 |

**附录**：
- A · 参数速查
- B · 状态 / 表情映射速查
- C · GPIO 分配速查
- D · 常见故障排查
- E · 参考文档
- F · 执行记录模板
- G · 回归对比基线（Phase 1 vs LCD MVP）

---

## 通用前置条件

```text
[ ] ESP32-S3 N16R8 主板（Phase 1 已验证可用）
[ ] MAX9814 麦克风 + MAX98357A I2S 功放 + 3W 扬声器（Phase 1 已接线）
[ ] 0.96" ST7735S IPS LCD 模块（160×80，4-line SPI，含背光引脚）
[ ] 独立舵机电源 5V/6V，额定 ≥ 2A（若同框测试双舵机）
[ ] USB 数据线（能传数据）
[ ] PlatformIO 编译环境（VS Code 或 CLI）
[ ] Python 3.9+，已 pip install -r pc/requirements.txt
[ ] Whisper.cpp 模型 + edge-tts 可用
[ ] Wi-Fi 路由器 + config.local.json 已配置
[ ] 串口监视工具（minicom /tio / Putty / VS Code Serial Monitor）
```

---

## TC-01 · 硬件接线与供电检查（LCD）

**目的**：确保 ST7735S 6 根信号线接线正确、VCC/GND 稳定，避免上电后屏无显示或 SPI 报错。

### 步骤

1. 目视检查接线（参考 [`docs/hardware.md`](./hardware.md) §3.1 / §3.2）：

   | LCD 引脚 | ESP32-S3 GPIO | 备注 |
   |----------|---------------|------|
   | SCL | GPIO14 | SPI clock |
   | SDA (MOSI) | GPIO13 | SPI data |
   | RES | GPIO12 | 复位（低有效，Arduino 拉高） |
   | DC  | GPIO11 | 数据 / 命令选择 |
   | CS  | GPIO10 | 片选（低有效） |
   | BLK | 3.3V   | 常亮，不使用 PWM |
   | VCC | 3.3V   | 电源 |
   | GND | GND    | 共地 |

2. 用万用表 DC 电压档确认：
   - LCD VCC - GND = 3.2 – 3.4 V
   - LCD BLK - GND = 3.2 – 3.4 V
3. 拔插一次 SPI 杜邦线确认无虚接
4. 确认 LCD 独立供电或与其他 3.3V 负载共 3.3V LDO

### 期望结果

- 所有 6 根信号线连接可靠
- LCD 单独上电（未接 ESP32）时背光亮起、屏面为黑（无背光时应完全黑）

### 排查思路

- 屏无背光 → 检查 BLK 是否接到 3.3V（不是 5V！）
- 屏亮但内容全黑 → 屏本身正常，等 display_init 后应显示内容
- ESP32 上电即复位 → LCD VCC 拉到 5V，检查电源电压

---

## TC-02 · PlatformIO 双 env 编译

**目的**：确认新增 `Adafruit_ST7735` + `Adafruit_GFX` 依赖能同时链接 base 和 wifi 两个 env。

### 步骤

```bash
cd esp32-voice-ai/firmware/esp32
pio run -e esp32-s3-n16r8          2>&1 | tee /tmp/lcd_base.log
pio run -e esp32-s3-n16r8-wifi     2>&1 | tee /tmp/lcd_wifi.log
```

### 期望结果

- 两个 env 均 SUCCESS
- 输出中可见 `Linking .pio/build/.../firmware.elf` 与 `RAM: [===         ] xx% (used / total)`
- 依赖解析行可见：
  - `LDF: Library Dependency Finder -> *.pio/libdeps`
  - `found 2 packages`（含 Adafruit_ST7735 和 Adafruit_GFX）
- Flash 占用不高于 wifi env 20%（新增库约 +30KB）
- 编译耗时不超过 30s（增量）/ 60s（clean）

### 排查思路

- 依赖解析失败 → `platformio.ini` 的 `lib_deps` 拼写或版本错误；registry 上包名是 `adafruit/Adafruit ST7735 and ST7789 Library@^1.11.0`（不是 `Adafruit ST7735 Library@^1.7`）
- 链接错误 `begin() is protected` → 使用了 `Adafruit_ST7735` 旧 API；应调用 `initR(INITR_GREENTAB)` 而非 `begin(freq)`
- 未定义 `BLACK` / `WHITE` → 新版库使用 `ST7735_BLACK` / `ST7735_WHITE`

---

## TC-03 · 固件烧录 + 启动日志（含 display_init）

**目的**：确认 `display_init()` 在 `setup()` 中被正确调用且未阻塞其他初始化。

### 步骤

```bash
pio run -e esp32-s3-n16r8-wifi -t upload
```

启动串口监视器（921600 baud），记录首 5 秒日志。

### 期望结果

- 首次启动时序：
  1. `[BOOT] ...` / 项目 banner
  2. `display_init(): initializing ST7735S...`（若 display.cpp 有日志）
  3. `display_init(): OK` / `LCD ready`
  4. `STARTING` 状态短暂显示后自动过渡
  5. Wi-Fi 连接进度日志
  6. TCP 连接到 wifi_server
  7. `servo_init()` 日志
  8. `READY` 状态稳定显示
- 屏面在 step 2 之后点亮并渲染内容
- 从 `display_init()` 到 `READY` 状态之间的总耗时应在 5 秒内（LCD 初始化本身 < 100ms）

### 排查思路

- LCD 完全无内容 → 检查 TC-01 接线；检查 `display.cpp::display_init()` 是否成功调用
- 屏面闪烁后黑屏 → 背光接 5V 或 RES 未接
- SPI 相关报错 → SCL / SDA 反接，检查 GPIO14/GPIO13

---

## TC-04 · 上电状态：STARTING → WIFI → READY 三段递进

**目的**：验证主程序启动阶段能正确驱动 LCD 显示三个连续状态。

### 步骤

1. 断电后重新上电 ESP32-S3
2. 观察 LCD 屏幕内容变化：
   - 期望先看到 `STARTING` 文字 + normal 表情
   - Wi-Fi 连接期间看到 `WIFI` 文字
   - TCP 连接建立 + `servo_init()` 后看到 `READY`
3. 记录状态切换时刻与串口日志的时间戳对比

### 期望结果

- 三个状态均显示且顺序正确
- 每个状态显示至少 200 ms（人眼可识别）
- `READY` 状态稳定保持，直到用户主动说话或触发服务端下行

### 排查思路

- 卡在某状态 → 检查 `main.cpp` 中该状态的 `display_set_state()` 调用点是否被跳过
- 三个状态一闪而过 → 若 Wi-Fi 连得非常快可能自然现象；若慢应能看到 WIFI 停留
- READY 不出现 → 检查 `servo_init()` 后是否有 `display_set_state(DISPLAY_STATE_READY)` 调用

---

## TC-05 · 静态帧正确性（表情 + 文字 + 颜色）

**目的**：验证 5 种表情 + 8 种状态文字在物理屏上清晰可辨、颜色正确。

### 步骤

1. 从 `READY` 状态出发，逐步触发各状态并拍照记录：

   | 状态 | 期望表情 | 期望颜色 | 触发方式 |
   |------|----------|----------|----------|
   | STARTING | normal（默认） | 白 on 黑 | 断电重启 |
   | WIFI | normal | 白 on 黑 | 断电重启，Wi-Fi 阶段 |
   | READY | normal | 白 on 黑 | 稳态等待 |
   | LISTENING | listening（红点） | 白 + 红点 | 说话触发 |
   | THINKING | thinking | 白 on 黑 | 说完等待 LLM |
   | SPEAKING | speaking | 白 on 黑 | 服务端回复播放 |
   | SERVO | normal | 白 on 黑 | "向左" 等舵机命令 |
   | ERROR | error（红框） | 白 + 红 | 需手动注入，见下方 |

2. 手动触发 ERROR：临时改 `main.cpp` 中 `display_set_state(DISPLAY_STATE_READY)` → `display_set_state(DISPLAY_STATE_ERROR)`，编译烧录，观察屏面
3. 完成后恢复代码

### 期望结果

- 5 种表情视觉可区分（眼睛形状 / 嘴巴形状 / 有无红点 / 有无红框）
- 8 种状态文字均能读出，字体清晰（6×8 ASCII）
- 表情与文字均不重叠、不出界
- 颜色对比度高（LCD 是 1-bit 或 2-bit 显示，白字黑底为默认）

### 排查思路

- 表情错乱 → 检查 `drawFace()` 中的坐标是否超出 160×80 范围
- 文字被截断 → 检查字符串是否超过 ~20 字符（160/6 ≈ 26 字符上限）
- 红框不显示 → 检查 `ST7735_RED` 是否被正确绘制
- `invertDisplay(true)` 反转了颜色 → 若屏显示是黑字白底，去掉 `invertDisplay(true)`

---

## TC-06 · 状态刷新策略：同状态 no-op，异状态重绘

**目的**：验证 `strcmp` 去重逻辑生效，防止每轮 loop() 都重复 SPI 写屏导致 CPU 占用过高。

### 步骤

1. 稳态停在 `READY` 状态 60 秒
2. 用逻辑分析仪（可选）或串口 log 采样 loop() 频率
3. 观察 LCD 是否有可见的刷新闪烁（正常应为 0 次刷新，屏幕静态保持）
4. 说话触发 LISTENING → THINKING → SPEAKING → READY 完整一轮，确认每次切换都有重绘

### 期望结果

- 稳态 60 秒期间，屏幕无任何可见刷新动作
- 每次状态切换（8 次切换）都有可见重绘
- 主循环 CPU 占用不因 LCD 显著增加（相较 Phase 1 基线增加 < 2%）

### 排查思路

- 稳态时屏幕仍在轻微刷新 → `display_set_state()` 中 `strcmp` 判断失效；检查 `g_currentState` 是否在每次调用后更新
- 状态切换时屏幕没变化 → `strcmp` 恒为 0；检查 `g_currentState` 初始值与传入 state 是否指针相同

---

## TC-07 · Voice 触发 LISTENING 状态

**目的**：验证 `mic_uploader.cpp` 的 poll() 之后能正确切换到 LISTENING 状态。

### 步骤

1. 稳态停在 `READY`
2. 对麦克风正常说话（如"你好，机器人"），等待 VAD 检测到语音
3. 观察 LCD 状态

### 期望结果

- VAD 触发后，LCD 在 500 ms 内切换到 `LISTENING`
- 屏幕显示 listening 表情（含红色指示点）
- 保持 LISTENING 直到录音结束（服务端下行 PLAY 帧到达或超时）

### 排查思路

- LCD 停留在 READY → `main.cpp::loop()` 中 `g_mic.poll()` 后缺少 `display_set_state(DISPLAY_STATE_LISTENING)`
- LCD 立即跳回 READY → 状态调用点位置错误，可能被下行分支立即覆盖

---

## TC-08 · Voice 触发 THINKING 状态

**目的**：验证录音结束后、LLM 回复到达前的过渡状态。

### 步骤

1. 完成一次 TC-07 触发 LISTENING
2. 停止说话，等待 ASR 完成、LLM 处理期间
3. 观察 LCD 状态

### 期望结果

- LISTENING → THINKING 切换（在录音停止信号发出后）
- THINKING 保持到服务端 PLAY 帧到达
- THINKING 时间 ≈ LLM 响应时间（通常 1-3 秒）

### 排查思路

- 未出现 THINKING → 检查 `main.cpp::loop()` 下行门控处的 `display_set_state(DISPLAY_STATE_THINKING)` 是否被执行
- THINKING 一闪而过 → LLM 太快属于正常；若每次都闪 → 状态触发点位置可能过早

---

## TC-09 · TTS 触发 SPEAKING → 回 READY

**目的**：验证播放期间显示 SPEAKING，播放完成后正确回到 READY。

### 步骤

1. 说一句完整问句，等待 TTS 回复播放
2. 观察 LCD 状态变化

### 期望结果

- 播放开始前 200 ms 内切换到 `SPEAKING`
- 显示 speaking 表情
- 播放结束（`notifyPlaybackDone()` 触发后）在 100 ms 内回到 `READY`

### 排查思路

- 未显示 SPEAKING → 检查 `playPCM()` 之前是否有 `display_set_state(DISPLAY_STATE_SPEAKING)` 调用
- 播放后未回 READY → 检查 `notifyPlaybackDone()` 后是否有 `display_set_state(DISPLAY_STATE_READY)` 调用

---

## TC-10 · Servo 命令触发 SERVO → 回 READY

**目的**：验证舵机命令执行期间显示 SERVO 状态。

### 步骤

1. 稳态 `READY`
2. 说"向左"或"抬头"
3. 观察 LCD 状态与舵机动作同步性

### 期望结果

- 收到 SVCO 帧后立即切换到 `SERVO`
- 舵机动作期间保持 `SERVO`
- 舵机动作完成后（步进 + 短暂延迟）回到 `READY`

### 排查思路

- 无 SERVO 显示 → 检查 `handleServoCommand()` 入口是否有 `display_set_state(DISPLAY_STATE_SERVO)`
- SERVO 一闪而过 → 舵机动作太快，属正常现象（10° 步进 ≈ 100 ms）

---

## TC-11 · 非阻塞验证：LCD 故障模拟（拔线）不阻塞主链路

**目的**：验证 LCD 是辅助模块，任何 LCD 故障都不能阻塞 Robot Core。

### 步骤

1. **场景 A**：断开 LCD CS 线（GPIO10），保持其他连接
   - 说话触发一次完整语音链路
   - 期望：语音链路完全不受影响；LCD 显示可能残缺或不变；串口无 panic 或 watchdog reset
2. **场景 B**：完全拔掉 LCD 排线（4 根信号线都断）
   - 说话触发一次完整语音链路
   - 期望：语音链路完全不受影响
3. **场景 C**：LCD VCC 拔线（LCD 断电）
   - 说话触发一次完整语音链路
   - 期望：语音链路完全不受影响；ESP32 不被复位

### 期望结果

- 三种场景下，语音闭环（ASR → LLM → TTS → PLAY）均能正常运行
- 无 `Guru Meditation` / `Watchdog reset` / `SPI: timeout` 日志
- `display_set_state()` 调用在故障时静默失败（no-op 或忽略），不 panic

### 排查思路

- 出现 watchdog reset → LCD 初始化阻塞超过 5 秒；检查 `display_init()` 是否有无限重试循环
- SPI 超时日志 → `Adafruit_ST7735` 内部有 SPI 传输超时；考虑用 `display_init()` 保护性 try / 检测返回值
- 语音链路卡顿 → `display_set_state()` 被高频调用且 SPI 未准备好导致阻塞；应检查 `g_displayReady` 是否正确置 false

---

## TC-12 · 边界：连续切换 8 状态 10 轮无卡顿

**目的**：验证状态机在高频切换场景下不出现内存泄漏或屏面撕裂。

### 步骤

1. 使用 Python 脚本（或手工触发）在 5 分钟内快速触发以下状态切换：

   ```text
   LISTENING → THINKING → SPEAKING → READY → SERVO → READY
   LISTENING → THINKING → SPEAKING → READY → LISTENING
   LISTENING → THINKING → READY（打断）
   ... 循环 10 轮
   ```

2. 每轮观察：
   - LCD 内容更新是否及时（无残留）
   - 屏幕是否出现撕裂或花屏
   - 主循环 CPU 占用（通过日志打印或外部分析）

### 期望结果

- 10 轮内无内存泄漏、无屏面撕裂
- LCD 显示始终与当前状态同步
- 主循环 CPU 占用波动 < 5%

### 排查思路

- 屏面撕裂 → `display_set_state()` 中间未 `display_clear()`；检查调用顺序
- 屏面残留旧内容 → `display_clear()` 未生效；检查 `fillScreen(ST7735_BLACK)` 是否被调用
- 内存泄漏 → 每次 `display_set_state()` 都分配临时 buffer；应使用静态变量而非 malloc

---

## TC-13 · 长时间稳定性（30 分钟混合场景）

**目的**：确认 LCD 长时间运行下无累积故障。

### 步骤

1. 启动设备，保持 `READY` 稳态 5 分钟
2. 每 5 分钟触发一次完整语音链路（LISTENING → THINKING → SPEAKING → READY）
3. 每 10 分钟触发一次舵机命令（SERVO → READY）
4. 记录每次切换时刻的：
   - LCD 状态显示是否正确
   - 屏幕是否仍有内容（未变黑 / 未花屏）
   - 主循环 CPU 占用（可通过日志时间戳估算）

### 期望结果

- 30 分钟内 LCD 无任何闪烁、死机、变黑、花屏
- 状态切换均能及时反映
- 主循环无内存泄漏（RSS 稳定）

### 排查思路

- LCD 逐渐变暗 → 背光电流不稳；检查供电
- LCD 逐渐花屏 → SPI 时序漂移；检查 SPI 频率是否过高
- 一段时间后完全黑屏 → `invertDisplay()` 被误切换或驱动丢失；检查是否有代码路径调用 `invertDisplay(false)`

---

## TC-14 · 回归：语音闭环 / Barge-in / 舵机命令 / Wake Word 均不受影响

**目的**：确认 LCD 集成不破坏 Phase 1 已验证的任何功能。

### 步骤

1. 执行 Phase 1 关键 TC（[`test-2026-09-09-phase1-wifi-voice-loop.md`](./test-2026-09-09-phase1-wifi-voice-loop.md)）：
   - TC-05 MAX9814 ADC 采集
   - TC-06 VAD 触发
   - TC-09 PLAY 分块 + ACK
   - TC-10 端到端语音闭环
   - TC-11 断线自动重连
2. 执行 Barge-in 关键 TC（若已启用）：
   - "停" 打断播放
3. 执行双舵机关键 TC（[`test-2026-09-27-dual-servo-voice.md`](./test-2026-09-27-dual-servo-voice.md)）：
   - TC-07 / TC-08 / TC-09 / TC-10 / TC-11
4. 执行 Wake Word 关键 TC：
   - "你好" 唤醒
   - SLEEPING / ACTIVE 双态
5. 对比回归基线（附录 G）

### 期望结果

- 所有 Phase 1 TC 通过率 100%
- 所有双舵机 TC 通过率 100%（已知 ASR 识别错误项除外）
- Wake Word 触发率不低于基线
- LCD 的存在不改变任何既有状态机的语义

### 排查思路

- 某 TC 通过率下降 → 检查 `main.cpp` 中新增 `display_set_state()` 调用是否插入到错误的分支
- Barge-in 延迟增加 → LCD 更新占用 CPU；检查 TC-06 的刷新频率
- Wake Word 触发率下降 → VAD 阈值可能被 LCD 干扰（不太可能，除非供电问题）

---

## 附录 A · 参数速查

### ESP32 端（`firmware/esp32/src/display/display.cpp`）

```text
LCD_SCLK_GPIO   = 14
LCD_MOSI_GPIO   = 13
LCD_RST_GPIO    = 12
LCD_DC_GPIO     = 11
LCD_CS_GPIO     = 10
LCD_ROTATION    = 1      // 逆时针 90°
LCD_SPI_KHZ     = 20000  // 20 MHz
```

### Adafruit_ST7735 内部参数（`initR(INITR_GREENTAB)` 设置）

```text
colstart = 26   // 有效像素 x 起点
rowstart = 1    // 有效像素 y 起点
```

### PC 端

```text
端口：8888（wifi_server）
串口：921600 baud
```

---

## 附录 B · 状态 / 表情映射速查

| State (display_set_state) | Face (drawFace) | Color | Text |
|---------------------------|-----------------|-------|------|
| STARTING  | normal    | 白 on 黑 | "STARTING" |
| WIFI      | normal    | 白 on 黑 | "WIFI" |
| READY     | normal    | 白 on 黑 | "READY" |
| LISTENING | listening | 白 + 红点 | "LISTENING" |
| THINKING  | thinking  | 白 on 黑 | "THINKING" |
| SPEAKING  | speaking  | 白 on 黑 | "SPEAKING" |
| SERVO     | normal    | 白 on 黑 | "SERVO" |
| ERROR     | error     | 白 + 红框 | "ERROR" |

---

## 附录 C · GPIO 分配速查

| GPIO | LCD 引脚 | 其他功能冲突 |
|------|----------|--------------|
| 8    | CS       | 无（原空闲） |
| 9    | DC       | 无（原空闲） |
| 10   | RES      | 无（原空闲） |
| 11   | SDA (MOSI) | 无（原空闲） |
| 12   | SCL      | 无（原空闲） |

**保留冲突（原占用，未变）**：
- GPIO 4 = Pan 舵机 PWM
- GPIO 5 = Tilt 舵机 PWM
- GPIO 25 = I2S DOUT (spk)
- GPIO 26 = I2S BCLK
- GPIO 27 = I2S LRCK
- GPIO 41 = ADC IN (mic)

---

## 附录 D · 常见故障排查

| 现象 | 可能原因 | 解决 |
|------|----------|------|
| LCD 全黑（无背光） | BLK 未接 3.3V | 检查 BLK 接线 |
| LCD 有背光但无内容 | `display_init()` 未成功 | 检查串口日志中的 `display_init` |
| LCD 内容显示不全（右侧截断） | `colstart` 错误 | 确认使用 `initR(INITR_GREENTAB)` |
| LCD 内容左右颠倒 | `setRotation()` 参数错误 | 修改 `LCD_ROTATION`（0/1/2/3） |
| LCD 内容上下颠倒 | `invertDisplay()` 误用 | 检查 `invertDisplay(true)` |
| LCD 白字黑底 → 黑字白底 | 反转 | 检查是否多次调用 `invertDisplay()` |
| LCD 有花屏 / 条纹 | SPI 时序问题 | 降低 `LCD_SPI_KHZ` 到 10000 |
| ESP32 上电即复位 | LCD 拉低了某个信号 | 检查 CS / RES 是否接反 |
| ESP32 无法 boot（烧录失败） | LCD CS 线冲突 | 检查是否误接到 GPIO 45（USB D-） |
| LCD 显示正常但 `display_set_state()` 无效 | `g_displayReady` 未置 true | 检查 `display_init()` 返回值处理 |
| 稳态时 LCD 频繁刷新 | `strcmp` 去重失效 | 检查 `g_currentState` 更新时机 |
| LCD 存在时语音链路卡顿 | LCD 阻塞 SPI | 检查 `display_init()` 是否有重试循环；确认主循环不主动调用 `display_clear()` |

---

## 附录 E · 参考文档

- [`docs/hardware.md`](./hardware.md) §3.4 LCD 模块
- [`docs/architecture.md`](./architecture.md) §14.6 LCD 显示
- [`docs/roadmap.md`](./roadmap.md) §55 开发阶段总表
- [`README.md`](../README.md) 当前路线速览
- [`firmware/esp32/src/display/display.h`](../firmware/esp32/src/display/display.h) API 定义
- [`firmware/esp32/src/display/display.cpp`](../firmware/esp32/src/display/display.cpp) 驱动实现
- [`docs/test-2026-09-09-phase1-wifi-voice-loop.md`](./test-2026-09-09-phase1-wifi-voice-loop.md) 回归基线
- [`docs/test-2026-09-27-dual-servo-voice.md`](./test-2026-09-27-dual-servo-voice.md) 回归基线
- [`docs/barge-in-known-issues.md`](./barge-in-known-issues.md) 已知限制

---

## 附录 F · 执行记录模板

```text
测试日期：__________
测试人：__________
固件版本（git commit）：__________
硬件版本：__________
PlatformIO env：esp32-s3-n16r8-wifi

TC-01 硬件接线：    [PASS / FAIL / SKIP] 备注：
TC-02 双 env 编译： [PASS / FAIL / SKIP] 备注：
TC-03 启动日志：    [PASS / FAIL / SKIP] 备注：
TC-04 启动三段：    [PASS / FAIL / SKIP] 备注：
TC-05 静态帧：      [PASS / FAIL / SKIP] 备注：
TC-06 刷新策略：    [PASS / FAIL / SKIP] 备注：
TC-07 LISTENING：   [PASS / FAIL / SKIP] 备注：
TC-08 THINKING：    [PASS / FAIL / SKIP] 备注：
TC-09 SPEAKING：    [PASS / FAIL / SKIP] 备注：
TC-10 SERVO：       [PASS / FAIL / SKIP] 备注：
TC-11 非阻塞：      [PASS / FAIL / SKIP] 备注：
TC-12 边界切换：    [PASS / FAIL / SKIP] 备注：
TC-13 长稳：        [PASS / FAIL / SKIP] 备注：
TC-14 回归：        [PASS / FAIL / SKIP] 备注：

结论：
  [x] LCD MVP 通过，可合入
  [ ] 有失败项，需修复后重测

故障记录：
```

---

## 附录 G · 回归对比基线（Phase 1 vs LCD MVP）

| 指标 | Phase 1 基线 | LCD MVP 期望 | 说明 |
|------|--------------|--------------|------|
| 首字延迟（TTS 开始播放） | < 3.5 s | < 3.7 s | 允许 < 200 ms 增加 |
| Loop 周期 | < 5 ms | < 5.5 ms | LCD 稳态 no-op 后影响应极小 |
| Flash 占用（wifi env） | ~14% | ~14-15% | 新增 Adafruit_ST7735 库约 30KB |
| RAM 占用（wifi env） | ~25% | ~26% | LCD 帧缓冲区使用 GFX buffer（默认关闭） |
| 稳态电流（Wi-Fi 连上） | ~200 mA | ~210-220 mA | LCD 背光约 10-20 mA |
| 语音链路端到端时延 | < 5 s | < 5.2 s | LCD 不改变状态机语义 |

**结论判定**：
- 所有指标均在可接受范围内 → LCD MVP PASS
- 任一指标劣化 > 10% → 需评审 LCD 集成点位置

---

## 版本

| 日期 | 版本 | 变更 | 作者 |
|------|------|------|------|
| 2026-09-27 | v1.0 | 初版；对应 LCD MVP 实施 | SenseNova |
