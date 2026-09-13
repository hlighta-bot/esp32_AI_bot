# ESP32 Voice AI - 通信协议

> 本文档定义 PC（或 aidlux）与 ESP32-S3 之间的音频传输协议。
> 协议实现位于 [`pc/send_wav.py`](../pc/send_wav.py)、[`pc/wifi_server.py`](../pc/wifi_server.py)、
> [`firmware/esp32/src/protocol/frame.h`](../firmware/esp32/src/protocol/frame.h)、
> [`firmware/esp32/src/main.cpp`](../firmware/esp32/src/main.cpp)。

---

## 1. 协议概览

Phase 1 起，主链路走 **Wi-Fi / TCP**：ESP32 主动连接 `wifi_server.py`，
同一条 TCP 连接上双向传输音频：

```text
ESP32 (MAX9814 → VAD → RECM 流 → RPTF)      PC / aidlux (wifi_server.py)
        │                                          │
        │  ── 上行 ─────────────►                  │
        │  RECM | flags | size | pcm (×N)          │   ASR → LLM → TTS
        │  RPTF | total_size                       │
        │                                          │
        │  ◄── 下行 ───────────────                │
        │  PLAY | size                             │
        │  chunk + ACK ×N                          │
        ▼                                          ▼
     I2S → MAX98357A → Speaker            (Wi-Fi / TCP :8888)
```

串口 `PLAY/ACK` 仍然保留，作为烧录、日志、无 Wi-Fi 环境的回退通道；
协议内容与下行 TCP 完全一致，只是物理层从 TCP 换成 UART 921600。

---

## 2. 物理层参数

### 2.1 Wi-Fi / TCP（主链路）

| 项 | 值 | 定义位置 |
|----|----|---------|
| 传输层 | TCP (`SOCK_STREAM`) | [`transport_wifi.py`](../pc/transport_wifi.py) / [`wifi_server.py`](../pc/wifi_server.py) |
| 端口 | `8888` | [`config.WIFI_TCP_PORT`](../pc/config.py) |
| Server 绑定 | `0.0.0.0`（局域网开放） | [`config.WIFI_HOST_BIND`](../pc/config.py) |
| 客户端 TCP_NODELAY | 开启（减少首包延迟） | `wifi_server.py` / `WifiClient` |
| TCP 读写超时 | `5 s` | `config.TCP_IO_TIMEOUT` |
| mDNS | `esp32-voice.local` | `firmware/network/wifi_client.cpp` |

> 客户端（ESP32）主动发起连接；PC 端 `wifi_server.py` 只做 listener。
> 这样即使路由器启用了 AP-Isolation，只要 ESP32 能到 PC，链路就能通。

### 2.2 USB Serial（回退链路）

| 项 | 值 | 定义位置 |
|----|----|---------|
| 波特率 | `921600` | [`config.ESP32_BAUD`](../pc/config.py) |
| 读超时 | `2 s` | `config.SERIAL_TIMEOUT` |
| 写超时 | `5 s` | `config.SERIAL_WRITE_TIMEOUT` |

> 921600 baud 对 16 kHz / 16-bit / Mono（256 kbps）有约 3.6× 余量。

---

## 3. 消息常量

| 常量 | 值 | 方向 | 用途 |
|------|----|------|------|
| `PROTOCOL_PLAY` | `b"PLAY"` | PC → ESP32 | 音频流开始标记 |
| `PROTOCOL_ACK`  | `b"ACK"`  | 双向 | 每 chunk 确认 |
| `PROTOCOL_REC`  | `b"RECM"` | ESP32 → PC | 录音 PCM 分块 |
| `PROTOCOL_RPTF` | `b"RPTF"` | ESP32 → PC | 本段录音结束 |
| `PROTOCOL_FINISHED` | `"PLAYBACK_FINISHED"` | ESP32 → PC | 播放完成标记（**当前固件未发送**，PC 端预留检测） |

固件侧常量定义在 [`protocol/frame.h`](../firmware/esp32/src/protocol/frame.h)，
PC 侧定义在 [`pc/config.py`](../pc/config.py)。

### 3.1 RECM flags 位

| 位 | 名称 | 含义 |
|----|------|------|
| bit0 | `REC_FLAG_FIRST` (`0x0001`) | 本段录音首包 |
| bit1 | `REC_FLAG_VAD_TRIGGER` (`0x0002`) | 该 chunk 覆盖 VAD 触发点 |

