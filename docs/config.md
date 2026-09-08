# ESP32 Voice AI - 配置与凭证

> config.py 配置项、默认参数、`.env` 示例以及 Gemini / Ollama / SenseNova / Whisper.cpp 的接入方式。

---


# 13. 配置中心 — `config.py`

所有重要配置集中在：

```text
pc/config.py
```

不要把配置散落到各个 Python 文件中。

主要配置：

| 分类      | 参数                      | 说明                |
| ------- | ----------------------- | ----------------- |
| ASR     | `WHISPER_CLI`           | Whisper.cpp 可执行文件 |
| ASR     | `WHISPER_MODEL`         | Whisper 模型        |
| ASR     | `WHISPER_LIB`           | Whisper library   |
| ASR     | `GGML_LIB`              | GGML library      |
| LLM     | `SENSENOVA_API_KEY`     | SenseNova API Key |
| LLM     | `SENSENOVA_URL`         | SenseNova API     |
| LLM     | `SENSENOVA_MODEL`       | SenseNova 模型      |
| LLM     | `OLLAMA_URL`            | Ollama 地址         |
| LLM     | `OLLAMA_MODEL`          | Ollama 模型         |
| LLM     | `GEMINI_API_KEY`        | Gemini API Key    |
| LLM     | `DEFAULT_LLM_ENGINE`    | 默认 LLM            |
| LLM     | `SYSTEM_PROMPT`         | 系统提示词（控制回复风格） |
| TTS     | `TTS_VOICE`             | Edge TTS 音色       |
| TTS     | `TTS_RATE`              | 语速                |
| TTS     | `TTS_PITCH`             | 音调                |
| Audio   | `RECORD_SAMPLE_RATE`    | 录音采样率             |
| Audio   | `RECORD_CHANNELS`       | 声道数               |
| Audio   | `RECORD_DEVICE`         | 录音设备              |
| ESP32   | `ESP32_BAUD`            | 串口波特率             |
| ESP32   | `ESP32_VID`             | USB VID           |
| ESP32   | `ESP32_PID`             | USB PID           |
| Audio   | `CHUNK_SIZE`            | PCM 分块大小          |
| Timeout | `ASR_TIMEOUT`           | ASR 超时            |
| Timeout | `LLM_TIMEOUT_SENSENOVA` | SenseNova 超时      |
| Timeout | `LLM_TIMEOUT_OLLAMA`    | Ollama 超时         |
| Audio   | `SILENCE_MAX_DURATION`  | 最大录音时间            |
| Audio   | `SILENCE_DURATION`      | 静音停止时间            |
| Audio   | `SILENCE_THRESHOLD`     | 静音阈值              |

---

# 14. 推荐默认参数

```python
RECORD_SAMPLE_RATE = 16000
RECORD_CHANNELS = 1
RECORD_DEVICE = None

ESP32_BAUD = 921600
CHUNK_SIZE = 4096

ASR_TIMEOUT = 120

LLM_TIMEOUT_SENSENOVA = 60
LLM_TIMEOUT_OLLAMA = 120

SILENCE_MAX_DURATION = 30
SILENCE_DURATION = 2
SILENCE_THRESHOLD = -45
```

---

# 15. 环境变量 `.env`

在：

```text
pc/.env
```

配置 API Key。

例如：

```env
SENSENOVA_API_KEY=你的_SENSENOVA_KEY

GEMINI_API_KEY=你的_GEMINI_KEY

OLLAMA_URL=http://localhost:11434
OLLAMA_MODEL=qwen2.5:7b

DEFAULT_LLM_ENGINE=gemini
```

注意：

```text
.env
```

不要提交到 Git。

`.gitignore`：

```gitignore
.env
```

---

# 16. Gemini

使用 Gemini：

```env
GEMINI_API_KEY=你的_API_KEY
DEFAULT_LLM_ENGINE=gemini
```

运行：

```bash
python voice_chat.py --engine gemini
```

如果：

```env
DEFAULT_LLM_ENGINE=gemini
```

则可以直接：

```bash
python voice_chat.py
```

---

# 17. Ollama

如果 PC 本地运行 Ollama：

```env
OLLAMA_URL=http://localhost:11434
OLLAMA_MODEL=qwen2.5:7b
```

运行：

```bash
python voice_chat.py --engine ollama
```

架构：

```text
Voice
 ↓
Whisper
 ↓
Ollama
 ↓
Edge TTS
 ↓
ESP32
```

这样即使没有云端 LLM，也可以实现本地 AI。

---

# 18. SenseNova

配置：

```env
SENSENOVA_API_KEY=你的_KEY
```

然后：

```bash
python voice_chat.py --engine sensenova
```

---

# 19. Whisper.cpp 配置

在 `config.py` 中配置：

```python
WHISPER_CLI = "/path/to/whisper-cli"

WHISPER_MODEL = "/path/to/ggml-tiny.bin"

WHISPER_LIB = "/path/to/build/src"

GGML_LIB = "/path/to/build/ggml/src"
```

例如：

```text
whisper.cpp
├── build/
│   ├── bin/
│   │   └── whisper-cli
│   ├── src/
│   └── ggml/
│
└── models/
    └── ggml-tiny.bin
```

---
