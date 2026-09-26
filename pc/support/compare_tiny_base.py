#!/usr/bin/env python3
"""Compare Whisper tiny vs base on a small set of representative debug WAV files.

Inputs:
- Explicit sample list below

Outputs:
- asr_debug_compare_tiny_base.txt

Purpose:
- Separate model-quality issues from inherent audio-quality issues.
"""

from __future__ import annotations

from pathlib import Path

from asr import WhisperASR

BASE = Path(__file__).resolve().parent
MODEL_TINY = "/home/hqb/ai-apps/whisper.cpp/models/ggml-tiny.bin"
MODEL_BASE = "/home/hqb/ai-apps/whisper.cpp/models/ggml-base.bin"
OUT = BASE / "asr_debug_compare_tiny_base.txt"

SAMPLES = [
    "debug_1789433993.wav",
    "debug_1789437563.wav",
    "debug_1789439107.wav",
    "debug_1789454147.wav",
    "debug_1790159649.wav",
    "debug_1790163233.wav",
    "debug_1790167283.wav",
    "debug_1790167389.wav",
]


def main() -> int:
    tiny = WhisperASR(model_path=MODEL_TINY)
    base = WhisperASR(model_path=MODEL_BASE)

    lines = [
        "Whisper tiny vs base 对照",
        f"Tiny model: {MODEL_TINY}",
        f"Base model: {MODEL_BASE}",
        "",
    ]

    for name in SAMPLES:
        path = BASE / name
        if not path.exists():
            continue
        t1 = tiny.transcribe_file(str(path)).strip()
        b1 = base.transcribe_file(str(path)).strip()
        lines.append(f"### {name}")
        lines.append(f"- tiny: {t1}")
        lines.append(f"- base: {b1}")
        lines.append("")

    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"WROTE {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
