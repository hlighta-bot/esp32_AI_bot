# Barge-in / 打断 · 已知问题与后续任务

> 本文档记录当前 Barge-in 功能的**只读诊断**结论。
> 本轮 commit **不做 barge-in 优化**，作为后续任务。
> 所有结论以当前代码为准（截至 2026-09-26）。

---

## 0. 摘要

- **期望行为**：AI 正在播放 TTS 时，用户说"停"能立即打断。
- **实际行为**：用户需要**很大幅度**喊"停"、且**不能太早**说，才能触发。多数自然场景下打断失败。
- **语义链路（PC 端）状态**：**正确**（`wifi_server.py::_handle_rptf()` + `command_router.py` INTERRUPT 分支，12 条单测中 `test_tc04_active_interrupt` / `test_stop_never_goes_to_llm` / `test_prolonged_speaking_no_false_interrupt` 均通过）。
- **主要瓶颈**：ESP32 端 `checkPlaybackInterrupt()` 的检测节奏与阈值设计。

---

## 1. 当前实现

### 1.1 ESP32 端（`firmware/esp32/src/main.cpp`）

```cpp
#define INTERRUPT_RMS_THRESHOLD   1200   // main.cpp:77
#define INTERRUPT_GRACE_MS        500    // main.cpp:78
```

调用位置：**每个 PCM chunk 播放完之后**（main.cpp:661-670），而一个 4096B chunk @ 16 kHz mono 对应 **128ms** 音频。

`checkPlaybackInterrupt()` 内部：
- `g_mic.poll()` → `g_mic.read(buf, 256)`：每次只读 **256 samples = 16ms**
- 计算 AC RMS（去均值 + 均方 + 二分整数开根）
- `INTERRUPT_GRACE_MS = 500` 内直接返回 false

**实际时间线**（用户按 TTS 中段说"停"，持续 ~300ms）：

| T (ms 起自播放开始) | 检测点 | 结果 |
|---|---|---|
| 128 | `checkPlaybackInterrupt(128)` | 128<500 → false |
| 256 | `checkPlaybackInterrupt(256)` | 256<500 → false |
| 384 | `checkPlaybackInterrupt(384)` | 384<500 → false |
| **512** | **首次真正检测** | ✅ 才生效 |
| 640 | 第二次检测 | ✅ |

用户"停"发生在 0-512ms 之间 → **几乎必败**。

### 1.2 麦克风数据（`mic_adc.cpp` + `mic_adc.h`）

- MAX9814 → ADC1 GPIO1，50 kHz raw → 12-bit → phaseStep=20971 重采样到 16 kHz
- 一阶 LP α=6554/8192 → ×2 软件增益（`MIC_GAIN_SHIFT=1`）
- 每 2 ms 拉一次（`MIC_POLL_US=2000`），Ring buffer 4096 samples（256 ms）

注释中记录的 RMS 参考值（main.cpp:47-75）：
- 静默 rms ≈ 100-140
- 喇叭回声 rms ≈ 100-500
- **用户正常说话 rms ≈ 200-900**
- 用户大声"停" rms ≈ 800-2000+
- 阈值 1200

**问题**：正常音量的"停"（rms 200-900）**根本达不到 1200**。

### 1.3 PC 端（`wifi_server.py` + `command_router.py`）

`INTERRUPT_WORDS = ["停", "停止", "别说了", "等一下", "闭嘴"]`，包含匹配。逻辑正确。

**依赖问题**：PC 端 INTERRUPT 分支必须**先收到一次新的 RPTF**（即用户"停"被录制成一次新录音）才能生效。若 ESP32 端 `checkPlaybackInterrupt()` 没有把 TTS 打断，用户说完"停"后 TTS 继续播放 → 麦克风数据在 `MicUploader` 里被 `_waitingForPlayback=true` 全部丢弃 → PC 根本收不到"停"的录音 → 语义链路无从触发。

---

## 2. 根因排序

| # | 问题 | 严重程度 | 修复成本 |
|---|------|----------|----------|
| 1 | 检测频率过低（每 128ms 一次） | 🔴 高 | 中（需改 `playPCM` 结构） |
| 2 | 宽限期 500ms 过长 | 🔴 高 | 极低（改 1 个宏） |
| 3 | 阈值 1200 高于正常说话幅度 | 🟠 中 | 极低（改 1 个宏） |
| 4 | `i2s_write(... portMAX_DELAY)` 阻塞 | 🟠 中 | 中（需改 `playChunk`） |
| 5 | 无 AEC，TTS 回声与人声混叠 | 🟡 长期 | 高（架构级） |

---

## 3. A vs B 问题区分

现场排查 Barge-in 失败时，先区分是**问题 A**（ESP32 没停）还是**问题 B**（PC 没识别成 INTERRUPT）：

| 现象 | A：ESP32 没停 | B：PC 未识别 |
|---|---|---|
| ESP32 串口日志 | **无** `[play] interrupt detected, rms=...` | 有 |
| ESP32 串口日志 | 无 `[play] PLAY interrupted by user` | 有 |
| PC 日志 | 用户说完没触发 `[user]` 或触发的是 TTS 尾音 | `[user] 停` 出现但分类为 USER_TEXT |

**推断**：本项目的 Barge-in 失败**几乎全部落在 A 类**（前 4 个根因）。B 类需要用户在打断后再说一次"停"才能触发，且语义链路已验证正确，概率极低。

