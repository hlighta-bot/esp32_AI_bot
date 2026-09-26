#!/usr/bin/env python3
# ============================================================
# extract_mg90s_doc.py
#
# 目的：
#   从旧式 .doc (Composite Document) 中提取 MG90S 舵机参数文本。
#
# 输入：
#   /mnt/d/Users/HQB/Downloads/MG90S铁.doc
#
# 输出：
#   stdout 纯文本，供人工核对后写入 docs/hardware.md
#
# 依赖：
#   pip install olefile
# ============================================================

import re
import sys
from pathlib import Path

try:
    import olefile
except ImportError:
    print("ERROR: pip install olefile", file=sys.stderr)
    sys.exit(1)

DOC_PATH = Path("/mnt/d/Users/HQB/Downloads/MG90S铁.doc")


def extract_text(path: Path) -> str:
    ole = olefile.OleFileIO(str(path))
    data = ole.openstream("WordDocument").read()

    # .doc 正文通常以 UTF-16LE 存储
    txt = data.decode("utf-16le", errors="ignore")

    # 保留 CJK / ASCII 可见字符，其余替换为空格
    keep = []
    for ch in txt:
        if "\u4e00" <= ch <= "\u9fff":
            keep.append(ch)
        elif ch.isascii() and (ch.isalnum() or ch in " .,:;()-/°%"):
            keep.append(ch)
        else:
            keep.append(" ")

    s = "".join(keep)
    s = re.sub(r"[ \t]+", " ", s)
    s = re.sub(r"\n{2,}", "\n", s)
    return s


def main() -> int:
    if not DOC_PATH.exists():
        print(f"ERROR: not found: {DOC_PATH}", file=sys.stderr)
        return 1

    s = extract_text(DOC_PATH)

    # 只打印含 CJK 或数字的行
    for part in s.split("\n"):
        part = part.strip()
        if not part:
            continue
        if any("\u4e00" <= c <= "\u9fff" for c in part) or re.search(r"\d", part):
            print(part)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
