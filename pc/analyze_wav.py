import wave
import struct
import math
import statistics
import sys
import os

wav_path = sys.argv[1] if len(sys.argv) > 1 else "debug_1789283663.wav"

print("=" * 60)
print("WAV 音频诊断")
print("=" * 60)
print(f"文件: {wav_path}")

if not os.path.exists(wav_path):
    print("ERROR: 文件不存在")
    sys.exit(1)

with wave.open(wav_path, "rb") as wf:
    channels = wf.getnchannels()
    sample_width = wf.getsampwidth()
    sample_rate = wf.getframerate()
    frames = wf.getnframes()
    duration = frames / sample_rate

    print(f"声道数:       {channels}")
    print(f"采样率:       {sample_rate} Hz")
    print(f"采样位宽:     {sample_width * 8} bit")
    print(f"采样数量:     {frames}")
    print(f"时长:         {duration:.3f} sec")

    raw = wf.readframes(frames)

if sample_width != 2:
    print(f"\n暂不支持 {sample_width * 8} bit，当前脚本需要 PCM16")
    sys.exit(1)

total_samples = len(raw) // 2
samples = struct.unpack("<" + "h" * total_samples, raw)

if channels > 1:
    samples = samples[::channels]

samples = list(samples)

print()
print("-" * 60)
print("整体信号")
print("-" * 60)

peak = max(abs(x) for x in samples)

sum_sq = sum(x * x for x in samples)
rms = math.sqrt(sum_sq / len(samples))

mean = sum(samples) / len(samples)

print(f"DC 平均值:    {mean:.2f}")
print(f"RMS:          {rms:.2f}")
print(f"Peak:         {peak}")
print(f"Peak %:       {peak / 32767 * 100:.2f}%")

if peak > 32000:
    print("⚠️ 严重削波/接近削波")
elif peak > 30000:
    print("⚠️ Peak 很高，可能存在削波")
elif peak > 20000:
    print("⚠️ 信号幅度比较高")
else:
    print("✓ Peak 没有明显接近 16-bit 上限")

# dBFS
if rms > 0:
    rms_db = 20 * math.log10(rms / 32768)
    print(f"RMS dBFS:     {rms_db:.2f} dB")

# 分段分析
print()
print("-" * 60)
print("分段 RMS 分析")
print("-" * 60)

segment_sec = 0.25
segment_samples = max(1, int(sample_rate * segment_sec))

segments = []

for i in range(0, len(samples), segment_samples):
    seg = samples[i:i + segment_samples]
    if not seg:
        continue

    seg_rms = math.sqrt(sum(x * x for x in seg) / len(seg))
    seg_peak = max(abs(x) for x in seg)

    if seg_rms > 0:
        db = 20 * math.log10(seg_rms / 32768)
    else:
        db = -120

    segments.append((i / sample_rate, seg_rms, seg_peak, db))

print(f"{'时间':>8} {'RMS':>10} {'Peak':>10} {'dBFS':>10}")

for t, r, p, db in segments:
    print(f"{t:8.2f} {r:10.1f} {p:10d} {db:10.2f}")

# 自动找低能量/高能量段
rms_values = [x[1] for x in segments]

if rms_values:
    sorted_rms = sorted(rms_values)

    low_count = max(1, len(sorted_rms) // 5)
    high_count = max(1, len(sorted_rms) // 5)

    low_rms = statistics.mean(sorted_rms[:low_count])
    high_rms = statistics.mean(sorted_rms[-high_count:])

    print()
    print("-" * 60)
    print("自动估计")
    print("-" * 60)

    print(f"低能量段平均 RMS: {low_rms:.2f}")
    print(f"高能量段平均 RMS: {high_rms:.2f}")

    if low_rms > 0:
        ratio = high_rms / low_rms
        print(f"高/低 RMS 比:     {ratio:.2f} 倍")

    print()
    if low_rms < 150:
        print("✓ 静音噪声看起来较低")
    elif low_rms < 300:
        print("⚠️ 静音噪声偏高")
    elif low_rms < 500:
        print("⚠️ 静音噪声很高，VAD=400 可能容易误触发")
    else:
        print("❌ 静音噪声极高，当前 VAD=400 很可能无法正常工作")

    if high_rms > low_rms * 3:
        print("✓ 说话与静音有明显能量差")
    else:
        print("❌ 说话与静音能量差很小，Whisper 识别困难")

print()
print("=" * 60)
print("重点：请把上面的完整输出发给我")
print("=" * 60)
