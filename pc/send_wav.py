"""
ESP32 音频播放器模块
======================

将 WAV 音频通过串口发送到 ESP32，通过 I2S 输出到扬声器。

依赖:
  - pyserial: 串口通信
  - numpy:    音频处理
  - soundfile: WAV 文件读取
  - scipy:    采样率重采样

接口:
  ESP32Player.connect(port)      - 连接 ESP32 串口
  ESP32Player.play_wav_bytes(wav) - WAV bytes → ESP32 播放
  ESP32Player.play_wav_file(path) - WAV 文件 → ESP32 播放
  ESP32Player.play_pcm_bytes(pcm) - PCM bytes → ESP32 播放
  ESP32Player.disconnect()        - 断开连接

使用示例:
  # 播放 WAV 文件
  from send_wav import ESP32Player
  player = ESP32Player()
  player.connect()
  player.play_wav_file("audio.wav")
  player.disconnect()

  # 播放 PCM 数据 (由其他模块生成)
  player.play_pcm_bytes(pcm_bytes)

CLI 使用 (独立运行):
  python send_wav.py              # 弹出文件选择对话框
  python send_wav.py audio.wav    # 指定 WAV 文件
"""

import io
import struct
import time
from math import gcd

import numpy as np
import serial
import serial.tools.list_ports
import soundfile as sf
from scipy.signal import resample_poly

from transport import TransportInterface
from config import (
    ESP32_BAUD,
    RECORD_SAMPLE_RATE,
    CHUNK_SIZE,
    ESP32_VID,
    ESP32_PID,
    PLAYBACK_TIMEOUT_BUFFER,
    PLAYBACK_POLL_INTERVAL,
    SERIAL_TIMEOUT,
    SERIAL_WRITE_TIMEOUT,
    PROTOCOL_PLAY,
    PROTOCOL_ACK,
    PROTOCOL_FINISHED,
)

# 别名 (向后兼容)
BAUD_RATE = ESP32_BAUD
TARGET_SAMPLE_RATE = RECORD_SAMPLE_RATE


# ============================================================
# ESP32Player 类
# ============================================================

class ESP32Player:
    """ESP32 WAV 播放器 - 通过传输层发送音频数据"""

    def __init__(self, transport: TransportInterface):
        self.transport = transport
    # --------------------------------------------------------
    # 公共接口
    # --------------------------------------------------------

    def connect(self):
        """连接传输层"""
        return self.transport.connect()

    def disconnect(self):
        """断开传输层连接"""
        self.transport.disconnect()
    def play_wav_bytes(self, wav_bytes):
        """播放 WAV bytes

        Args:
            wav_bytes: WAV 格式的音频数据

        Returns:
            bool: 播放完成返回 True
        """
        audio, sample_rate = sf.read(
            io.BytesIO(wav_bytes),
            always_2d=True,
        )

        pcm_bytes, duration = self._prepare_audio(audio, sample_rate)
        return self._send_play(pcm_bytes, duration)

    def play_wav_file(self, filepath):
        """播放 WAV 文件

        Args:
            filepath: WAV 文件路径

        Returns:
            bool: 播放完成返回 True
        """
        audio, sample_rate = sf.read(filepath, always_2d=True)

        pcm_bytes, duration = self._prepare_audio(audio, sample_rate)
        return self._send_play(pcm_bytes, duration)

    def play_pcm_bytes(self, pcm_bytes, sample_rate=TARGET_SAMPLE_RATE):
        """播放原始 PCM bytes (int16, 单声道)

        跳过 WAV 解析，直接发送裸 PCM 数据。

        Args:
            pcm_bytes: int16 单声道 PCM 数据
            sample_rate: 采样率 (默认 16000)

        Returns:
            bool: 播放完成返回 True
        """
        duration = len(pcm_bytes) // 2 / sample_rate
        return self._send_play(pcm_bytes, duration)

    # --------------------------------------------------------
    # 内部方法: 音频预处理
    # --------------------------------------------------------

    def _prepare_audio(self, audio, sample_rate):
        """预处理音频: float32 → mono → 重采样 → int16 PCM

        Args:
            audio: float32 numpy 数组 (shape: [samples, channels])
            sample_rate: 原始采样率

        Returns:
            tuple: (pcm_bytes, duration_seconds)
        """
        if audio.shape[1] > 1:
            audio = np.mean(audio, axis=1)
        else:
            audio = audio[:, 0]

        if sample_rate != TARGET_SAMPLE_RATE:
            g = gcd(int(sample_rate), TARGET_SAMPLE_RATE)
            up = TARGET_SAMPLE_RATE // g
            down = int(sample_rate) // g
            audio = resample_poly(audio, up, down)

        # 限幅
        audio = np.clip(audio, -1.0, 1.0)

        # float32 → int16
        pcm = (audio * 32767).astype(np.int16)
        pcm_bytes = pcm.tobytes()
        duration = len(pcm) / TARGET_SAMPLE_RATE

        return pcm_bytes, duration

    # --------------------------------------------------------
    # 内部方法: 串口发送
    # --------------------------------------------------------

    def _send_play(self, pcm_bytes, duration):
        """通过传输层发送 PCM 数据到 ESP32

        协议: PLAY + uint32_le(size) + [chunk + ACK]*
        """
        data_size = len(pcm_bytes)

        # 发送协议头
        self.transport.send(PROTOCOL_PLAY)
        self.transport.send(struct.pack("<I", data_size))

        # 分块发送数据
        if not self._send_chunks(pcm_bytes):
            return False

        # 等待播放完成 (超时: 音频时长 + 缓冲)
        self._wait_playback_finish(duration)

        return True

    def _send_chunks(self, pcm_bytes):
        """分块发送 PCM 数据，等待每块 ACK

        Args:
            pcm_bytes: 完整的 PCM 数据

        Returns:
            bool: 全部发送成功返回 True
        """
        data_size = len(pcm_bytes)
        sent = 0
        while sent < data_size:
            end = min(sent + CHUNK_SIZE, data_size)
            chunk = pcm_bytes[sent:end]

            if not self.transport.send(chunk):
                return False

            # 等待 ACK
            ack = self.transport.receive(len(PROTOCOL_ACK))
            if ack != PROTOCOL_ACK:
                return False

            sent = end
        return True

    def _wait_playback_finish(self, duration):
        """等待 ESP32 播放完成

        Args:
            duration: 音频时长 (秒)
        """
        deadline = time.time() + duration + PLAYBACK_TIMEOUT_BUFFER
        while time.time() < deadline:
            if self.transport.in_waiting():
                line = self.transport.readline().decode(errors="ignore").strip()
                if PROTOCOL_FINISHED in line:
                    break
            else:
                time.sleep(PLAYBACK_POLL_INTERVAL)

    # --------------------------------------------------------
    # 内部方法: 串口检测
    # --------------------------------------------------------

    def _find_port(self):
        """自动检测 ESP32 串口

        Returns:
            str 或 None: 串口设备路径
        """
        ports = list(serial.tools.list_ports.comports())

        for p in ports:
            if p.vid == ESP32_VID and p.pid == ESP32_PID:
                return p.device

        # 回退: Linux 串口设备
        for p in ports:
            if p.device.startswith("/dev/ttyUSB"):
                return p.device

        # 回退: macOS 串口设备
        for p in ports:
            if p.device.startswith("/dev/tty.") or p.device.startswith("/dev/cu."):
                return p.device

        return None


