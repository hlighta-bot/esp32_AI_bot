"""
文字转语音播放入口
======================

将文字通过 Edge TTS 合成语音，然后通过串口发送到 ESP32 扬声器播放。

这是文字 → TTS → WAV → 串口 → ESP32 → 扬声器的完整链路。

依赖:
  - edge-tts: 语音合成
  - pydub:    MP3 → WAV 转换 (需系统安装 ffmpeg)
  - pyserial: 串口通信
  - numpy, soundfile, scipy: 音频处理

CLI 使用:
  python text_to_speak.py                    # 输入模式 (交互)
  python text_to_speak.py "你好世界"         # 直接播放指定文字

模块化使用:
  from tts import TTSEngine
  from send_wav import ESP32Player

  tts = TTSEngine()
  player = ESP32Player()
  player.connect()
  wav = tts.synthesize("你好")
  player.play_wav_bytes(wav)
"""

import sys

from tts import TTSEngine
from send_wav import ESP32Player


def speak(text, player, tts):
    """将文字合成语音并播放

    Args:
        text: 要播放的文字
        player: ESP32Player 实例 (已连接)
        tts: TTSEngine 实例

    Returns:
        bool: 播放成功返回 True
    """
    wav_bytes = tts.synthesize(text)
    if wav_bytes is None:
        return False

    # WAV bytes → ESP32 播放
    success = player.play_wav_bytes(wav_bytes)
    if not success:
        return False

    return True


def interactive_mode(player, tts):
    """交互模式 - 循环输入文字并播放

    输入 quit/exit 退出。

    Args:
        player: ESP32Player 实例
        tts: TTSEngine 实例
    """
    while True:
        try:
            text = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if not text:
            continue

        if text.lower() in ("quit", "exit", "q"):
            break

        speak(text, player, tts)


def main():
    """主入口

    支持两种模式:
      1. 交互模式: python text_to_speak.py
      2. 直接播放: python text_to_speak.py "你好世界"
    """
    # 创建模块实例
    tts = TTSEngine()
    player = ESP32Player()

    if not player.connect():
        sys.exit(1)

    try:
        # 判断模式
        if len(sys.argv) > 1:
            # 直接播放模式: python text_to_speak.py "你好"
            text = " ".join(sys.argv[1:])
            speak(text, player, tts)
        else:
            # 交互模式
            interactive_mode(player, tts)
    finally:
        player.disconnect()


if __name__ == "__main__":
    main()
