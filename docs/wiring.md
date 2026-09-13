# ESP32 Voice AI - 硬件接线

> 本文档定义 ESP32-S3 N16R8 与 MAX98357A I2S 数字功放之间的物理接线。
> 引脚定义与固件一致：[`firmware/esp32/src/main.cpp`](../firmware/esp32/src/main.cpp)。

---

## 1. 硬件清单

| 组件 | 型号 | 用途 |
|------|------|------|
| MCU | ESP32-S3 N16R8 | 主控 + I2S 主设备 + Wi-Fi STA |
| 功放 | MAX98357A | I2S 数字音频功放 |
| 扬声器 | 3W / 8Ω 或以上 | 音频输出 |
| 麦克风 | MAX9814 | 模拟 MEMS 麦克风（走 ADC1 GPIO1） |
| 数据线 | USB-C / USB-A → USB-C | PC ↔ ESP32 串口（烧录 / 日志 / 回退） |
| Wi-Fi | ESP32-S3 内置 | 主链路（PC 或 aidlux） |

> ESP32-S3 N16R8：Flash 16 MB，PSRAM 8 MB，双核 240 MHz。
> 与板子规格冲突时以实际开发板为准。

---

## 2. I2S 接线表（当前配置）

### 2.1 信号线

| ESP32-S3 | MAX98357A | 功能 | 备注 |
|----------|-----------|------|------|
| **GPIO16** | **BCLK** | 位时钟 (SCLK) | I2S_BCLK |
| **GPIO17** | **LRCLK / LRC** | 左右声道时钟 (WS) | I2S_LRC |
| **GPIO15** | **DIN** | 音频数据 | I2S_DIN，主控输出 |
| GND | GND | 地 | 必须共地 |

### 2.2 电源线

| ESP32-S3 | MAX98357A | 备注 |
|----------|-----------|------|
| 5V / VIN | VIN | 5V 供电 |
| GND | GND | 共地 |

### 2.3 扬声器

| 引脚 | 说明 |
|------|------|
| MAX98357A **SPK+** | 扬声器正极 |
| MAX98357A **SPK−** | 扬声器负极 |

---

## 3. MAX9814 麦克风接线（Phase 1）

MAX9814 是**模拟** MEMS 麦克风（不是 I2S），输出模拟音频电压（静态 ~VDD/2），
直接接到 ESP32-S3 ADC1 通道。

### 3.0 使用的模块（5 引脚 breakout 板）

| 参数 | 值 |
|------|----|
| 模块尺寸 | 25.58 mm × 14.28 mm |
| 引脚 | `GND` / `VDD` / `Out` / `GAIN` / `AR` |
| 电源 | 2.7 V ~ 5.5 V（推荐 3V3 与 ESP32 同源，方便共地） |
| 静态输出（默认） | DC-coupled，VDD/2（3V3 下 ≈ 1.65 V） |
| 最大输出电压摆幅 | 2 Vpp |

### 3.1 信号与电源

| 模块引脚 | ESP32-S3 | 备注 |
|---------|----------|------|
| **GND**  | **GND**    | 必须与 ESP32 共地 |
| **VDD**  | **3V3**    | 与 ESP32 同源 3V3；旁边加 100 nF 陶瓷电容到 GND（模块已含部分电路，仍建议外部再补一颗） |
| **Out**  | **GPIO1**  | ADC1_CH0，`mic_adc.cpp` 默认 `gpio=1` |
| **GAIN** | **悬空**   | +50 dB（默认推荐）；接 VDD → +60 dB；接 GND → +40 dB |
| **AR**   | **悬空**   | DC-coupled 模式（LOW），输出带 VDD/2 直流偏置——与固件 `-MIC_ADC_MID_VALUE` 去直流匹配 |

> **警告**：`AR` 千万不要接到 VDD。接 VDD 会切换成 AC-coupled（≈0 V 偏置），
> 固件里减 `2048` 会把正半周期全部削掉，导致声音只剩负半周期。
> 需要 AC-coupled 时同步改 `mic_adc.cpp` 的 `MIC_ADC_MID_VALUE`。

### 3.1.1 增益选择对照

| GAIN 引脚接法 | 增益 | 使用场景 |
|--------------|-----|---------|
| 接 GND | +40 dB | 声音太大 / 环境噪声高 / 削波 |
| **悬空（默认）** | **+50 dB** | **近场语音对话（本项目默认）** |
| 接 VDD | +60 dB | 远距离拾音 / 极安静环境（易底噪） |