# ============================================================
# __main__ - 独立运行入口
#   python send_wav.py              → 文件选择对话框
#   python send_wav.py audio.wav    → 指定文件
# ============================================================

if __name__ == "__main__":
    import sys
    import tkinter as tk
    from tkinter import filedialog

    # Original serial implementation for __main__ only
    class SerialTransport(TransportInterface):
        def __init__(self, port, baud, timeout, write_timeout):
            self._port = port
            self._baud = baud
            self._timeout = timeout
            self._write_timeout = write_timeout
            self._ser = None

        def connect(self):
            if self._ser and self._ser.is_open:
                return True
            try:
                self._ser = serial.Serial(
                    self._port,
                    self._baud,
                    timeout=self._timeout,
                    write_timeout=self._write_timeout,
                )
                return True
            except Exception:
                return False

        def disconnect(self):
            if self._ser and self._ser.is_open:
                self._ser.close()
                self._ser = None

        def send(self, data: bytes) -> bool:
            if self._ser is None or not self._ser.is_open:
                return False
            try:
                self._ser.write(data)
                return True
            except serial.SerialTimeoutException:
                print(f"Error: Serial write timeout when sending {len(data)} bytes.")
                return False
            except Exception as e:
                print(f"Error sending data: {e}")
                return False

        def receive(self, num_bytes: int) -> bytes:
            if self._ser is None or not self._ser.is_open:
                return b''
            return self._ser.read(num_bytes)

        def in_waiting(self) -> int:
            if self._ser is None or not self._ser.is_open:
                return 0
            return self._ser.in_waiting

        def readline(self) -> bytes:
            if self._ser is None or not self._ser.is_open:
                return b''
            return self._ser.readline()


    # Port finding logic is still in ESP32Player for now, could be moved to SerialTransport factory
    player_instance = ESP32Player(None) # Pass None initially, only to access _find_port
    port = player_instance._find_port()
    if port is None:
        print("Error: No ESP32 serial port found.")
        sys.exit(1)

    # Now create the actual player with the transport
    transport = SerialTransport(port, ESP32_BAUD, SERIAL_TIMEOUT, SERIAL_WRITE_TIMEOUT)
    player = ESP32Player(transport)
    if not player.connect():
        sys.exit(1)

    try:
        # CLI 参数或文件对话框
        if len(sys.argv) > 1:
            filepath = sys.argv[1]
        else:
            root = tk.Tk()
            root.withdraw()
            filepath = filedialog.askopenfilename(
                title="选择 WAV 文件",
                filetypes=[("WAV files", "*.wav"), ("All files", "*.*")],
            )
            if not filepath:
                sys.exit(0)

        if not player.play_wav_file(filepath):
            sys.exit(1)
    finally:
        player.disconnect()

