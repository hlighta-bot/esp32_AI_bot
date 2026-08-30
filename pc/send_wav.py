import struct
import time
import tkinter as tk
from tkinter import filedialog
from math import gcd

import numpy as np
import soundfile as sf
import serial
import serial.tools.list_ports

from scipy.signal import resample_poly


# ============================================================
# ESP32
# ============================================================

BAUD = 921600

TARGET_SAMPLE_RATE = 16000

CHUNK_SIZE = 4096


# ============================================================
# 自动寻找 ESP32
# ============================================================

def find_esp32_port():

    ports = list(
        serial.tools.list_ports.comports()
    )


    print()
    print("检测到的串口:")
    print("--------------------------------")


    for port in ports:

        print(
            port.device,
            "|",
            port.description,
            "|",
            port.vid,
            port.pid
        )


    print("--------------------------------")


    # 你的板卡定义：
    #
    # VID = 0x1A86
    # PID = 0x55D3
    #

    for port in ports:

        if (
            port.vid == 0x1A86
            and
            port.pid == 0x55D3
        ):
            return port.device


    # 如果没有识别到 VID/PID
    # 再尝试 ttyUSB0

    for port in ports:

        if port.device.startswith(
            "/dev/ttyUSB"
        ):
            return port.device


    return None


# ============================================================
# 选择 WAV
# ============================================================

root = tk.Tk()

root.withdraw()


filename = filedialog.askopenfilename(

    title="选择 WAV 文件",

    filetypes=[
        ("WAV files", "*.wav"),
        ("All files", "*.*")
    ]
)


if not filename:

    print(
        "没有选择 WAV 文件"
    )

    raise SystemExit


print()
print(
    "================================"
)

print(
    "ESP32 WAV PLAYER"
)

print(
    "================================"
)

print()

print(
    "文件:",
    filename
)


# ============================================================
# 读取 WAV
# ============================================================

print()
print(
    "读取 WAV..."
)


audio, sample_rate = sf.read(

    filename,

    always_2d=True
)


print(
    "原始采样率:",
    sample_rate,
    "Hz"
)


print(
    "原始声道:",
    audio.shape[1]
)


print(
    "原始数据类型:",
    audio.dtype
)


# ============================================================
# 转 float32
# ============================================================

audio = audio.astype(
    np.float32
)


# ============================================================
# Stereo -> Mono
# ============================================================

if audio.shape[1] > 1:

    print(
        "Stereo -> Mono"
    )


    audio = np.mean(

        audio,

        axis=1
    )

else:

    audio = audio[:, 0]


# ============================================================
# 采样率转换
# ============================================================

if sample_rate != TARGET_SAMPLE_RATE:

    print(
        f"{sample_rate} Hz -> "
        f"{TARGET_SAMPLE_RATE} Hz"
    )


    g = gcd(

        int(sample_rate),

        TARGET_SAMPLE_RATE
    )


    up = (
        TARGET_SAMPLE_RATE //
        g
    )


    down = (
        int(sample_rate) //
        g
    )


    audio = resample_poly(

        audio,

        up,

        down
    )


# ============================================================
# 限幅
# ============================================================

audio = np.clip(

    audio,

    -1.0,

    1.0
)


# ============================================================
# float32 -> int16
# ============================================================

pcm = (

    audio * 32767

).astype(

    np.int16
)


# ============================================================
# PCM bytes
# ============================================================

pcm_bytes = pcm.tobytes()


data_size = len(
    pcm_bytes
)


duration = (

    len(pcm) /
    TARGET_SAMPLE_RATE
)


# ============================================================
# 显示最终格式
# ============================================================

print()

print(
    "--------------------------------"
)

print(
    "最终格式"
)

print(
    "--------------------------------"
)

print(
    "Sample Rate:",
    TARGET_SAMPLE_RATE,
    "Hz"
)

print(
    "Bit Depth:",
    "16 bit"
)

print(
    "Channels:",
    "Mono"
)

print(
    "Encoding:",
    "PCM"
)

print(
    "PCM Size:",
    data_size,
    "bytes"
)

print(
    "Duration:",
    f"{duration:.2f}",
    "seconds"
)

print(
    "--------------------------------"
)


# ============================================================
# 寻找 ESP32
# ============================================================

port = find_esp32_port()


if port is None:

    print()
    print(
        "没有找到 ESP32!"
    )

    print(
        "请检查 USBIPD 和 USB 连接。"
    )

    raise SystemExit(1)


print()
print(
    "ESP32 串口:",
    port
)


# ============================================================
# 打开串口
# ============================================================

try:

    ser = serial.Serial(

        port,

        BAUD,

        timeout=2,

        write_timeout=5
    )

except Exception as e:

    print()
    print(
        "串口打开失败:"
    )

    print(e)

    raise SystemExit(1)


# ============================================================
# 等待 ESP32 启动
# ============================================================

time.sleep(2)


ser.reset_input_buffer()


# ============================================================
# 发送 PLAY
# ============================================================

print()

print(
    "发送 PLAY..."
)


ser.write(
    b"PLAY"
)


# ============================================================
# 发送数据长度
# ============================================================

ser.write(

    struct.pack(
        "<I",
        data_size
    )
)


# ============================================================
# 发送 PCM
# ============================================================

print()

print(
    "开始发送 PCM..."
)

print()


sent = 0

start_time = time.time()


while sent < data_size:

    end = min(

        sent + CHUNK_SIZE,

        data_size
    )


    chunk = pcm_bytes[
        sent:end
    ]


    # --------------------------------------------------------
    # 发送一个 chunk
    # --------------------------------------------------------

    ser.write(
        chunk
    )


    # --------------------------------------------------------
    # 等待 ESP32 ACK
    # --------------------------------------------------------

    ack = ser.read(3)


    if ack != b"ACK":

        print()

        print(
            "错误：没有收到 ESP32 ACK"
        )

        print(
            "收到:",
            repr(ack)
        )

        ser.close()

        raise SystemExit(1)


    sent = end


    # --------------------------------------------------------
    # 显示进度
    # --------------------------------------------------------

    percent = (

        sent /
        data_size *
        100
    )


    elapsed = (

        time.time() -
        start_time
    )


    if elapsed > 0:

        speed = (

            sent /
            elapsed /
            1024
        )

    else:

        speed = 0


    print(

        f"\r"
        f"{percent:6.2f}% "
        f"{speed:6.1f} KB/s",

        end="",

        flush=True
    )


print()

print()

print(
    "PCM 数据发送完成"
)


# ============================================================
# 等待 ESP32 播放结束
# ============================================================

print()

print(
    "等待 ESP32 播放完成..."
)


deadline = (

    time.time() +

    duration +

    10
)


while time.time() < deadline:

    if ser.in_waiting:

        line = (

            ser.readline()

            .decode(
                errors="ignore"
            )

            .strip()
        )


        if line:

            print(
                "ESP32:",
                line
            )


            if (
                "PLAYBACK_FINISHED"
                in line
            ):

                break


    else:

        time.sleep(0.05)


# ============================================================
# 关闭
# ============================================================

ser.close()


print()

print(
    "================================"
)

print(
    "播放完成"
)

print(
    "================================"
)
