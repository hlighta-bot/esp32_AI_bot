"""
ASR 语音识别模块
==================

使用 Whisper.cpp (whisper-cli) 进行本地语音转文字。

依赖:
  - whisper.cpp 已编译: /home/hqb/ai-apps/whisper.cpp/build/bin/whisper-cli
  - 模型文件: /home/hqb/ai-apps/whisper.cpp/models/ggml-tiny.bin

接口:
  WhisperASR.transcribe_wav(wav_bytes) - WAV bytes → 文字
  WhisperASR.transcribe_file(filepath) - WAV 文件 → 文字

使用示例:
  from asr import WhisperASR

  asr = WhisperASR()
  text = asr.transcribe_wav(wav_bytes)
"""

import os
import subprocess
import tempfile

from config import WHISPER_CLI, WHISPER_MODEL, WHISPER_LIB, GGML_LIB, ASR_TIMEOUT


class WhisperASR:
    """Whisper.cpp 语音识别引擎"""

    def __init__(self, whisper_cli=None, model_path=None, language="zh"):
        self.cli = whisper_cli or WHISPER_CLI
        self.model = model_path or WHISPER_MODEL
        self.language = language

    # --------------------------------------------------------
    # 公共接口
    # --------------------------------------------------------

    def transcribe_wav(self, wav_bytes):
        """WAV bytes → 识别文字

        将 bytes 写入临时文件，调用 whisper-cli 识别。

        Args:
            wav_bytes: WAV 格式的音频数据

        Returns:
            str: 识别的文字，失败返回空字符串
        """
        if not wav_bytes:
            return ""

        tmp_path = None
        try:
            # 写入临时 WAV 文件
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
                tmp.write(wav_bytes)
                tmp_path = tmp.name

            text = self._run_whisper(tmp_path)
            return text

        except Exception:
            return ""

        finally:
            if tmp_path and os.path.exists(tmp_path):
                os.unlink(tmp_path)

    def transcribe_file(self, filepath):
        """WAV 文件 → 识别文字

        Args:
            filepath: WAV 文件路径

        Returns:
            str: 识别的文字，失败返回空字符串
        """
        if not os.path.exists(filepath):
            return ""

        return self._run_whisper(filepath)

    # --------------------------------------------------------
    # 内部方法
    # --------------------------------------------------------

    def _run_whisper(self, audio_path):
        """调用 whisper-cli 执行识别

        Args:
            audio_path: 音频文件路径

        Returns:
            str: 识别结果
        """
        if not os.path.exists(self.cli):
            return ""

        if not os.path.exists(self.model):
            return ""

        # 设置动态库路径
        env = os.environ.copy()
        env["LD_LIBRARY_PATH"] = (
            f"{WHISPER_LIB}:{GGML_LIB}:{env.get('LD_LIBRARY_PATH', '')}"
        )

        cmd = [
            self.cli,
            "-m", self.model,
            "-f", audio_path,
            "-l", self.language,
            "-nt",
        ]

        try:
            result = subprocess.check_output(cmd, env=env, timeout=ASR_TIMEOUT)
            text = result.decode("utf-8").strip()
            return text

        except subprocess.TimeoutExpired:
            return ""

        except subprocess.CalledProcessError:
            return ""


# ============================================================
# __main__ - 独立测试
# ============================================================

if __name__ == "__main__":
    import sys

    if len(sys.argv) > 1:
        filepath = sys.argv[1]
        asr = WhisperASR()
        asr.transcribe_file(filepath)
    else:
        sys.exit("用法: python asr.py <wav文件>")
