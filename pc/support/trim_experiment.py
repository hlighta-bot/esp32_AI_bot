#!/usr/bin/env python3
"""Offline experiment: trim debug WAV audio and compare ASR accuracy.

Inputs:
- ./debug_*.wav in the same directory as this script

Outputs:
- ./support/asr_trim_experiment_report.txt

The goal is to find simple preprocessing rules that reduce invalid ASR input:
- remove leading/trailing low-energy silence
- avoid sending extremely long recordings to Whisper
"""

from __future__ import annotations

import csv
import glob
import math
import os
import struct
import subprocess
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from config import WHISPER_CLI, WHISPER_MODEL, WHISPER_LIB, GGML_LIB, ASR_TIMEOUT

BASE = Path(__file__).resolve().parent
OUT = BASE / "asr_trim_experiment_report.txt"
SAMPLE_RATE = 16000
SEG_SEC = 0.05
MAX_DURATION_SEC = 6.0

TRIM_VARIANTS = [
    ("raw", 0, 0.0),
    ("trim_abs220_max6", 220, MAX_DURATION_SEC),
    ("trim_abs260_max6", 260, MAX_DURATION_SEC),
    ("trim_abs300_max6", 300, MAX_DURATION_SEC),
]


def unpack_samples(raw: bytes):
    return list(struct.unpack("<" + "h" * (len(raw) // 2), raw))


def rms(samples: list[int]) -> float:
    if not samples:
        return 0.0
    return math.sqrt(sum(x * x for x in samples) / len(samples))


def segment_rms(samples: list[int], seg_sec: float = SEG_SEC):
    seg_samples = max(1, int(SAMPLE_RATE * seg_sec))
    out = []
    for i in range(0, len(samples), seg_samples):
        seg = samples[i:i + seg_samples]
        if seg:
            out.append(rms(seg))
    return out


def write_wav(path: Path, samples: list[int]) -> None:
    raw = struct.pack("<" + "h" * len(samples), *samples)
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(raw)


def trim_samples(samples: list[int], floor_rms: float, max_duration_sec: float):
    if floor_rms <= 0:
        return samples[: int(SAMPLE_RATE * max_duration_sec)] if max_duration_sec else samples
    seg_samples = max(1, int(SAMPLE_RATE * SEG_SEC))
    segs = []
    for i in range(0, len(samples), seg_samples):
        seg = samples[i:i + seg_samples]
        if seg:
            segs.append((i, rms(seg)))
    if not segs:
        return []
    keep_start = None
    for idx, r in segs:
        if r >= floor_rms:
            keep_start = idx
            break
    if keep_start is None:
        return []
    keep_end = len(samples)
    for idx, r in reversed(segs):
        if r >= floor_rms:
            keep_end = min(len(samples), idx + seg_samples)
            break
    trimmed = samples[keep_start:keep_end]
    if max_duration_sec and len(trimmed) > int(SAMPLE_RATE * max_duration_sec):
        trimmed = trimmed[: int(SAMPLE_RATE * max_duration_sec)]
    return trimmed


def transcribe(path: Path):
    cmd = [
        WHISPER_CLI,
        "-m", WHISPER_MODEL,
        "-f", str(path),
        "-l", "zh",
        "-nt",
    ]
    env = dict(**os.environ)
    env["LD_LIBRARY_PATH"] = f"{WHISPER_LIB}:{GGML_LIB}:{env.get('LD_LIBRARY_PATH','')}"
    try:
        out = subprocess.check_output(cmd, env=env, timeout=ASR_TIMEOUT)
        return out.decode("utf-8", errors="replace").strip()
    except Exception:
        return ""


def load_labels():
    labels = {}
    with (BASE / "batch_asr_results.csv").open("r", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        for row in reader:
            labels[row["文件名"]] = row["人工确认内容"].strip()
    return labels


def main() -> int:
    labels = load_labels()
    files = sorted(glob.glob(str(BASE / "debug_*.wav")))
    lines = ["ASR 预处理离线实验报告", ""]

    for variant_name, floor, maxdur in TRIM_VARIANTS:
        lines.append(f"### {variant_name}")
        lines.append(f"- floor_rms: {floor}")
        lines.append(f"- max_duration_sec: {maxdur}")
        kept = 0
        for fp in files:
            src = Path(fp)
            with wave.open(str(src), "rb") as wf:
                rate = wf.getframerate()
                raw = wf.readframes(wf.getnframes())
            samples = unpack_samples(raw)
            trimmed = trim_samples(samples, floor, maxdur)
            if not trimmed:
                continue
            kept += 1
            tmp = BASE / "support" / f"{variant_name}__{src.name}"
            write_wav(tmp, trimmed)
            text = transcribe(tmp)
            label = labels.get(src.name, "")
            lines.append(f"- {src.name} | label={label} | out={text}")
            if tmp.exists():
                tmp.unlink()
        lines.append(f"- kept={kept}/{len(files)}")
        lines.append("")

    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"WROTE {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