---

## 4. 建议的最小现场确认日志（未在本 commit 应用）

如果要 1 次现场测试就能定位到底是 A 还是 B，只需在 `checkPlaybackInterrupt()` 内加两条 `printf`（`main.cpp:482-540`）：

```cpp
// 宽限期内每 100ms 打印当前 RMS（观察 TTS 稳态回声）
if (elapsedMs < INTERRUPT_GRACE_MS) {
    static uint32_t lastLog = 0;
    g_mic.poll();
    int16_t dbuf[256]; size_t dn = g_mic.read(dbuf, 256);
    if (dn > 0 && (millis() - lastLog) >= 100) {
        lastLog = millis();
        // ... 计算 rms ...
        Serial.printf("[play] grace rms=%d n=%u elapsed=%u\n", rms, dn, elapsedMs);
    }
    return false;
}

// 主分支每次也打印 rms（观察用户说"停"时的实际值）
Serial.printf("[play] probe rms=%d (thr=%d) elapsed=%u n=%u\n",
              rms, INTERRUPT_RMS_THRESHOLD, elapsedMs, n);
```

**判读规则**：
- 宽限期内 rms 频繁 >1200 → TTS 稳态回声过大，需先降喇叭音量或做 AEC
- 宽限期后 rms 长期 <500 但用户在说话 → 麦克风距离/增益问题
- 用户说"停"时 rms 冲到 500-1200 但没触发 → 阈值过高
- 有 `[play] interrupt detected` 但 PC 无 `[interrupt]` → B 问题
- 有 `[play] interrupt detected` 但被分类为 USER_TEXT → 检查 ASR 文本（是否识别成"丁"、"平"等近似词）

---

## 5. 后续任务清单（按性价比排序）

### Task 1（推荐起点，改动 2 行）

调整两个宏：

```cpp
// main.cpp:77-78
// 原：
#define INTERRUPT_RMS_THRESHOLD   1200
#define INTERRUPT_GRACE_MS        500

// 建议：
#define INTERRUPT_RMS_THRESHOLD   900    // 覆盖用户正常音量"停"
#define INTERRUPT_GRACE_MS        150    // MAX98357A 起振 <10ms
```

**风险**：TTS 稳态回声可能误触发，需先用第 4 节的现场确认日志验证 TTS 稳态 rms <900。

### Task 2（中等改动）

把 `checkPlaybackInterrupt()` 的检测点从"每 chunk 末尾"改成"每 20ms"：
- 在 `playChunk()` 内改为非阻塞 I2S 轮询
- 或引入独立的 FreeRTOS 检测任务

真正修复根因 #1 和 #4，但需要动 `playChunk()` / `playPCM()` 结构。

### Task 3（架构级）

FreeRTOS 双任务：
- 播放任务：独立 `i2s_write` + ACK
- 检测任务：以 20ms 周期跑 `g_mic.poll()` + RMS，超过阈值置 `_interruptFlag`
- 播放任务轮询 flag，看到即停

改动较大，需要引入 `TaskHandle_t` 和 `g_mic` 的互斥锁。**当前阶段不建议**。

### Task 4（算法级）

半双工 AEC：在 `checkPlaybackInterrupt()` 里减去已知 TTS PCM 参考信号（延迟对齐后相减），从源头消除回声。属于进阶优化，超出当前最小修改范围。

---

## 6. 现场快速验证清单（后续执行时按序）

1. **安静房间，AI 播长 TTS，用户不说话**：观察 grace 期日志，判断 TTS 稳态回声 rms。若 >900，必须先降喇叭音量或做 AEC。
2. **播放中用户 1 米外说"停"**：观察是否有 `[play] interrupt detected`。若仍无 → Task 1 未生效或需进一步调低阈值。
3. **确认 `PLAY interrupted by user` 打印后 PC 500ms 内是否收到新一轮 `[user] 停`**：验证 B 链路。
4. **如果 B 链路失败**：检查 `INTERRUPT_WORDS` 是否包含实际识别出的文本（ASR 可能识别成"丁"、"平"等近似词）。

---

## 7. 明确不做

- 本 commit **不修改任何宏**（保持 `INTERRUPT_RMS_THRESHOLD=1200`、`INTERRUPT_GRACE_MS=500`）
- 本 commit **不修改 `playPCM()` / `playChunk()` 结构**
- 本 commit **不引入 FreeRTOS 任务分离**
- 本 commit **不实现 AEC**

以上作为独立后续任务，需要单独 commit + 独立验证。

---

## 8. 参考

- 代码：`firmware/esp32/src/main.cpp` L46-78, L479-541, L585-674
- 代码：`firmware/esp32/src/audio/mic_adc.cpp` L88-231（ADC → 16kHz 处理链）
- 代码：`firmware/esp32/src/audio/mic_uploader.cpp` L406-474（播放期间 discard）
- 代码：`pc/wifi_server.py` L761-774（INTERRUPT 分支）
- 代码：`pc/command_router.py` L173-175（INTERRUPT 包含匹配）
- 测试：`pc/tests/test_command_router.py`（TC-04 / TC-11 / TC-12）
- 相关文档：`docs/architecture.md` §8（Interrupt / "停"）

---

## 版本

- 首次归档：2026-09-26
- 触发背景：本轮 commit 涉及 wake word 从"大聪明"改为"你好"、TCP 探针修复，用户希望 Barge-in 问题**只记录不修改**