---

## 4. 帧格式

### 4.1 下行（PC → ESP32）

一次完整的播放由「头 + 若干 chunk + 结束标记」构成：

```
┌──────────┬────────────┬─────────────────┬───────────────────┐
│  PLAY    │  size (u32)│  chunk + ACK ×N │  (无显式结束帧)    │
│  4 字节  │   4 字节    │                  │                   │
└──────────┴────────────┴─────────────────┴───────────────────┘
```

**头部（PC → ESP32）**：

| 字段 | 长度 | 编码 |
|------|------|------|
| 命令 | 4 | ASCII `"PLAY"` |
| 数据总长度 | 4 | **uint32 little-endian**（`struct.pack("<I", size)`） |

**数据体**：按 `CHUNK_SIZE = 4096` 字节分块：

```text
PC → ESP32:  chunk (≤ 4096 B, PCM 数据)
ESP32 → PC:  "ACK" (3 B)
```

- 最后一个 chunk 允许小于 4096（但仍是偶数字节）
- ESP32 端在 [`main.cpp::playPCM()`](../firmware/esp32/src/main.cpp) 里对奇数长度做了 `chunkSize--` 保护

**结束标记**：

当前实现中 ESP32 端**不发送**显式结束帧。播放完成完全由阻塞式 `i2s_write` 决定。
PC 端在 [`send_wav.py::_wait_playback_finish()`](../pc/send_wav.py) 中通过
`duration + PLAYBACK_TIMEOUT_BUFFER` 超时来判定播放结束；同时兼容未来固件的 `PLAYBACK_FINISHED` 帧。

### 4.2 上行（ESP32 → PC）

由 VAD 触发一整段录音，段内用 `RECM` 帧流式上传，段末发 `RPTF`：

```
┌──────────┬───────────┬──────────────┬─────────────────┐
│  RECM    │ flags (u16)│ size (u32)   │ pcm (≤ 4096 B) │  ×N
│  4 字节  │   2 字节   │   4 字节      │                 │
└──────────┴───────────┴──────────────┴─────────────────┘
                    ...
┌──────────┬──────────────────┐
│  RPTF    │ total_size (u32) │   本段累计 PCM 字节数
│  4 字节  │    4 字节         │
└──────────┴──────────────────┘
```

- `flags` 位见 §3.1
- 单 chunk 上限 `4096` 字节（100 ms @ 16 kHz mono PCM）
- 单段录音硬上限 `MAX_SESSION_BYTES = 4 MB`（[`wifi_server.py`](../pc/wifi_server.py)）
- 所有数值字段一律 little-endian

---

## 5. 音频格式（载荷）

**载荷是原始 PCM，不是 WAV。**

| 项 | 值 |
|----|----|
| 采样率 | 16000 Hz |
| 位深 | 16-bit |
| 声道 | Mono |
| 字节序 | Little-endian |
| 类型 | Signed int16 |

### 5.1 上行（ESP32 端预处理）

MAX9814 是模拟 MEMS 麦克风，走 ESP32 ADC1。[`mic_adc.cpp`](../firmware/esp32/src/audio/mic_adc.cpp)
完成：

1. ADC 采样率 50 kS/s、12-bit unsigned
2. 去直流偏置（`- 2048`）→ 有符号
3. 相位累加器精确重采样：50 kHz → 16 kHz
4. 一阶低通抗混叠（α = 6554 Q13，fc ≈ 7 kHz）
5. 软件增益 `<< MIC_GAIN_SHIFT`（默认 ×2）

输出与下行 PCM 格式一致，`RECM` 帧无需再加 WAV 头。

### 5.2 下行（PC 端预处理）

- TTS 输出（MP3 → WAV）→ `resample_poly` → int16 mono PCM
- ESP32 在 [`main.cpp::playChunk()`](../firmware/esp32/src/main.cpp) 里做 Mono → Stereo 复制，
  以适配 MAX98357A 的双声道 I2S 要求

```cpp
stereoBuffer[i * 2]     = mono[i];   // L
stereoBuffer[i * 2 + 1] = mono[i];   // R
```

> PC 端不需要发 stereo 数据，节省一半带宽。

---

## 6. 状态机

### 6.1 ESP32 端（下行播放）

