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

第一阶段：

```text
麦克风
 ↓
PC
 ↓
sounddevice
```

ESP32 不负责录音。

后续可以增加 ESP32 麦克风。

例如：

```text
ESP32-S3
   │
   ├── I2S MIC
   │
   ▼
PCM
   │
   ▼
Wi-Fi
   │
   ▼
PC / Server
```

---

# 25. 关于 MAX9814

如果后续使用 MAX9814：

```text
MAX9814
 ↓
Analog Audio
 ↓
ESP32 ADC
```

例如：

```text
MAX9814 OUT
     │
     ▼
ESP32 ADC GPIO
```

但对于 ESP32-S3 语音 AI，长期方案更推荐：

```text
数字 I2S 麦克风
```

例如 I2S MEMS 麦克风。

原因是数字麦克风可以避免：

```text
ADC
模拟噪声
增益控制
电源噪声
```

等问题。

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
