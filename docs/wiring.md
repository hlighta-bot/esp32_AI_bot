# ESP32 Voice AI - 硬件接线

> 本文档定义 ESP32-S3 N16R8 与 MAX98357A I2S 数字功放之间的物理接线。
> 引脚定义与固件一致：[`firmware/esp32/src/main.cpp`](../firmware/esp32/src/main.cpp)。

---

## 1. 硬件清单

| 组件 | 型号 | 用途 |
|------|------|------|
| MCU | ESP32-S3 N16R8 | 主控 + I2S 主设备 |
| 功放 | MAX98357A | I2S 数字音频功放 |
| 扬声器 | 3W / 8Ω 或以上 | 音频输出 |
| 数据线 | USB-C / USB-A → USB-C | PC ↔ ESP32 串口 |

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

## 3. 完整接线图

```text
                ┌──────────────────┐
                │   ESP32-S3       │
                │  N16R8           │
                │                  │
  USB ─────────►│  USB-C / UART    │─── PC 串口
                │                  │
                │  GPIO16 ─── BCLK─┼──┐
                │  GPIO17 ── LRCLK─┼──┤
                │  GPIO15 ──── DIN─┼──┤
                │  5V ────────────┼──┐┤
                │  GND ───────────┼──┤┤
                └──────────────────┘  │┤
                                      │┤
                ┌─────────────────────┘┤
                │                      │
                │  ┌─────────────────┐ │
                │  │   MAX98357A     │ │
                │  │                 │◄┘  VIN
                │  │  BCLK   LRCLK   │
                │  │  DIN            │
                │  │                 │
                │  │  SPK+ SPK-      │
                │  └────────┬────────┘
                │           │
                │           ▼
                │  ┌─────────────┐
                │  │   Speaker   │
                │  └─────────────┘
                │
```

---

## 4. I2S 参数

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

### 4.1 Mono → Stereo 说明

PC 端发送的是 **Mono PCM**（单声道 16-bit）。ESP32 在 `playChunk()` 中把每个样本复制成双声道：

```cpp
for (size_t i = 0; i < samples; i++) {
    stereoBuffer[i * 2]     = mono[i];   // L
    stereoBuffer[i * 2 + 1] = mono[i];   // R
}
```

这样 MAX98357A 才能正常识别 WS 时钟边沿并输出音频。**不要省略这一步。**

---

## 5. GPIO 修改清单

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

## 6. 电源与地线注意

1. **必须共地**：ESP32 GND 与 MAX98357A GND 必须相连，否则会出现严重底噪。
2. **供电**：MAX98357A 推荐 5V 输入，可直接由 ESP32 的 5V/VIN 引出。
3. **电源滤波**：MAX98357A 对电源噪声敏感，建议在 VIN 附近加 100nF 陶瓷电容。
4. **供电电流**：驱动 3W/8Ω 扬声器时峰值约 300–500 mA。ESP32 USB 供电（500 mA）通常够，若声音爆音或杂音，改用独立 5V 电源。

---

## 7. 常见接线故障

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

## 8. 验证步骤

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

## 9. 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1 | 2026-09-08 | 首版，从 README 与 main.cpp 抽取 |