```
        ┌─────────┐
        │   IDLE  │
        └────┬────┘
             │ TCP/UART 读到 "PLAY"
             ▼
        ┌─────────────┐  read 4 bytes → uint32 size
        │ READ_HEADER │
        └────┬────────┘
             ▼
        ┌──────────────┐
  ┌────►│ READ_CHUNK   │
  │     └────┬─────────┘
  │          │ read chunkSize bytes
  │          │ playChunk()
  │          │ send "ACK"
  │          ▼
  │     ┌───────────┐
  │     │ remaining │──► 循环
  │     │   -= size │
  │     └────┬──────┘
  │          │ remaining == 0
  │          ▼
  │     ┌──────────┐
  │     │  回到 IDLE│  (阻塞式 i2s_write，无显式 FINISH)
  │     └──────────┘
  │
  └────────────── (下一轮 PLAY 请求)
```

### 6.2 ESP32 端（上行录音）

```
        ┌─────────┐
        │   IDLE  │   VAD_IDLE
        └────┬────┘
             │ EnergyVad → VAD_SPEAK
             ▼
        ┌─────────────┐  每 100 ms 发一个 RECM chunk
        │   REC       │  首包带 FLAG_FIRST | FLAG_VAD_TRIGGER
        └────┬────────┘
             │ EnergyVad → VAD_END (silenceMs 超时)
             ▼
        ┌─────────────┐  发送 RPTF | total_bytes
        │  REPORT     │
        └────┬────────┘
             │ 等待 server 下发 PLAY（复用同一 TCP）
             ▼
        ┌─────────┐
        │   IDLE  │
        └─────────┘
```

状态机代码位于 [`mic_uploader.cpp`](../firmware/esp32/src/audio/mic_uploader.cpp)。

### 6.3 PC Server 端（wifi_server.py）

```
        ┌─────────┐
        │  LISTEN │  TCP :8888
        └────┬────┘
             │ accept
             ▼
        ┌──────────────┐   每个客户端一个线程
        │ ClientSession │
        └────┬─────────┘
             │ 循环 _read_one_frame()
             ▼
        ┌──────────────┐
        │  RECM 累加    │  到 _session_pcm
        └────┬─────────┘
             │ 收到 RPTF
             ▼
        ┌──────────────┐
        │ pipeline()    │  ASR → LLM → TTS
        └────┬─────────┘
             ▼
        ┌──────────────┐
        │ 下发 PLAY    │  chunk + ACK ×N
        └────┬─────────┘
             │ 回 IDLE 监听下一段 RECM
             ▼
        ┌──────────────┐
        │ ClientSession │  循环
        └──────────────┘
```

---

## 7. 时序与带宽计算

### 7.1 单 chunk 时间（下行）

| 传输 | 单 chunk 4096 B 传输时间 | 备注 |
|------|--------------------------|------|
| USB Serial 921600 | ≈ 44.4 ms | 波特率决定 |
| TCP @ 2.4 GHz Wi-Fi | ≪ 1 ms | 实际受 Wi-Fi 抖动与 ACK 往返 |

即每 chunk 一个 ACK，延迟可预测。

### 7.2 上行 chunk 节奏

`mic_uploader.cpp` 每 `REC_CHUNK_INTERVAL_MS = 100 ms` 发一个 chunk。
16 kHz × 2 B × 0.1 s = 3200 B，取 4096 B 为安全上限。

### 7.3 播放完成超时

PC 端等待超时窗口：

```text
timeout = audio_duration + PLAYBACK_TIMEOUT_BUFFER
        = audio_duration + 5.0 s
```

以 `PLAYBACK_POLL_INTERVAL = 0.05 s` 轮询 `in_waiting()`。

### 7.4 带宽余量（下行）

| 项 | 值 |
|----|----|
| 数据速率 | 16000 × 16 = 256 kbps |
| USB Serial | 921600 bps，利用率 ~27.8%，余量 3.6× |
| Wi-Fi 2.4 GHz | 实际吞吐 ≥ 20 Mbps，余量 ≫ 100× |

---

## 8. 错误处理

