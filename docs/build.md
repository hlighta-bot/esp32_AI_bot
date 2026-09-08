# ESP32 Voice AI - 启动流程

> 完整启动步骤（Step 1-10）、最简启动命令与完整语音对话过程示例。

---

# 38. 完整启动流程

这是项目正式联调时推荐使用的流程。

---

## 38.1 第一步：连接 ESP32

将：

```text
ESP32-S3 N16R8
```

通过 USB 数据线连接 PC。

确认：

```bash
pio device list
```

可以发现设备。

---

## 38.2 第二步：进入固件目录

```bash
cd ~/projects/esp32-voice-ai/firmware/esp32
```

如果项目实际路径不同，以实际路径为准。

---

## 38.3 第三步：编译

```bash
pio run
```

确认编译成功。

---

## 38.4 第四步：烧录

```bash
pio run --target upload
```

烧录完成后 ESP32 会重新启动。

---

## 38.5 第五步：查看串口

```bash
pio device monitor -b 921600
```

确认 ESP32 已经启动。

应该看到类似：

```text
ESP32 Voice AI
Initializing...
I2S initialized
Ready
```

具体输出取决于 `main.cpp`。

---

## 38.6 第六步：退出串口监视器

退出：

```text
Ctrl + C
```

注意：

> 串口监视器不要与 PC 播放程序同时占用同一个串口。

---

## 38.7 第七步：进入 PC 目录

```bash
cd ~/projects/esp32-voice-ai/pc
```

---

## 38.8 第八步：激活 Python 环境

```bash
source esp32_voice_ai_env/bin/activate
```

Windows：

```powershell
.\esp32_voice_ai_env\Scripts\Activate.ps1
```

---

## 38.9 第九步：确认依赖

```bash
pip install -r requirements.txt
```

---

## 38.10 第十步：启动语音 AI

默认打字输入模式（WSL / 无麦克风环境推荐）：

```bash
python voice_chat.py --engine sensenova
```

如果要语音输入（需要 PC 麦克风）：

```bash
python voice_chat.py --engine sensenova --voice-input
```

如果 `.env` 里已经配好 `DEFAULT_LLM_ENGINE`，可以省略 `--engine`：

```bash
python voice_chat.py
```

---

# 39. 最终最简启动命令

系统全部配置完成以后：

### 固件更新

```bash
cd ~/projects/esp32-voice-ai/firmware/esp32
pio run --target upload
```

### 启动语音 AI

```bash
cd ~/projects/esp32-voice-ai/pc
source esp32_voice_ai_env/bin/activate
python voice_chat.py --engine sensenova
```

然后：

```text
[text-input mode] 在 > 后直接输入文字，回车发送；q/exit 退出
>
```

出现 `>` 提示符后直接输入文字，回车发送。

---

# 40. 完整对话过程

## 40.1 打字模式（默认，WSL / 无麦克风环境）

启动：

```bash
python voice_chat.py --engine sensenova
```

启动后屏幕：

```text
[text-input mode] 在 > 后直接输入文字，回车发送；q/exit 退出
```

用户在 `>` 后输入：

```text
> 你好
```

处理流程：

```text
用户输入 → LLM → Edge TTS → WAV → PCM → ESP32 → I2S → 扬声器
```

屏幕输出：

```text
> 你好
[bot] 嗨，我在这呢，有啥事儿？
```

同时 ESP32 扬声器播放同样的回复。回到 `>` 继续下一轮，`q/exit` 退出。

---

## 40.2 语音模式（需要 PC 麦克风）

启动：

```bash
python voice_chat.py --engine sensenova --voice-input
```

启动后屏幕：

```text
[voice-input mode] 在 > 后按 Enter 开始录音，说话，静音约 2 秒结束
```

在 `>` 后按 Enter 开始录音，静音 2 秒自动结束。屏幕上依次出现：

```text
Recording...
Transcribing...
LLM Responding...
```

识别文本：

```text
你好
```

屏幕输出：

```text
[bot] 你好！有什么可以帮你的吗？
```

处理流程：

```text
麦克风 → Whisper → LLM → Edge TTS → WAV → PCM → ESP32 → I2S → 扬声器
```

> 注意：`--list-devices` 在无音频设备（如 WSL2）环境下不会有输出，属正常现象，不是错误。见 [`troubleshooting.md`](./troubleshooting.md) 第 47.5 节。

---

## 40.3 调整回复风格

系统提示词在 [`pc/config.py`](../pc/config.py) 的 `SYSTEM_PROMPT` 常量，默认让回复口语化、简短、无 Markdown、最多 5 句。临时覆盖：

```bash
SYSTEM_PROMPT="只用一句话回答。" python voice_chat.py --engine sensenova
```

持久化：把 `SYSTEM_PROMPT=...` 加到 `pc/.env` 或直接修改 `config.py`。

---
