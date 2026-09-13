"""
voice_pipeline.py - ASR → LLM → TTS 流水线
=============================================

把 voice_chat.py 里的三段逻辑抽出为可复用函数：

    pipeline(wav_bytes) -> bytes
    pipeline_text(text) -> bytes

输入输出都是 WAV bytes；wifi_server.py 和 voice_chat.py 共用。

aidlux 兼容性：
  - 只依赖 asr / llm / tts（项目内已有）
  - 无 asyncio / 无第三方网络库
  - 每个函数都是阻塞式，aidlux 端可包一层线程运行
"""

import io
import wave
import struct

from asr import WhisperASR
from llm import LLMRouter
from tts import TTSEngine
import config


# ============================================================
# PCM → WAV bytes 转换（ESP32 上行的 RECM 是裸 PCM）
# ============================================================

def pcm_to_wav(pcm_bytes, sample_rate=16000, channels=1, sample_width=2):
    """把裸 PCM 包装成 WAV bytes，供 Whisper / TTS 消费

    Args:
        pcm_bytes:  int16 little-endian mono PCM
        sample_rate:采样率
        channels:   声道数
        sample_width:位深字节数（int16 = 2）

    Returns:
        WAV bytes
    """
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(sample_width)
        w.setframerate(sample_rate)
        w.writeframes(pcm_bytes)
    return buf.getvalue()


def wav_duration_seconds(wav_bytes):
    """从 WAV 头读时长（秒），用于日志与超时判断"""
    try:
        with io.BytesIO(wav_bytes) as bio:
            with wave.open(bio, "rb") as w:
                frames = w.getnframes()
                rate = w.getframerate()
                return frames / rate if rate else 0.0
    except Exception:
        return 0.0


# ============================================================
# 核心流水线
# ============================================================

_asr_singleton = None
_tts_singleton = None


def _get_asr():
    global _asr_singleton
    if _asr_singleton is None:
        _asr_singleton = WhisperASR()
    return _asr_singleton


def _get_tts():
    global _tts_singleton
    if _tts_singleton is None:
        _tts_singleton = TTSEngine()
    return _tts_singleton


def transcribe(wav_bytes):
    """WAV → 文本；失败返回 None"""
    try:
        return _get_asr().transcribe_wav(wav_bytes)
    except Exception as e:
        print(f"[pipeline] ASR failed: {e}")
        return None


def synthesize(text):
    """文本 → WAV bytes；失败返回 None"""
    try:
        return _get_tts().synthesize(text)
    except Exception as e:
        print(f"[pipeline] TTS failed: {e}")
        return None


def pipeline(wav_bytes, llm_engine=None):
    """完整流水线：WAV → ASR → LLM → TTS → WAV

    Args:
        wav_bytes: 输入 WAV bytes（来自 ESP32 上行或 PC 麦克风）
        llm_engine: LLM 引擎名（sensenova / ollama / gemini），None 用默认

    Returns:
        WAV bytes（TTS 结果），失败返回 None
    """
    if not wav_bytes:
        return None

    user_text = transcribe(wav_bytes)
    if not user_text:
        print("[pipeline] ASR returned empty")
        return None
    print(f"[user]  {user_text}")

    llm = LLMRouter(engine=llm_engine) if llm_engine else LLMRouter()
    reply = llm.chat(user_text)
    if not reply:
        print("[pipeline] LLM returned empty")
        return None
    print(f"[bot]   {reply}")

    reply_wav = synthesize(reply)
    if reply_wav is None:
        return None
    print(f"[pipeline] reply wav {len(reply_wav)} bytes, "
          f"{wav_duration_seconds(reply_wav):.1f}s")
    return reply_wav


def pipeline_text(user_text, llm_engine=None):
    """纯文本输入 → TTS WAV（跳过 ASR）"""
    if not user_text:
        return None
    llm = LLMRouter(engine=llm_engine) if llm_engine else LLMRouter()
    reply = llm.chat(user_text)
    if not reply:
        return None
    print(f"[bot]   {reply}")
    return synthesize(reply)
