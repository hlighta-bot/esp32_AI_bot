import glob
import csv
import os
import re
from asr import WhisperASR


def natural_key(path):
    name = os.path.basename(path)
    m = re.search(r"debug_(\d+)", name)
    return int(m.group(1)) if m else 0


def main():
    files = sorted(
        glob.glob("debug_*.wav"),
        key=natural_key
    )

    if not files:
        print("没有找到 debug_*.wav")
        return

    print(f"找到 {len(files)} 个 WAV 文件")
    print("=" * 70)

    asr = WhisperASR()

    results = []

    for i, filepath in enumerate(files, 1):
        filename = os.path.basename(filepath)

        print(f"[{i:02d}/{len(files):02d}] {filename}")

        try:
            text = asr.transcribe_file(filepath)
        except Exception as e:
            text = ""
            print(f"    ERROR: {e}")

        text = text.strip()

        if text:
            print(f"    Whisper: {text}")
        else:
            print("    Whisper: <空>")

        results.append((i, filename, text))

    # CSV
    with open("batch_asr_results.csv", "w", newline="", encoding="utf-8-sig") as f:
        writer = csv.writer(f)
        writer.writerow(["编号", "文件名", "Whisper识别结果"])

        for row in results:
            writer.writerow(row)

    # TXT
    with open("batch_asr_results.txt", "w", encoding="utf-8") as f:
        for i, filename, text in results:
            f.write(f"{i:02d} | {filename} | {text}\n")

    print("=" * 70)
    print("完成")
    print("CSV: batch_asr_results.csv")
    print("TXT: batch_asr_results.txt")


if __name__ == "__main__":
    main()
