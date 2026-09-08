# ESP32 Voice AI - PC 端模块

> 入口层、核心逻辑层（mic / asr / llm / tts）、send_wav 与 Transport 抽象。

---


# 5. 模块关系

## 5.1 入口层

### `pc/voice_chat.py`

整个系统的核心入口。

负责协调：

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
```

运行：

```bash
python voice_chat.py
```

或者：

```bash
python voice_chat.py --engine gemini
```

---

### `pc/send_wav.py`

独立测试 ESP32 音频播放。

用途：

```text
PC WAV
 ↓
Serial
 ↓
ESP32
 ↓
Speaker
```

运行：

```bash
python send_wav.py
```

或者：

```bash
python send_wav.py test.wav
```

---

# 6. 核心逻辑层

## 6.1 `mic.py`

负责麦克风录音。

主要功能：

```python
from mic import MicrophoneRecorder

rec = MicrophoneRecorder()

wav_bytes = rec.record(3.0)

wav_bytes = rec.record_until_silence()

devices = rec.list_devices()
```

默认：

```text
Sample Rate = 16000 Hz
Channels    = 1
```

---

# 7. ASR — `asr.py`

使用 Whisper.cpp 进行本地语音识别。

```text
WAV
 ↓
Whisper.cpp
 ↓
Text
```

使用：

```python
from asr import WhisperASR

asr = WhisperASR()

text = asr.transcribe_wav(wav_bytes)
```

或者：

```python
text = asr.transcribe_file("audio.wav")
```

指定中文：

```python
asr = WhisperASR(language="zh")
```

CLI：

```bash
python asr.py audio.wav
```

---

# 8. LLM — `llm.py`

统一的大模型接口。

支持：

```text
SenseNova
Ollama
Gemini
```

结构：

```text
                 ┌── SenseNova
                 │
LLMRouter ───────┼── Ollama
                 │
                 └── Gemini
```

使用：

```python
from llm import LLMRouter

llm = LLMRouter()

reply = llm.chat("你好，世界")
```

指定 Gemini：

```python
llm = LLMRouter(engine="gemini")
```

指定 Ollama：

```python
llm = LLMRouter(engine="ollama")
```

运行时切换：

```python
llm.set_engine("ollama")
```

CLI：

```bash
python llm.py "你好，请用一句话介绍你自己"
```

---

# 9. TTS — `tts.py`

使用 Microsoft Edge TTS。

流程：

```text
Text
 ↓
Edge TTS
 ↓
MP3
 ↓
WAV
 ↓
PCM
```

使用：

```python
from tts import TTSEngine

tts = TTSEngine()

wav_bytes = tts.synthesize("你好世界")
```

保存：

```python
tts.synthesize_to_file(
    "你好世界",
    "out.wav"
)
```

自定义音色：

```python
tts = TTSEngine(
    voice="zh-CN-YunxiNeural",
    rate="-20%",
    pitch="+1Hz"
)
```

默认：

```text
Voice = zh-CN-XiaoxiaoNeural
Rate  = -10%
Pitch = -1Hz
```

---

# 10. `text_to_speak.py`

用于测试：

```text
文字
 ↓
TTS
 ↓
WAV
 ↓
ESP32
 ↓
Speaker
```

运行：

```bash
python text_to_speak.py
```

进入交互模式。

也可以：

```bash
python text_to_speak.py "你好世界"
```

---

# 11. `send_wav.py`

负责 PC → ESP32 的音频传输。

主要接口：

```python
from send_wav import ESP32Player

player = ESP32Player()

player.connect()

player.play_wav_file("test.wav")

player.play_wav_bytes(wav_bytes)

player.play_pcm_bytes(pcm_bytes)

player.disconnect()
```

支持：

```text
WAV 文件
WAV bytes
裸 PCM bytes
```

---

# 12. Transport 传输层

为了以后从 USB Serial 扩展到 Wi-Fi，项目采用传输层抽象。

```text
                 TransportInterface
                        │
              ┌─────────┴─────────┐
              │                   │
              ▼                   ▼
     transport_serial.py   transport_wifi.py
              │                   │
              ▼                   ▼
          USB Serial             UDP
```

核心思想：

> 上层业务不应该关心底层到底使用串口还是 Wi-Fi。

因此：

```text
voice_chat.py
      │
      ▼
TransportInterface
      │
 ┌────┴────┐
 ▼         ▼
Serial     WiFi
```

以后可以直接：

```bash
python voice_chat.py --transport serial
```

或者：

```bash
python voice_chat.py --transport wifi
```

而不需要修改：

```text
ASR
LLM
TTS
```

---
