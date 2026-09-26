#!/usr/bin/env python3
"""Classify ASR transcript quality for pc/asr_debug_transcripts.txt.

Outputs:
- asr_debug_report_summary.txt

Heuristic categories:
- suspicious_repeat: repeated phrase patterns that often indicate hallucination
- suspicious_parenthetical: parenthetical non-text labels like (不幸) / (笑)
- suspicious_long_emptyish: long audio but output is one or two characters
- suspicious_generic: very short or generic outputs like 好 / 你 / 谢谢 on longer audio
"""

from __future__ import annotations

import re
from collections import Counter
from pathlib import Path


BASE = Path(__file__).resolve().parent
SRC = BASE / "asr_debug_transcripts.txt"
OUT = BASE / "asr_debug_report_summary.txt"


def parse_blocks(text: str):
    blocks = []
    current = None
    for line in text.splitlines():
        if line.startswith("### "):
            if current:
                blocks.append(current)
            current = {"file": line[4:].strip(), "duration": None, "text": None}
        elif current:
            if line.startswith("- duration_sec: "):
                current["duration"] = line.split(":", 1)[1].strip()
            elif line.startswith("- text: "):
                current["text"] = line.split(":", 1)[1].strip()
    if current:
        blocks.append(current)
    return blocks


def classify(duration_sec: float, text: str) -> str:
    clean = re.sub(r"\s+", "", text)
    # Repeated phrase, e.g. 好,好,好,好 or same phrase multiple times
    if len(clean) >= 4 and re.search(r"(.)\1{2,}", clean):
        return "suspicious_repeat"
    if text.startswith("(") and text.endswith(")"):
        return "suspicious_parenthetical"
    if duration_sec >= 5.0 and len(clean) <= 2:
        return "suspicious_long_emptyish"
    if duration_sec >= 2.0 and clean in {"好", "你", "謝謝", "谢谢", "啊", "OK", "不"}:
        return "suspicious_generic"
    if duration_sec >= 6.0:
        return "suspicious_long_lowyield"
    return "likely_normal"


def main() -> int:
    text = SRC.read_text(encoding="utf-8")
    blocks = parse_blocks(text)
    counts = Counter()
    examples: dict[str, list[str]] = {}
    lines = [
        "ASR 结果分类摘要",
        f"来源: {SRC.name}",
        "",
    ]
    for b in blocks:
        duration = float(b["duration"] or 0.0)
        label = classify(duration, b["text"] or "")
        counts[label] += 1
        examples.setdefault(label, []).append(f"{b['file']} => {b['text']}")
    for label, count in sorted(counts.items()):
        lines.append(f"{label}: {count}")
    lines.append("")
    for label in ["suspicious_repeat", "suspicious_parenthetical", "suspicious_long_emptyish", "suspicious_generic", "suspicious_long_lowyield", "likely_normal"]:
        if examples.get(label):
            lines.append(f"### {label}")
            for item in examples[label][:12]:
                lines.append(f"- {item}")
            lines.append("")
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"WROTE {OUT}")
    print("COUNTS", dict(counts))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