| 场景 | PC 行为 |
|------|--------|
| 发送 chunk 后未收到 ACK | 立即返回 `False`，中止播放 |
| 收到非 `ACK` 字节 | 立即返回 `False` |
| 等待超时（无显式 FINISHED 帧） | 返回 `True`（视为播完），继续下一轮 |
| 串口 / TCP 写入超时 | `False` |
| 读不足字节（`_recv_exact` 返回 None） | ClientSession 关闭连接 |
| 上行 RECM chunk_size 非法 | 丢弃，尝试对齐 |
| 上行 RPTF 之前无 RECM | 忽略 |
| 上行单段 > 4 MB | 丢弃，重新等待下一段 |

---

## 9. 传输层抽象

协议本身不绑定物理介质。项目定义了 [`transport.py::TransportInterface`](../pc/transport.py)：

```python
class TransportInterface(ABC):
    def connect(self) -> bool
    def disconnect(self)
    def send(self, data: bytes) -> bool
    def receive(self, size: int) -> bytes
    def in_waiting(self) -> int
    def readline(self) -> bytes
```

当前实现：

| 实现 | 位置 | 介质 | 用途 |
|------|------|------|------|
| `SerialTransport` | [`transport_serial.py`](../pc/transport_serial.py) | USB Serial (pyserial) | 过渡期回退 |
| `WifiTransport`   | [`transport_wifi.py`](../pc/transport_wifi.py) | TCP（主链路） | PC 主动连 ESP32 时（如 `send_wav.py`） |
| `WifiTransportUdp`| [`transport_wifi.py`](../pc/transport_wifi.py) | UDP（调试用） | 非 `TransportInterface`，仅遗留调试 |

**方向说明**：

- **下行（PC → ESP32 播放 TTS）**：可用 `WifiTransport` 主动连 ESP32:8888，
  也可直接走串口。
- **上行（ESP32 → PC）**：ESP32 主动连 PC 的 `wifi_server.py:8888`。
  这条方向不走 `TransportInterface`（不需要），因为 Server 端由 `wifi_server.py`
  自己用 `socket` 直连处理。

---

## 10. aidlux 兼容性

`wifi_server.py`、`voice_pipeline.py` 与 `transport_wifi.py` 都只依赖 Python 标准库
（`socket` / `threading` / `struct` / `argparse`）以及项目内模块；未使用 `asyncio`，
未使用任何第三方网络库。

**迁移到 aidlux（Android 精简 Python）时**：

- `wifi_server.py`：可原样搬入，`threading` 与 `socket` 在 aidlux slim 环境可用。
- `voice_pipeline.py`：需要把 `asr` / `llm` / `tts` 三个外部依赖注入；
  aidlux 侧可用同端口转发到设备上的 whisper.cpp / 本地 LLM。
- 保持阻塞式 I/O，避免引入 `asyncio`（aidlux slim 版本可能不带完整 `select` / `epoll`）。
- 所有超时值集中在 [`config.py`](../pc/config.py)，迁移时统一调整即可。

---

## 11. 参考代码位置

| 功能 | 文件 |
|------|------|
| 协议常量（PC） | [`pc/config.py`](../pc/config.py) |
| 协议常量（固件） | [`firmware/esp32/src/protocol/frame.h`](../firmware/esp32/src/protocol/frame.h) |
| 分块发送（下行） | [`pc/send_wav.py::ESP32Player._send_chunks`](../pc/send_wav.py) |
| 播放完成等待 | [`pc/send_wav.py::ESP32Player._wait_playback_finish`](../pc/send_wav.py) |
| Wi-Fi Server 主循环 | [`pc/wifi_server.py::AudioServer`](../pc/wifi_server.py) |
| 上行帧解析 | [`pc/wifi_server.py::ClientSession._read_one_frame`](../pc/wifi_server.py) |
| 接收并播放 | [`firmware/esp32/src/main.cpp::playPCM`](../firmware/esp32/src/main.cpp) |
| 上行分块发送 | [`firmware/esp32/src/audio/mic_uploader.cpp`](../firmware/esp32/src/audio/mic_uploader.cpp) |
| Wi-Fi/TCP 客户端 | [`firmware/esp32/src/network/wifi_client.cpp`](../firmware/esp32/src/network/wifi_client.cpp) |

---

## 12. 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v1   | 2026-09-08 | 首版，从 README 与源码抽取（USB Serial 单向下行） |
| v2   | 2026-09-09 | 主链路切换至 Wi-Fi/TCP；新增上行 RECM/RPTF 帧；补充 aidlux 兼容性说明 |
