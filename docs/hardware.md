# ESP32 Voice AI - 硬件与音频

> ESP32-S3、MAX98357A、I2S 接线与 GPIO 说明；I2S 音频格式、音频数据流、串口通信与推荐协议、ACK 机制、921600 波特率理由。

---


# 20. 硬件

## 20.1 ESP32

目标平台：

```text
ESP32-S3 N16R8
```

建议：

```text
Flash : 8 MB
PSRAM : 16 MB
```

实际开发时应以开发板实际规格为准。

---

# 21. MAX98357A

MAX98357A 是本项目使用的 I2S 数字功放。

连接：

```text
ESP32-S3
   │
   │ I2S
   ▼
MAX98357A
   │
   ▼
Speaker
```

---

# 22. I2S 接线

当前示例：

| ESP32-S3 | MAX98357A   | 说明     |
| -------- | ----------- | ------ |
| GPIO16   | BCLK        | 位时钟    |
| GPIO17   | LRC / LRCLK | 左右声道时钟 |
| GPIO15   | DIN         | 音频数据   |
| GND      | GND         | 地      |
| 5V / VIN | VIN         | 电源     |

扬声器连接 MAX98357A 的：

```text
SPK+
SPK-
```

---

# 23. 注意 I2S GPIO

GPIO 只是当前示例。

实际使用时：

```text
GPIO16 → BCLK
GPIO17 → LRCLK
GPIO15 → DIN
```

必须与：

```text
firmware/esp32/src/main.cpp
```

中的 I2S 配置一致。

例如：

```cpp
#define I2S_BCLK 16
#define I2S_LRC  17
#define I2S_DOUT 15
```

如果修改硬件接线，也必须同步修改固件。

---

# 24. 麦克风

Phase 1 已把录音下沉到 ESP32：

```text
MAX9814 (analog MEMS)
 ↓
ESP32 ADC1 GPIO1
 ↓
mic_adc.cpp（去直流偏置 + 相位累加器 50k→16k + 一阶 LP + 增益 ×2）
 ↓
EnergyVad（RMS 阈值 + 静音超时）
 ↓
MicUploader（RECM 分块 + RPTF 段末）
 ↓
Wi-Fi / TCP :8888
 ↓
PC 或 aidlux wifi_server.py
```

详见 [`wiring.md §3`](./wiring.md)（GPIO1 接线表）与
[`firmware/esp32/src/audio/mic_adc.cpp`](../firmware/esp32/src/audio/mic_adc.cpp)。

---

# 25. 关于 MAX9814

MAX9814 是**模拟** MEMS 麦克风，输出模拟电压（静态 ~VDD/2），
走 ESP32-S3 的 ADC1 通道。当前 Phase 1 使用它作为默认麦克风。

我们使用的模块是 5 引脚 breakout 板（`GND / VDD / Out / GAIN / AR`），
丝印规格：`G=VDD → +60dB / 悬空 → +50dB / G=GND → +40dB`；
`Out` 走 `AR` 控制 DC 偏置：`AR=LOW → 1.25V offset`，`AR=HIGH → AC-coupled ≈0V`。

接线（详见 [`wiring.md`](./wiring.md)）：

```text
MAX9814  GND  ──► ESP32-S3  GND
MAX9814  VDD  ──► ESP32-S3  3V3   (旁边加 100nF 到 GND)
MAX9814  Out  ──► ESP32-S3  GPIO1  (ADC1_CH0)
MAX9814  GAIN ──► 悬空     (+50 dB，默认推荐)
MAX9814  AR   ──► 悬空     (DC-coupled，输出带 VDD/2 偏置)
```

> 切勿把 `AR` 接到 VDD。接 VDD 会切到 AC-coupled，直流偏置≈0，
> 但 [`mic_adc.cpp`](../firmware/esp32/src/audio/mic_adc.cpp) 仍按 `-MIC_ADC_MID_VALUE`
> 去直流 → 会削掉正半周期。若日后切 AC-coupled，需同步改固件。

固件预处理链（`mic_adc.cpp`）：

1. ADC 内部采样率 ~50 kS/s、12-bit unsigned
2. 去直流偏置（`- 2048`）→ 有符号 int16
3. 相位累加器精确重采样：50 kHz → 16 kHz
4. 一阶低通抗混叠（α = 6554 Q13，fc ≈ 7 kHz）
5. 软件增益 `<< MIC_GAIN_SHIFT`（默认 ×2）

> 后续如果追求更低噪声 / 更好信噪比，可切换到 I2S 数字麦克风
> （如 ICS-43434、INMP441、SPH0655），此时 `mic_adc.cpp` 可替换成
> 独立的 `i2s_mic.cpp`，`MicUploader` / `EnergyVad` 上层无需改动。

---

# 26. ESP32 固件

固件位置：

```text
firmware/esp32/
```

主要文件：

```text
platformio.ini
src/main.cpp
```

固件主要负责：

```text
Serial / Wi-Fi
     ↓
接收音频
     ↓
PCM Buffer
     ↓
I2S DMA
     ↓
MAX98357A
```

---

# 27. I2S 音频格式

当前建议使用：

```text
Sample Rate : 16000 Hz
Bit Depth   : 16 bit
Channels    : Mono
Format      : PCM
```

即：

```text
16 kHz
16-bit
Mono
PCM
```

但是需要注意：

> **16 kHz Mono 适合语音数据，但播放端 MAX98357A 并不要求只能使用这个格式。**

如果后续需要提高播放音质，可以使用：

```text
22050 Hz
24000 Hz
44100 Hz
48000 Hz
```

但 PC、TTS、ESP32 I2S 三端必须统一。

---

# 28. 音频数据流

当前推荐：

```text
Edge TTS
 ↓
MP3
 ↓
ffmpeg
 ↓
WAV
 ↓
PCM
 ↓
Serial
 ↓
ESP32
 ↓
I2S
```

ESP32 最终接收的是：

```text
PCM
```

而不是 MP3。

这样 ESP32 不需要承担 MP3 解码。

---

# 29. 串口通信

默认：

```text
921600 baud
```

通信：

```text
PC
 │
 │ USB Serial
 │ 921600
 ▼
ESP32
```

---

# 30. 推荐通信协议

可以采用：

```text
PLAY
[length]
[PCM DATA]
```

例如：

```text
PLAY
4-byte length
PCM chunk
PCM chunk
PCM chunk
...
```

ESP32：

```text
收到 PLAY
   ↓
读取长度
   ↓
读取 PCM
   ↓
写入 I2S
   ↓
ACK
```

---

# 31. ACK 机制

PC 发送数据：

```text
PC
 │
 │ PCM
 ▼
ESP32
 │
 │ ACK
 ▼
PC
```

例如：

```text
PC → ESP32
PCM 4096 bytes

ESP32 → PC
ACK
```

这样可以避免 PC 发送速度过快导致：

```text
Buffer Overflow
```

---

# 32. 为什么使用 921600

音频数据量：

```text
16000 samples/s
× 16 bit
× 1 channel
= 256000 bit/s
```

也就是：

```text
256 kbps
```

921600 baud 可以提供较大的传输余量。

实际有效吞吐量还会受到：

```text
USB
UART
协议
ACK
系统调度
```

影响。

---
