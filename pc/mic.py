"""
麦克风录音模块
================

使用 sounddevice 库从麦克风录制音频，保存为 WAV 文件。

依赖:
  - sounddevice: 音频输入输出 (pip install sounddevice)
  - numpy:       音频数据处理

接口:
  MicrophoneRecorder.record(duration, filepath) - 录制指定时长
  MicrophoneRecorder.record_until_silence(timeout) - 录制直到静音
  MicrophoneRecorder.list_devices() - 列出可用输入设备

使用示例:
  from mic import MicrophoneRecorder

  rec = MicrophoneRecorder()
  wav_bytes = rec.record(3.0)           # 录制 3 秒
  wav_bytes = rec.record_until_silence() # 录制直到静音
"""

import io
import time
import wave

import numpy as np
import sounddevice as sd

from config import (
    RECORD_SAMPLE_RATE,
    RECORD_CHANNELS,
    RECORD_DEVICE,
    SILENCE_MAX_DURATION,
    SILENCE_DURATION,
    SILENCE_THRESHOLD,
    SILENCE_POLL_INTERVAL,
)


class MicrophoneRecorder:
    """麦克风录音器 - 录制音频到 WAV bytes"""

    def __init__(self, sample_rate=None, channels=None, device=None):
        self.sample_rate = sample_rate or RECORD_SAMPLE_RATE
        self.channels = channels or RECORD_CHANNELS
        self.device = device or RECORD_DEVICE

    # --------------------------------------------------------
    # 公共接口
    # --------------------------------------------------------

    def record(self, duration):
        """录制指定时长的音频

        Args:
            duration: 录制时长 (秒)

        Returns:
            bytes 或 None: WAV 格式的音频数据，失败返回 None
        """
        try:
            # 用 queue 收集回调数据
            recorded = []

            def _callback(indata, frames, time_info, status):
                recorded.append(indata.copy())

            with sd.InputStream(
                samplerate=self.sample_rate,
                channels=self.channels,
                dtype="int16",
                device=self.device,
                callback=_callback,
            ):
                time.sleep(duration)

            if not recorded:
                return None

            # 合并所有回调数据
            audio = np.concatenate(recorded)
            return self._audio_to_wav_bytes(audio)

        except Exception:
            return None

    def record_until_silence(
        self,
        max_duration=SILENCE_MAX_DURATION,
        silence_duration=SILENCE_DURATION,
        threshold=SILENCE_THRESHOLD,
    ):
        """录制音频直到检测到静音

        使用能量检测判断是否停止录制。

        Args:
            max_duration: 最大录制时长 (秒)
            silence_duration: 静音持续时间触发停止 (秒)
            threshold: 静音阈值 (dBFS)

        Returns:
            bytes 或 None: WAV 格式的音频数据
        """
        try:
            recorded = []
            silence_start = None

            def _callback(indata, frames, time_info, status):
                recorded.append(indata.copy())

            with sd.InputStream(
                samplerate=self.sample_rate,
                channels=self.channels,
                dtype="int16",
                device=self.device,
                callback=_callback,
            ):
                start = time.time()

                while True:
                    elapsed = time.time() - start

                    # 超时
                    if elapsed >= max_duration:
                        break

                    # 分析最近的声音 (取最后 1 秒的音频块)
                    if recorded:
                        window = max(1, self.sample_rate // 1024)
                        recent = np.concatenate(recorded[-window:])
                        db = self._rms_to_db(recent)

                        if db <= threshold:
                            if silence_start is None:
                                silence_start = time.time()
                            elif time.time() - silence_start >= silence_duration:
                                break
                        else:
                            silence_start = None

                    time.sleep(SILENCE_POLL_INTERVAL)

            if not recorded:
                return None

            audio = np.concatenate(recorded)
            return self._audio_to_wav_bytes(audio)

        except Exception:
            return None

    @staticmethod
    def list_devices():
        """列出可用的音频输入设备

        Returns:
            list: 设备列表 [(index, name, input_channels, sample_rate), ...]
        """
        devices = sd.query_devices()
        result = []
        for i, dev in enumerate(devices):
            if dev["max_input_channels"] > 0:
                result.append((
                    i,
                    dev["name"],
                    dev["max_input_channels"],
                    dev["default_samplerate"],
                ))
        return result


    # --------------------------------------------------------
    # 内部方法
    # --------------------------------------------------------

    def _audio_to_wav_bytes(self, audio):
        """int16 numpy 数组 → WAV bytes

        Args:
            audio: int16 numpy 数组

        Returns:
            bytes: WAV 格式数据
        """
        buffer = io.BytesIO()
        with wave.open(buffer, "wb") as wav_file:
            wav_file.setnchannels(self.channels)
            wav_file.setsampwidth(2)  # 16-bit = 2 bytes
            wav_file.setframerate(self.sample_rate)
            wav_file.writeframes(audio.tobytes())
        return buffer.getvalue()

    @staticmethod
    def _rms_to_db(audio):
        """计算音频的 RMS 分贝值

        Args:
            audio: int16 numpy 数组

        Returns:
            float: dBFS 值
        """
        if audio.size == 0:
            return -100.0
        rms = np.sqrt(np.mean(np.float32(audio) ** 2))
        if rms == 0:
            return -100.0
        return 20.0 * np.log10(rms / 32768.0)


# ============================================================
# __main__ - 独立测试
# ============================================================

if __name__ == "__main__":
    rec = MicrophoneRecorder()

    rec.list_devices()

    wav = rec.record(3.0)
    if wav:
        out = "mic_test.wav"
        with open(out, "wb") as f:
            f.write(wav)
