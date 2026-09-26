#!/usr/bin/env python3
"""Batch transcribe pc/debug_*.wav with Whisper and write a text report.

Inputs:
- ./debug_*.wav in the same directory as this script

Outputs:
- asr_debug_transcripts.txt in the same directory

Dependencies:
- Existing pc/asr.py
"""

from __future__ import annotations

import glob
import time
import wave
from pathlib import Path

from asr import WhisperASR


def read_wav_info(path: Path) -> dict:
    info: dict = {}
    with wave.open(str(path), "rb") as wf:
        info["sample_rate"] = wf.getframerate()
        info["channels"] = wf.getnchannels()
        info["sample_width"] = wf.getsampwidth() * 8
        info["frames"] = wf.getnframes()
        info["duration_sec"] = round(
            wf.getnframes() / max(1, wf.getframerate()), 3
        )
    return info


def main() -> int:
    base = Path(__file__).resolve().parent
    out_path = base / "asr_debug_transcripts.txt"
    files = sorted(glob.glob(str(base / "debug_*.wav")))

    asr = WhisperASR()
    started = time.time()
    lines: list[str] = []

    lines.append("WAV Whisper 批量识别报告")
    lines.append(f"生成时间: {time.strftime('%Y-%m-%d %H:%M:%S')} UTC")
    lines.append(f"文件数量: {len(files)}")
    lines.append("")

    empty = 0
    nonempty = 0

    for file_str in files:
        p = Path(file_str)
        entry: dict = {"file": p.name}
        try:
            entry.update(read_wav_info(p))
        except Exception as exc:
            entry["wave_error"] = str(exc)

        t0 = time.time()
        text = asr.transcribe_file(str(p))
        entry["asr_ms"] = int((time.time() - t0) * 1000)
        entry["text"] = text.strip()

        if entry["text"]:
            nonempty += 1
        else:
            empty += 1

        lines.append(f"### {p.name}")
        for key in (
            "sample_rate",
            "channels",
            "sample_width",
            "frames",
            "duration_sec",
            "asr_ms",
        ):
            if key in entry:
                lines.append(f"- {key}: {entry[key]}")
        if "wave_error" in entry:
            lines.append(f"- wave_error: {entry['wave_error']}")
        lines.append(f"- text: {entry['text']}")
        lines.append("")

    lines.append("--- Summary ---")
    lines.append(f"识别为空: {empty}")
    lines.append(f"识别非空: {nonempty}")
    lines.append(f"总耗时: {round(time.time() - started, 3)}s")

    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"WROTE {out_path}")
    print(
        f"EMPTY={empty} NONEMPTY={nonempty} "
        f"TOTAL={len(files)} ELAPSED={round(time.time() - started, 3)}s"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