改增益需要物理接线改动，不需要改固件；如果调完仍不理想，再回来调
[`mic_adc.cpp`](../firmware/esp32/src/audio/mic_adc.cpp) 里的 `MIC_GAIN_SHIFT`（软件增益，默认 ×2）。

### 3.2 GPIO 选择理由

| 约束 | 说明 |
|------|------|
| ADC1 通道 | GPIO1–GPIO9 是 ADC1；Wi-Fi 启用时 ADC2 被占用，只能用 ADC1 |
| 避开 strapping pins | GPIO0、GPIO3、GPIO45、GPIO46 涉及启动模式 |
| 避开 USB D-/D+ | GPIO19、GPIO20 |
| 避开 I2S 播放线 | GPIO15、GPIO16、GPIO17 已给 MAX98357A |
| **推荐** | **GPIO1（ADC1_CH0）** |

### 3.3 供电与噪声

- MAX9814 输出为**单电源**模拟信号，静态电压约 `VDD/2`（3V3 供电下约 1.65V，对应 12-bit ADC ≈ 2048）
- [`mic_adc.cpp`](../firmware/esp32/src/audio/mic_adc.cpp) 在固件里做去直流偏置（`- 2048`），把直流分量去除
- 建议在 MAX9814 VDD 与 GND 之间加 100nF 陶瓷电容；若出现底噪，再加 10µF 电解
- MAX9814 Out → GPIO1 的走线尽量短（< 10 cm），避免长导线引入工频干扰

### 3.4 常见 MAX9814 故障

| 现象 | 可能原因 | 检查项 |
|------|----------|--------|
| 静默无响应 | Out 没接 GPIO1 | 万用表测 GPIO1 是否有约 1.65 V 静态电压 |
| 输出≈0 V | AR 接到了 VDD（切到 AC-coupled） | 把 AR 悬空；或同步改固件 `-MIC_ADC_MID_VALUE` |
| 输出削顶 / 声音过大 | GAIN 接到 VDD（+60 dB）且输入强 | GAIN 悬空（+50 dB）或接 GND（+40 dB） |
| 采集到全是噪声 | 供电不稳 / 未共地 | 加电容 + 共地 |
| 音量过小 | 增益不足 | GAIN 悬空→VDD，或增大 `MIC_GAIN_SHIFT`（默认 ×2） |
| 直流偏置异常 | VDD 不是 3V3 | 修改 `MIC_ADC_MID_VALUE` |

---

## 4. 完整接线图

```text
                ┌────────────────────┐
                │    ESP32-S3        │
                │    N16R8           │
                │                    │
  USB ─────────►│  USB-C / UART      │─── PC (烧录/日志/回退)
                │                    │
                │  Wi-Fi 天线 ────────│─── 2.4 GHz (主链路)
                │                    │
                │  GPIO1  ──── Out ──┼──┐
                │  3V3    ──── VDD ──┼──┤    MAX9814 (mic, 5-pin)
                │  GND    ──── GND ──┼──┤
                │   (悬空)   GAIN    │    ← +50 dB（默认）
                │   (悬空)   AR      ┘    ← DC-coupled（默认）
                │                    │
                │  GPIO16 ─── BCLK ──┼──┐
                │  GPIO17 ── LRCLK ──┼──┤    MAX98357A (amp)
                │  GPIO15 ──── DIN ──┼──┤
                │  5V     ──── VIN ──┼──┐┤
                │  GND    ──── GND ──┼──┤┤
                └────────────────────┘  │┤
                                        │┤
                ┌───────────────────────┘┤
                │                        │
                │  ┌───────────────────┐ │
                │  │    MAX98357A      │ │
                │  │  BCLK LRCLK DIN   │◄┘  VIN
                │  │                   │
                │  │  SPK+ SPK-        │
                │  └────────┬──────────┘
                │           │
                │           ▼
                │  ┌─────────────┐
                │  │   Speaker   │
                │  └─────────────┘
                │
```

Wi-Fi 主链路（无需接线）：

```text
  ESP32 (STA, mDNS esp32-voice.local)  ──TCP:8888──►  PC / aidlux wifi_server.py
```

---

## 5. I2S 参数

定义在 [`main.cpp`](../firmware/esp32/src/main.cpp) 顶部：

