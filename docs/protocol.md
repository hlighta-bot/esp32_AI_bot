# ESP32 Voice AI - 通信协议

> 本文档定义 PC 与 ESP32-S3 之间的音频传输协议。
> 协议实现位于 [`pc/send_wav.py`](../pc/send_wav.py) 和 [`firmware/esp32/src/main.cpp`](../firmware/esp32/src/main.cpp)。

---

## 1. 协议概览

第一阶段使用 **USB Serial** 传输音频数据。协议为轻量级二进制协议，核心目标是**可靠地让 ESP32 播完 PCM 音频**。

```text
PC (TTS / send_wav)          ESP32 (I2S → 扬声器)
        │                            │
        │  ┌───────────────────────►  │  1. 发送 PLAY
        │  │ PLAY (4 bytes)          │
        │  │ uint32_le size (4B)     │
        │  │ chunk_0 (≤4096 B)       │
        │  │ chunk_1 (≤4096 B)       │
        │  │ ...                     │
        │  │ chunk_N                 │
        │  │◄── ACK (每 chunk) ────── │  2. 每收一块回 ACK
        │  │                          │
        │  │  (超时后视为播放结束)      │  3. PC 端按 duration+buffer 计时
        ▼                            ▼
```

---

## 2. 物理层参数

| 项 | 值 | 定义位置 |
|----|----|---------|
| 波特率 | `921600` | [`config.py::ESP32_BAUD`](../pc/config.py) / [`main.cpp::SERIAL_BAUD`](../firmware/esp32/src/main.cpp) |
| 串口超时（读） | `2 s` | `config.py::SERIAL_TIMEOUT` |
| 串口超时（写） | `5 s` | `config.py::SERIAL_WRITE_TIMEOUT` |

> 921600 baud 对 16 kHz / 16-bit / Mono（256 kbps）有约 3.6× 余量。

---

## 3. 消息常量

| 常量 | 值 | 用途 |
|------|----|----|
| `PROTOCOL_PLAY` | `b"PLAY"` | 音频流开始标记 |
| `PROTOCOL_ACK` | `b"ACK"` | 每 chunk 确认 |
| `PROTOCOL_FINISHED` | `"PLAYBACK_FINISHED"` | 播放完成标记（**常量已定义，当前固件未发送**，PC 端预留检测） |

全部定义在 [`pc/config.py`](../pc/config.py)。

---

## 4. 帧格式

一次完整的播放由「头 + 若干 chunk + 结束标记」构成：

```
┌──────────┬────────────┬─────────────────┬───────────────────┐
│  PLAY    │  size (u32)│  chunk + ACK ×N │  (无显式结束帧)    │
│  4 字节  │   4 字节    │                  │                   │
└──────────┴────────────┴─────────────────┴───────────────────┘
```

### 4.1 头部（PC → ESP32）

| 字段 | 长度 | 编码 |
|------|------|------|
| 命令 | 4 | ASCII `"PLAY"` |
| 数据总长度 | 4 | **uint32 little-endian**（`struct.pack("<I", size)`） |

### 4.2 数据体（PC → ESP32 / ESP32 → PC）

按 `CHUNK_SIZE = 4096` 字节分块：

```text
PC → ESP32:  chunk (≤ 4096 B, PCM 数据)
ESP32 → PC:  "ACK" (3 B)
```

- 最后一个 chunk 允许小于 4096（但仍是偶数字节）
- ESP32 端在 [`main.cpp::playPCM()`](../firmware/esp32/src/main.cpp) 里对奇数长度做了 `chunkSize--` 保护

### 4.3 结束标记

**当前实现中 ESP32 端不发送显式结束帧。** 播放完成完全由 ESP32 端阻塞式 `i2s_write` 决定。

PC 端在 [`send_wav.py::_wait_playback_finish()`](../pc/send_wav.py) 中通过 `duration + PLAYBACK_TIMEOUT_BUFFER` 超时来判定播放结束：

```python
deadline = time.time() + duration + PLAYBACK_TIMEOUT_BUFFER
while time.time() < deadline:
    if self.transport.in_waiting():
        line = self.transport.readline()
        if PROTOCOL_FINISHED in line:   # 兼容未来固件
            break
```

> **未来改进方向**：让固件在 `playPCM()` 结束后发送 `PLAYBACK_FINISHED\n`，PC 端提前结束等待。当前代码已预留 `PROTOCOL_FINISHED` 检测逻辑。

---

## 5. 音频格式（载荷）

**载荷是原始 PCM，不是 WAV。**

| 项 | 值 |
|----|----|
| 采样率 | 16000 Hz |
| 位深 | 16-bit |
| 声道 | Mono（PC 端已混音） |
| 字节序 | Little-endian |
| 类型 | Signed int16 |

### 5.1 Mono → Stereo（ESP32 端完成）

MAX98357A 需要 L/R 两路同时给数据，因此 ESP32 端在 [`main.cpp::playChunk()`](../firmware/esp32/src/main.cpp) 中把 mono 复制成 stereo：

