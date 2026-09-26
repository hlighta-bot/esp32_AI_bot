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
import math
import struct
import wave

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


def _read_wav_samples(wav_bytes, sample_rate=16000, channels=1):
    """读取 WAV 头与 PCM，返回 (frames, raw, nframes)。"""
    with io.BytesIO(wav_bytes) as bio:
        with wave.open(bio, "rb") as w:
            frames = w.getnframes()
            if frames <= 0 or w.getsampwidth() != 2:
                return None
            if channels > 0 and w.getnchannels() != channels:
                return None
            raw = w.readframes(frames)
    return raw, frames, int(w.getframerate() or sample_rate)


def _write_wav(samples, sample_rate=16000, channels=1):
    """把采样点重新打包为 WAV bytes。"""
    if not samples:
        return None
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(struct.pack("<" + "h" * len(samples), *samples))
    return buf.getvalue()


def _rms(samples):
    """计算整段样本的 RMS。"""
    if not samples:
        return 0.0
    return math.sqrt(sum(s * s for s in samples) / len(samples))


def _trim_wav_for_asr(wav_bytes):
    """ASR 前轻量预处理：去掉低能量静音并限制最大长度。

    经验规则来自 pc/support/asr_debug_wave_stats.csv 和人工标注：
    - 过低 RMS 的录音大多对应“背景全是杂音，没有说话”
    - 超长录音更容易诱发 Whisper 幻觉/重复
    """
    floor_rms = getattr(config, "ASR_PRETRIM_MIN_RMS", 260)
    max_duration_sec = getattr(config, "ASR_PRETRIM_MAX_DURATION_SEC", 6.0)

    parsed = _read_wav_samples(wav_bytes)
    if parsed is None:
        return None
    raw, nframes, sample_rate = parsed
    if raw:
        samples = list(struct.unpack("<" + "h" * (len(raw) // 2), raw))
    else:
        samples = []

    # 只保留有能量片段，同时避免把整段无效音频送进 ASR。
    seg_sec = 0.05
    seg_samples = max(1, int(sample_rate * seg_sec))
    seg_rms = []
    for i in range(0, len(samples), seg_samples):
        seg = samples[i:i + seg_samples]
        if seg:
            seg_rms.append((i, _rms(seg)))

    keep_start = None
    for idx, value in seg_rms:
        if value >= floor_rms:
            keep_start = idx
            break

    if keep_start is None:
        return None

    keep_end = len(samples)
    for idx, value in reversed(seg_rms):
        if value >= floor_rms:
            keep_end = min(len(samples), idx + seg_samples)
            break

    trimmed = samples[keep_start:keep_end]
    if max_duration_sec and len(trimmed) > int(sample_rate * max_duration_sec):
        trimmed = trimmed[: int(sample_rate * max_duration_sec)]
    return _write_wav(trimmed, sample_rate=sample_rate, channels=1)


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
        cleaned = _trim_wav_for_asr(wav_bytes)
        if cleaned is None:
            return None
        return _get_asr().transcribe_wav(cleaned)
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