```cpp
#define I2S_PORT  I2S_NUM_1
#define I2S_BCLK  16
#define I2S_LRC   17
#define I2S_DIN   15

#define SAMPLE_RATE     16000
#define BITS_PER_SAMPLE 16
```

驱动配置（`setupI2S()`）：

| 参数 | 值 |
|------|----|
| Mode | MASTER / TX |
| Channel Format | RIGHT_LEFT（立体声） |
| Communication | STAND_I2S |
| DMA Buf Count | 8 |
| DMA Buf Len | 256 |
| Intr Alloc | LEVEL1 |
| APLL | 关闭 |
| TX Desc Auto Clear | 开启 |

### 5.1 Mono → Stereo 说明

PC 端发送的是 **Mono PCM**（单声道 16-bit）。ESP32 在 `playChunk()` 中把每个样本复制成双声道：

```cpp
for (size_t i = 0; i < samples; i++) {
    stereoBuffer[i * 2]     = mono[i];   // L
    stereoBuffer[i * 2 + 1] = mono[i];   // R
}
```

这样 MAX98357A 才能正常识别 WS 时钟边沿并输出音频。**不要省略这一步。**

---

## 6. GPIO 修改清单

如果你改硬件接线，需要同步改两处：

| 位置 | 修改 |
|------|------|
| [`firmware/esp32/src/main.cpp`](../firmware/esp32/src/main.cpp) 顶部 `#define I2S_*` | 更新引脚号 |
| [`README.md`](../README.md) 第 22 章 I2S 接线表 | 更新文档 |

GPIO 选择建议：

- 避开 ESP32-S3 的 strapping pins（GPIO0、GPIO3、GPIO45、GPIO46）
- 避开 USB D-/D+（GPIO19、GPIO20）
- 避开 flash / PSRAM 使用的 GPIO34~37
- MAX98357A 需要 3 根信号线，可选引脚范围很宽

---

## 7. 电源与地线注意

1. **必须共地**：ESP32 GND 与 MAX98357A GND 必须相连，否则会出现严重底噪。
2. **供电**：MAX98357A 推荐 5V 输入，可直接由 ESP32 的 5V/VIN 引出。
3. **电源滤波**：MAX98357A 对电源噪声敏感，建议在 VIN 附近加 100nF 陶瓷电容。
4. **供电电流**：驱动 3W/8Ω 扬声器时峰值约 300–500 mA。ESP32 USB 供电（500 mA）通常够，若声音爆音或杂音，改用独立 5V 电源。

---

## 8. 常见接线故障

| 现象 | 可能原因 | 检查项 |
|------|----------|--------|
| 无声 | 接线错误 | 万用表测 BCLK / LRCLK 是否有方波 |
| 无声 | GPIO 与固件不一致 | 对照第 4 节 |
| 无声 | 未共地 | 检查 GND |
| 无声 | 扬声器没接 SPK+ / SPK- | 检查焊点 |
| 有噪声 | 电源不稳 | 加电容 / 独立电源 |
| 有噪声 | 时钟线过长 | BCLK / LRCLK 尽量短，<10 cm |
| 爆音 | DMA buffer 不足 | 尝试增大 `dma_buf_count` 到 16 |
| 断续 | 采样率不匹配 | 确认 PC 端重采样到 16 kHz |

---

## 9. 验证步骤

1. 烧录固件后打开串口监视器：
   ```bash
   pio device monitor -b 921600
   ```
   应该看到：
   ```text
   =================================
   ESP32-S3 WAV AUDIO PLAYER
   =================================
   Sample Rate: 16000 Hz
   Bits: 16
   Channels: Mono input
   I2S initialized.
   READY
   ```

2. 用示波器测 GPIO17（LRC）：应看到与采样率同步的方波。

3. 用 `python send_wav.py test.wav` 发送一段测试音频，扬声器应发声。

---

## 10. 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1 | 2026-09-08 | 首版，从 README 与 main.cpp 抽取 |
| v2 | 2026-09-09 | Phase 1：新增 MAX9814 麦克风接线表（GPIO1）与完整接线图更新 |
| v3 | 2026-09-10 | §3 补全 5 引脚 breakout 板规格：GAIN 悬空 (+50 dB) / AR 悬空 (DC-coupled)；补充常见故障：AR→VDD 导致输出≈0 V、GAIN→VDD 削顶 |
