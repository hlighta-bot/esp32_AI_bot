"""
TTS 语音合成模块
==================

将文字转换为 WAV 音频数据。使用微软 Edge TTS 在线语音合成引擎。

依赖:
  - edge-tts: TTS 引擎 (pip install edge-tts)
  - pydub:    MP3 → WAV 转换 (pip install pydub，需要系统安装 ffmpeg)

接口:
  TTSEngine.synthesize(text)                - 文字 → WAV bytes
  TTSEngine.synthesize_to_file(text, path)  - 文字 → WAV 文件

使用示例:
  from tts import TTSEngine

  tts = TTSEngine()
  wav_bytes = tts.synthesize("你好世界")        # → bytes
  tts.synthesize_to_file("你好世界", "out.wav") # → 保存文件
"""

import asyncio
import io
import os
import tempfile
from pathlib import Path

import edge_tts

try:
    from pydub import AudioSegment
except ImportError:
    AudioSegment = None

from config import TTS_VOICE, TTS_RATE, TTS_PITCH


class TTSEngine:
    """TTS 语音合成引擎 - 文字转语音"""

    # 预设音色 (微软 Edge TTS 中文女声)，默认从 config 读取
    VOICE = TTS_VOICE
    RATE = TTS_RATE
    PITCH = TTS_PITCH

    def __init__(self, voice=None, rate=None, pitch=None):
        """初始化 TTS 引擎

        Args:
            voice: Edge TTS 音色名称，默认晓晓
            rate: 语速，如 "-10%" 慢 10%
            pitch: 音调，如 "-1Hz"
        """
        self._voice = voice or self.VOICE
        self._rate = rate or self.RATE
        self._pitch = pitch or self.PITCH

    # --------------------------------------------------------
    # 公共接口
    # --------------------------------------------------------

    def synthesize(self, text):
        """将文字合成为 WAV 音频

        流程: 文字 → Edge TTS → MP3 bytes → pydub → WAV bytes

        Args:
            text: 要合成的文字

        Returns:
            bytes 或 None: WAV 格式的音频数据，失败返回 None
        """
        # Step 1: 文字 → MP3
        mp3_bytes = self._text_to_mp3(text)
        if mp3_bytes is None:
            return None

        # Step 2: MP3 → WAV
        wav_bytes = self._mp3_to_wav(mp3_bytes)
        if wav_bytes is None:
            return None

        return wav_bytes

    def synthesize_to_file(self, text, output_path):
        """将文字合成为 WAV 并保存到文件

        Args:
            text: 要合成的文字
            output_path: 输出 WAV 文件路径

        Returns:
            bool: 成功返回 True
        """
        wav_bytes = self.synthesize(text)
        if wav_bytes is None:
            return False

        Path(output_path).write_bytes(wav_bytes)
        return True

    # --------------------------------------------------------
    # 内部方法
    # --------------------------------------------------------

    def _text_to_mp3(self, text):
        """文字 → MP3 字节 (使用 Edge TTS WebSocket 流式获取)

        Args:
            text: 要合成的文字

        Returns:
            bytes 或 None: MP3 音频数据
        """
        try:
            async def _run():
                communicate = edge_tts.Communicate(
                    text=text,
                    voice=self._voice,
                    rate=self._rate,
                    pitch=self._pitch,
                )
                mp3_buffer = io.BytesIO()
                async for chunk in communicate.stream():
                    if chunk["type"] == "audio":
                        mp3_buffer.write(chunk["data"])
                return mp3_buffer.getvalue()

            return asyncio.run(_run())
        except Exception:
            return None

    def _mp3_to_wav(self, mp3_bytes):
        """MP3 字节 → WAV 字节 (使用 pydub + ffmpeg)

        Args:
            mp3_bytes: MP3 音频数据

        Returns:
            bytes 或 None: WAV 音频数据
        """
        if AudioSegment is None:
            return None

        tmp_path = None
        try:
            # pydub 需要从文件读取，写入临时文件
            with tempfile.NamedTemporaryFile(suffix=".mp3", delete=False) as tmp:
                tmp.write(mp3_bytes)
                tmp_path = tmp.name

            audio = AudioSegment.from_mp3(tmp_path)
            wav_buffer = io.BytesIO()
            audio.export(wav_buffer, format="wav")
            return wav_buffer.getvalue()

        except Exception:
            return None

        finally:
            if tmp_path and os.path.exists(tmp_path):
                os.unlink(tmp_path)


# ============================================================
# __main__ 入口 - 独立测试 (python tts.py "你好")
# ============================================================

if __name__ == "__main__":
    import sys

    text = " ".join(sys.argv[1:]) if len(sys.argv) > 1 else "你好，世界"
    output = "tts_test.wav"

    tts = TTSEngine()
    tts.synthesize_to_file(text, output)
