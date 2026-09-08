# ESP32 Voice AI - 测试

> 推荐的第一次测试顺序（Test 1-6）。

---


# 41. 推荐的第一次测试顺序

不要第一次就直接运行：

```bash
python voice_chat.py
```

建议逐层测试。

---

## Test 1：ESP32 播放

首先确认：

```text
ESP32
 ↓
I2S
 ↓
MAX98357A
 ↓
Speaker
```

正常。

---

## Test 2：WAV → ESP32

运行：

```bash
python send_wav.py
```

选择：

```text
test.wav
```

如果成功播放：

```text
PC WAV
 ↓
Serial
 ↓
ESP32
 ↓
Speaker
```

说明播放链路正常。

---

## Test 3：TTS → ESP32

运行：

```bash
python text_to_speak.py "你好，这是ESP32语音测试"
```

如果能正常听到声音：

```text
Edge TTS
 ↓
WAV
 ↓
ESP32
 ↓
Speaker
```

正常。

---

## Test 4：ASR

运行：

```bash
python asr.py audio.wav
```

确认：

```text
WAV
 ↓
Whisper
 ↓
Text
```

正常。

---

## Test 5：LLM

运行：

```bash
python llm.py "你好，请用一句话介绍你自己"
```

确认：

```text
Text
 ↓
Gemini
 ↓
Text
```

正常。

---

## Test 6：完整系统

最后：

```bash
python voice_chat.py --engine gemini
```

---
