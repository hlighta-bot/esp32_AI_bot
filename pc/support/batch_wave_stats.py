#!/usr/bin/env python3
"""Batch analyze pc/debug_*.wav waveform stats and write a CSV report.

Inputs:
- ./debug_*.wav in the same directory as this script

Outputs:
- asr_debug_wave_stats.csv in the same directory

Goal:
- Help distinguish ASR failure caused by signal quality issues
  (e.g. clipping, very high noise floor, extremely long clips)
  from model-only misrecognition.
"""

from __future__ import annotations

import csv
import glob
import math
import statistics
import struct
import wave
from pathlib import Path


def rms(samples: list[int]) -> float:
    if not samples:
        return 0.0
    return math.sqrt(sum(x * x for x in samples) / len(samples))


def segment_stats(samples: list[int], sample_rate: int) -> tuple[float, float]:
    seg_samples = max(1, int(sample_rate * 0.05))
    seg_rms: list[float] = []
    for i in range(0, len(samples), seg_samples):
        seg = samples[i:i + seg_samples]
        if seg:
            seg_rms.append(rms(seg))
    if not seg_rms:
        return 0.0, 0.0
    sorted_rms = sorted(seg_rms)
    low_count = max(1, len(sorted_rms) // 5)
    high_count = max(1, len(sorted_rms) // 5)
    low_avg = statistics.mean(sorted_rms[:low_count])
    high_avg = statistics.mean(sorted_rms[-high_count:])
    return low_avg, high_avg


def main() -> int:
    base = Path(__file__).resolve().parent
    out_path = base / "asr_debug_wave_stats.csv"
    files = sorted(glob.glob(str(base / "debug_*.wav")))

    rows: list[dict[str, object]] = []
    for file_str in files:
        path = Path(file_str)
        with wave.open(str(path), "rb") as wf:
            sample_rate = wf.getframerate()
            channels = wf.getnchannels()
            sample_width = wf.getsampwidth()
            frames = wf.getnframes()
            raw = wf.readframes(frames)

        if sample_width != 2:
            continue

        samples = list(struct.unpack("<" + "h" * (len(raw) // 2), raw))
        if channels > 1:
            samples = samples[::channels]

        duration_sec = round(frames / max(1, sample_rate), 3)
        overall_rms = rms(samples)
        peak = max(abs(x) for x in samples) if samples else 0
        low5_rms, high5_rms = segment_stats(samples, sample_rate)

        rows.append(
            {
                "file": path.name,
                "duration_sec": duration_sec,
                "overall_rms": round(overall_rms, 2),
                "peak": peak,
                "peak_pct": round(peak / 32767 * 100, 2) if peak else 0.0,
                "low5_rms": round(low5_rms, 2),
                "high5_rms": round(high5_rms, 2),
                "high_low_ratio": round(high5_rms / low5_rms, 3) if low5_rms > 0 else 0.0,
            }
        )

    with out_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "file",
                "duration_sec",
                "overall_rms",
                "peak",
                "peak_pct",
                "low5_rms",
                "high5_rms",
                "high_low_ratio",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)

    files_with_peak_gt_16000 = sum(1 for r in rows if int(r["peak"]) > 16000)
    files_with_low5_lt_150 = sum(1 for r in rows if float(r["low5_rms"]) < 150)
    files_with_duration_gt_10s = sum(1 for r in rows if float(r["duration_sec"]) > 10)

    print(f"WROTE {out_path}")
    print(
        f"FILES={len(rows)} "
        f"PEAK_GT_16000={files_with_peak_gt_16000} "
        f"LOW5_LT_150={files_with_low5_lt_150} "
        f"DURATION_GT_10S={files_with_duration_gt_10s}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
