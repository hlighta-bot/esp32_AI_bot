#!/usr/bin/env python3
"""Analyze WAV files for sample-rate, level, and rough pitch diagnostics."""

from __future__ import annotations

import argparse
import csv
import json
import math
import struct
import wave
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple


def read_wav(path: Path) -> Tuple[int, int, int, list[int]]:
    with wave.open(str(path), "rb") as wf:
        sr = wf.getframerate()
        ch = wf.getnchannels()
        width = wf.getsampwidth()
        frames = wf.getnframes()
        raw = wf.readframes(frames)
    if width != 2:
        raise ValueError(f"expected 16-bit PCM, got {width * 8}-bit PCM")
    samples = list(struct.unpack("<" + "h" * (len(raw) // 2), raw))
    if ch != 1:
        samples = samples[::ch]
    return sr, ch, frames, samples


def rms(vals: Sequence[int]) -> float:
    if not vals:
        return 0.0
    return math.sqrt(sum(v * v for v in vals) / len(vals))


def peak(vals: Sequence[int]) -> int:
    if not vals:
        return 0
    return max(abs(v) for v in vals)


def mean(vals: Sequence[int]) -> float:
    if not vals:
        return 0.0
    return sum(vals) / len(vals)


def segment_rms(samples: Sequence[int], sr: int, sec: float = 0.05):
    step = max(1, int(sr * sec))
    out = []
    for i in range(0, len(samples), step):
        seg = samples[i : i + step]
        out.append(rms(seg))
    return out


def crude_f0(samples: Sequence[int], sr: int) -> float | None:
    """Very rough autocorrelation-based pitch estimate for voiced segments only."""
    if len(samples) < 256:
        return None
    step = max(256, len(samples) // 16)
    f0s = []
    for start in range(0, len(samples) - 256, step):
        seg = samples[start : start + 256]
        if rms(seg) < 300:
            continue
        zero = [x * y for x, y in zip(seg, seg)]
        if not zero:
            continue
        energy = sum(x * x for x in seg)
        if energy <= 0:
            continue
        best_lag = 0
        best_val = -1.0
        for lag in range(60, min(len(seg) - 1, 400)):
            s = sum(seg[i] * seg[i + lag] for i in range(len(seg) - lag))
            norm = energy * math.sqrt(1 - lag / len(seg))
            if norm <= 0:
                continue
            val = s / norm
            if val > best_val:
                best_val = val
                best_lag = lag
        if best_lag > 0 and best_val > 0.25:
            f0s.append(sr / best_lag)
    if not f0s:
        return None
    return sum(f0s) / len(f0s)


def analyze(path: Path) -> dict:
    sr, ch, frames, samples = read_wav(path)
    seg = segment_rms(samples, sr, 0.05)
    return {
        "file": path.name,
        "seconds": frames / sr if sr else 0.0,
        "sample_rate": sr,
        "channels": ch,
        "frames": frames,
        "samples": len(samples),
        "mean": mean(samples),
        "rms": rms(samples),
        "peak": peak(samples),
        "min_seg_rms": min(seg) if seg else 0.0,
        "max_seg_rms": max(seg) if seg else 0.0,
        "f0_hz": crude_f0(samples, sr),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="*")
    parser.add_argument("--out-json", default="audio_pitch_report.json")
    args = parser.parse_args()

    if args.files:
        files = [Path(p) for p in args.files]
    else:
        base = Path(__file__).resolve().parents[1]
        files = sorted(base.glob("debug_*.wav"))

    rows = [analyze(p) for p in files]
    out = Path(args.out_json)
    with out.open("w", encoding="utf-8") as f:
        json.dump(rows, f, ensure_ascii=False, indent=2)
    print(json.dumps(rows, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
