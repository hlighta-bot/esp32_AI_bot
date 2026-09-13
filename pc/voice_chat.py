"""
语音 AI 完整闭环入口
======================

完整链路: 麦克风 → ASR → LLM → TTS → ESP32 → 扬声器

依赖:
  - edge-tts:       语音合成
  - pydub:          MP3 → WAV 转换 (需系统安装 ffmpeg)
  - sounddevice:    麦克风录音
  - requests:       LLM API 调用
  - pyserial:       串口通信
  - numpy, soundfile, scipy: 音频处理
  - whisper.cpp:    本地语音识别 (外部工具)

CLI 使用:
  python voice_chat.py                   # 打字输入模式（默认，无麦克风环境可用）
  python voice_chat.py --engine ollama   # 指定 LLM 引擎
  python voice_chat.py --voice-input     # 语音输入模式（需要 PC 麦克风）
  python voice_chat.py --list-devices    # 列出音频设备（仅语音模式用）

模块化使用:
  from mic import MicrophoneRecorder
  from asr import WhisperASR
  from llm import LLMRouter
  from tts import TTSEngine
  from send_wav import ESP32Player

  mic = MicrophoneRecorder()
  asr = WhisperASR()
  llm = LLMRouter()
  tts = TTSEngine()
  player = ESP32Player()

  player.connect()
  wav = mic.record_until_silence()
  text = asr.transcribe_wav(wav)
  reply = llm.chat(text)
  wav = tts.synthesize(reply)
  player.play_wav_bytes(wav)
"""

import sys

from mic import MicrophoneRecorder
from asr import WhisperASR
from llm import LLMRouter
from tts import TTSEngine
from send_wav import ESP32Player
from transport_serial import SerialTransport
from transport_wifi import WifiTransport
import config


# ============================================================
# 核心处理流程
# ============================================================

def voice_chat_round(mic, asr, llm, tts, player):
    """执行一次完整的语音对话

    流程: 录音 → ASR → LLM → TTS → 播放

    Args:
        mic:    MicrophoneRecorder 实例
        asr:    WhisperASR 实例
        llm:    LLMRouter 实例
        tts:    TTSEngine 实例
        player: ESP32Player 实例

    Returns:
        bool: 流程成功返回 True
    """
    # Step 1: 录音
    wav_bytes = mic.record_until_silence()
    if wav_bytes is None:
        return False

    # Step 2: ASR 识别
    user_text = asr.transcribe_wav(wav_bytes)
    if not user_text:
        return False

    # Step 3: LLM 对话
    reply = llm.chat(user_text)
    if not reply:
        return False

    print(f"[bot] {reply}")

    # Step 4: TTS + 播放
    wav_bytes = tts.synthesize(reply)
    if wav_bytes is None:
        return False

    if not player.play_wav_bytes(wav_bytes):
        return False

    return True


def text_chat_round(user_text, llm, tts, player):
    """执行一次纯文本对话：用户输入文本 → LLM → TTS → 播放

    用于 WSL / 无麦克风环境，跳过 mic + ASR 步骤。

    Args:
        user_text: 用户输入的文字
        llm:    LLMRouter 实例
        tts:    TTSEngine 实例
        player: ESP32Player 实例

    Returns:
        bool: 流程成功返回 True
    """
    if not user_text:
        return False

    # Step 1: LLM 对话
    reply = llm.chat(user_text)
    if not reply:
        return False

    print(f"[bot] {reply}")

    # Step 2: TTS + 播放
    wav_bytes = tts.synthesize(reply)
    if wav_bytes is None:
        return False

    if not player.play_wav_bytes(wav_bytes):
        return False

    return True


# ============================================================
# 交互模式
# ============================================================

def interactive_chat(player, engine=None, text_input=True):
    """交互式对话

    Args:
        player:     ESP32Player 实例
        engine:     LLM 引擎名（可选，如 sensenova/ollama/gemini）
        text_input: True 走打字输入模式（默认，跳过 mic+ASR）；
                    False 走语音输入模式（需要 PC 麦克风）

    输入命令:
      quit/exit/q - 退出
      device N - 选择麦克风设备 N（仅语音模式有效）
    """
    llm = LLMRouter(engine=engine)
    tts = TTSEngine()

    if text_input:
        print("[text-input mode] 在 > 后直接输入文字，回车发送；q/exit 退出")
        mic = None
        asr = None
    else:
        print("[voice-input mode] 在 > 后按 Enter 开始录音，说话，静音约 2 秒结束")
        mic = MicrophoneRecorder()
        asr = WhisperASR()

    while True:
        try:
            cmd = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if not cmd:
            continue

        if cmd.lower() in ("quit", "exit", "q"):
            break

        if cmd.lower().startswith("device"):
            if mic is not None:
                parts = cmd.split()
                if len(parts) > 1:
                    mic.device = int(parts[1])
            else:
                print("[text-input] 打字模式下不支持 device 命令")
            continue

        # 执行一次对话
        if text_input:
            text_chat_round(cmd, llm, tts, player)
        else:
            voice_chat_round(mic, asr, llm, tts, player)


# ============================================================
# 主入口
# ============================================================

def main():
    """主入口

    CLI 参数:
      --engine NAME      - LLM 引擎 (sensenova/ollama/gemini)
      --list-devices     - 列出音频设备后退出
      --transport TYPE   - 传输方式 (serial/wifi)
      --voice-input      - 语音输入模式（需要 PC 麦克风；默认是打字输入）
    """
    args = sys.argv[1:]

    # 列出设备模式
    if "--list-devices" in args:
        MicrophoneRecorder.list_devices()
        return

    # 解析引擎参数
    engine = None
    if "--engine" in args:
        idx = args.index("--engine")
        if idx + 1 < len(args):
            engine = args[idx + 1]
            args = args[:idx] + args[idx + 2:]

    # 解析传输方式参数
    transport_type = "serial"
    if "--transport" in args:
        idx = args.index("--transport")
        if idx + 1 < len(args):
            transport_type = args[idx + 1]
            args = args[:idx] + args[idx + 2:]

    # 解析输入模式参数（默认打字输入，--voice-input 切回语音输入）
    text_input = True
    if "--voice-input" in args:
        text_input = False
        args = [a for a in args if a != "--voice-input"]

    # 初始化传输层
    #   --transport wifi   → 连 config.ESP32_WIFI_IP（或环境变量 ESP32_IP）
    #   --transport serial → 使用默认串口（/dev/ttyACM0，可后续从 --port 覆盖）
    if transport_type == "wifi":
        transport = WifiTransport(ip=config.ESP32_WIFI_IP, port=config.WIFI_TCP_PORT)
    else:
        # 使用默认串口，通常是 /dev/ttyUSB0 或 COMx
        transport = SerialTransport(port="/dev/ttyACM0")
    player = ESP32Player(transport)
    if not player.connect():
        print("无法连接到 ESP32")
        sys.exit(1)

    try:
        interactive_chat(player, engine=engine, text_input=text_input)
    finally:
        player.disconnect()


if __name__ == "__main__":
    main()

