"""
ESP32 Voice AI - PC 端模块包

模块:
  ESP32Player          - WAV/PCM 音频串口发送到 ESP32 播放
  TTSEngine            - 文字转语音 (Edge TTS)
  MicrophoneRecorder   - 麦克风录音 (sounddevice)
  WhisperASR           - 语音转文字 (Whisper.cpp)
  LLMRouter            - 大模型对话 (SenseNova / Ollama)

入口脚本:
  text_to_speak.py     - 文字 → TTS → ESP32 播放
  voice_chat.py        - 麦克风 → ASR → LLM → TTS → ESP32 (完整闭环)

使用示例:
  from tts import TTSEngine
  from send_wav import ESP32Player
  from config import TTS_VOICE

  tts = TTSEngine(voice=TTS_VOICE)
  wav_bytes = tts.synthesize("你好世界")

  player = ESP32Player()
  player.connect()
  player.play_wav_bytes(wav_bytes)
"""

__all__ = [
    "ESP32Player",
    "TTSEngine",
    "MicrophoneRecorder",
    "WhisperASR",
    "LLMRouter",
]
