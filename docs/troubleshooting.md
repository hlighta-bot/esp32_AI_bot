# ESP32 Voice AI - 故障排查

> 串口、权限、无声、杂音、Gemini / Whisper / Edge TTS 报错以及系统故障定位原则。

---


# 42. 故障排查

## 42.1 找不到 ESP32

检查：

```bash
pio device list
```

如果没有设备：

```text
检查 USB 数据线
检查 USB 接口
检查 ESP32 是否上电
检查 USB 驱动
```

注意使用：

> USB 数据线，而不是只有充电功能的 USB 线。

---

# 43. VID / PID

如果程序依靠：

```text
ESP32_VID
ESP32_PID
```

自动寻找设备。

检查：

```text
pc/config.py
```

例如：

```python
ESP32_VID = ...
ESP32_PID = ...
```

Linux：

```bash
lsusb
```

查看 USB 设备：

```text
ID xxxx:xxxx
```

然后填入：

```text
VID = xxxx
PID = xxxx
```

---

# 44. Permission denied

Linux / WSL 如果出现：

```text
Permission denied
```

执行：

```bash
sudo usermod -a -G dialout $USER
```

然后：

> 重新登录系统或者重新启动终端。

检查：

```bash
groups
```

应该出现：

```text
dialout
```

---

# 45. ESP32 可以连接，但是没有声音

首先：

```bash
python send_wav.py
```

选择 WAV。

---

## 情况 A：有声音

说明：

```text
USB
 ↓
Serial
 ↓
ESP32
 ↓
I2S
 ↓
MAX98357A
 ↓
Speaker
```

基本正常。

问题可能在：

```text
ASR
LLM
TTS
WAV 转换
PCM
```

---

## 情况 B：完全没有声音

检查：

```text
GPIO16 → BCLK
GPIO17 → LRC
GPIO15 → DIN
```

以及：

```text
GND
VIN
SPK+
SPK-
```

检查：

```text
MAX98357A
I2S 配置
Sample Rate
Bit Depth
Channels
DMA
```

---

# 46. 有杂音

如果能播放，但是：

```text
声音失真
爆音
杂音
断断续续
```

检查：

### 1. I2S 参数

```text
Sample Rate
Bit Depth
Channels
```

### 2. PCM 格式

确认：

```text
Signed 16-bit PCM
```

### 3. DMA Buffer

适当增加 DMA Buffer。

### 4. 电源

MAX98357A 对电源质量比较敏感。

### 5. GND

确认：

```text
ESP32 GND
MAX98357A GND
```

共地。

---

# 47. Gemini 无法使用

检查：

```env
GEMINI_API_KEY=xxxx
```

以及：

```env
DEFAULT_LLM_ENGINE=gemini
```

运行：

```bash
python llm.py "你好"
```

如果单独运行也失败：

```text
问题在 Gemini / API 配置
```

如果：

```text
llm.py 正常
voice_chat.py 不正常
```

则继续检查：

```text
voice_chat.py
```

---

# 47.5 WSL2 下没有麦克风 / 列表为空

**症状**：

```text
$ python voice_chat.py --list-devices
（无输出）
```

或者 `sounddevice.query_devices()` 报 `Total devices: 0`，`aplay -l` 显示 `no soundcards found`。

**根因**：WSL2 内核（如 `6.6.87.2-microsoft-standard-WSL2`）不原生暴露宿主机的 ALSA 声卡。

**解决办法（推荐）**：使用打字输入模式（默认）。

```bash
python voice_chat.py --engine sensenova
> 你好
```

打字模式跳过 mic + ASR，直接：文本 → LLM → TTS → ESP32 播放。

**如需真语音**：
- 方案 A：改用 Windows 侧运行 `voice_chat.py`（WSL2 的 ESP32 串口需要在 Windows 侧用 Python 运行）
- 方案 B：在宿主 Windows 上开启 Hyper-V 音频直通，或在物理 Linux 机上运行
- 方案 C：USB 外接声卡（部分型号 WSL2 下能识别）

---

# 47.6 第一轮播放完就 `AttributeError: 'SerialTransport' object has no attribute 'in_waiting'`

**症状**：ESP32 能发出一句，之后程序崩溃，栈在：

```text
File ".../send_wav.py", line 227, in _wait_playback_finish
    if self.transport.in_waiting():
       ^^^^^^^^^^^^^^^^^^^^^^^^^
AttributeError: 'SerialTransport' object has no attribute 'in_waiting'
```

**根因**：`ESP32Player._wait_playback_finish` 轮询 `FINISHED` 日志行依赖 `transport.in_waiting()` / `readline()`，但模块路径下的 `transport_serial.py` 只实现了 `send/receive`。

**修复**：`transport_serial.py` 和 `transport_wifi.py` 已补齐这两个方法（在 `pc/transport_serial.py` 与 `pc/transport_wifi.py`）。如果本地版本落后，请同步：

```bash
git pull
```

或直接补上：

```python
# transport_serial.py
def in_waiting(self) -> int:
    return self.ser.in_waiting if self.ser else 0

def readline(self) -> bytes:
    return self.ser.readline() if self.ser else b''
```

---

# 47.7 ESP32 串口打开失败

**症状**：启动 `voice_chat.py` 时打印 `无法连接到 ESP32` 并退出。

**排查顺序**：

```bash
# 1. ESP32 是否被识别
lsusb | grep -i esp
ls /dev/ttyACM* /dev/ttyUSB*

# 2. 当前程序里的默认串口
grep "SerialTransport(port" pc/voice_chat.py
# 默认是 /dev/ttyACM0，若你的设备是 /dev/ttyUSB0 需修改

# 3. Linux 权限
groups | grep dialout
# 若没有 dialout 组：
sudo usermod -a -G dialout $USER
# 然后重新登录

# 4. 是否被串口监视器占用
pgrep -af "device monitor"
# 关闭监视器再启动
```

---

# 48. Whisper 无法识别

运行：

```bash
python asr.py audio.wav
```

检查：

```text
WHISPER_CLI
WHISPER_MODEL
```

例如：

```python
WHISPER_CLI = "/path/to/whisper-cli"

WHISPER_MODEL = "/path/to/ggml-tiny.bin"
```

确认模型文件存在。

---

# 49. Edge TTS 无法工作

先单独测试：

```bash
python text_to_speak.py "你好"
```

如果 TTS 失败：

```text
检查网络
检查 edge-tts
检查 ffmpeg
```

因为 Edge TTS 当前属于：

> **在线 TTS**

所以没有网络时不能正常工作。

---

# 50. 系统故障定位原则

完整系统：

```text
Mic
 ↓
ASR
 ↓
LLM
 ↓
TTS
 ↓
Transport
 ↓
ESP32
 ↓
I2S
 ↓
MAX98357A
 ↓
Speaker
```

发生问题时不要同时修改所有模块。

应该逐层排查：

```text
① Mic
 ↓
② ASR
 ↓
③ LLM
 ↓
④ TTS
 ↓
⑤ Transport
 ↓
⑥ ESP32
 ↓
⑦ I2S
 ↓
⑧ MAX98357A
 ↓
⑨ Speaker
```

每一层单独验证。

---