```cpp
stereoBuffer[i * 2]     = mono[i];   // L
stereoBuffer[i * 2 + 1] = mono[i];   // R
```

> PC 端不需要发 stereo 数据，节省一半带宽。

---

## 6. 状态机

### 6.1 PC 端

```
            ┌─────────┐
            │   IDLE  │
            └────┬────┘
                 │ _send_play()
                 ▼
            ┌─────────┐  send PLAY
            │  HEADER │
            └────┬────┘
                 │ send uint32 size
                 ▼
            ┌────────────┐  send chunk
     ┌─────►│  SENDING   │
     │      └────┬───────┘
     │           │ receive ACK
     │           ▼
     │      ┌─────────┐
     │◄─────│  WAIT ACK│  (下一 chunk)
     │      └─────────┘
     │           │
     │           │ 全部 chunk 完成
     │           ▼
     │      ┌──────────────┐
     │      │ WAIT FINISH  │
     │      └────┬─────────┘
     │           │ 超时 或 收到 FINISHED
     │           ▼
     └─────────►│   IDLE   │
                └──────────┘
```

### 6.2 ESP32 端

```
        ┌─────────┐
        │   IDLE  │
        └────┬────┘
             │ Serial.available() >= 4
             │ 读到 "PLAY"
             ▼
        ┌─────────────┐  readBytes (4)
        │ READ_HEADER │
        └────┬────────┘
             │ receiveUint32()
             ▼
        ┌──────────────┐
  ┌────►│ READ_CHUNK   │
  │     └────┬─────────┘
  │          │ receiveBytes(chunkSize)
  │          │ playChunk()
  │          │ Serial.write("ACK")
  │          ▼
  │     ┌───────────┐
  │     │ remaining │──► 循环
  │     │   -= size │
  │     └────┬──────┘
  │          │ remaining == 0
  │          ▼
  │     ┌──────────┐
  │     │  回到 IDLE │  (阻塞式 i2s_write，无显式 FINISH)
  │     └──────────┘
  │
  └────────────── (下一轮 PLAY 请求)
```

---

## 7. 时序与带宽计算

### 7.1 单 chunk 时间

```text
4096 bytes @ 921600 baud
≈ 4096 * 10 / 921600
≈ 44.4 ms
```

即每 44 ms 左右一个 chunk + ACK，符合 16 kHz 的实时性（1 秒 64 chunks）。

### 7.2 播放完成超时

PC 端等待超时窗口：

```text
timeout = audio_duration + PLAYBACK_TIMEOUT_BUFFER
       = audio_duration + 5.0 s
```

以 `PLAYBACK_POLL_INTERVAL = 0.05 s` 轮询 `in_waiting()`。

### 7.3 带宽余量

| 项 | 值 |
|----|----|
| 数据速率 | 16000 × 16 = 256 kbps |
| 波特率 | 921600 bps |
| 利用率 | ~27.8% |
| 余量 | 约 3.6× |

---

## 8. 错误处理

| 场景 | PC 行为 |
|------|--------|
| 发送 chunk 后未收到 ACK | 立即返回 `False`，中止播放 |
| 收到非 `ACK` 字节 | 立即返回 `False` |
| 等待超时（无显式 FINISHED 帧） | 返回 `True`（视为播完），继续下一轮 |
| 串口写入超时 | `SerialTimeoutException` → `False` |
| 串口读取不足字节 | `receive()` 返回短字节串 |

---

## 9. 传输层抽象

协议本身不绑定物理介质。项目定义了 [`transport.py::TransportInterface`](../pc/transport.py)：

```python
class TransportInterface(ABC):
    def connect(self) -> bool
    def disconnect(self)
    def send(self, data: bytes) -> bool
    def receive(self, size: int) -> bytes
```

当前实现：

| 实现 | 位置 | 介质 |
|------|------|------|
| `SerialTransport` | [`transport_serial.py`](../pc/transport_serial.py) | USB Serial (pyserial) |
| `WifiTransport` | [`transport_wifi.py`](../pc/transport_wifi.py) | UDP（预留） |

> Wi-Fi 传输是预留接口，UDP 实现尚未包含 ACK 处理逻辑，正式启用前需补齐。

---

## 10. 参考代码位置

| 功能 | 文件 |
|------|------|
| 协议常量 | [`pc/config.py`](../pc/config.py) |
| 分块发送 | [`pc/send_wav.py::ESP32Player._send_chunks`](../pc/send_wav.py) |
| 播放完成等待 | [`pc/send_wav.py::ESP32Player._wait_playback_finish`](../pc/send_wav.py) |
| 接收并播放 | [`firmware/esp32/src/main.cpp::playPCM`](../firmware/esp32/src/main.cpp) |
| 帧解析主循环 | [`firmware/esp32/src/main.cpp::loop`](../firmware/esp32/src/main.cpp) |

---

## 11. 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1 | 2026-09-08 | 首版，从 README 与源码抽取 |
