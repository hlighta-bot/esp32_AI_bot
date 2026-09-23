#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_hi_hello_pcm.py
===================

用途:
    为 ESP32-S3 固件生成一次固定的"你好！"问候音频资源。

流程:
    1. 使用现有 pc/tts.py（Edge TTS）生成"你好！" WAV
    2. 复用 pc/wifi_server.py 中 _send_play_header() 的预处理逻辑：
       - WAV 解码
       - Stereo → Mono
       - 重采样到 16 kHz（scipy.signal.resample_poly）
       - float32 → int16 PCM
    3. 输出到 firmware/esp32/src/assets/hi_hello.h 的只读字节数组

输出:
    firmware/esp32/src/assets/hi_hello.h
        - HI_HELLO_PCM_SIZE:  原始 PCM 字节数（用于 playChunk() 分块）
        - HI_HELLO_PCM:       const uint8_t 数组

设计要点:
    - 与 pc/wifi_server.py 保持一致的采样率/通道/位深（16 kHz mono int16）
    - 使用 flash 存储（const 数组默认放到 .rodata → flash）
    - 只读取一次生成；后续固件编译时不需要联网
    - 与 scripts/gen_secrets.py 一样是构建辅助脚本，非运行时依赖

依赖:
    - Python 3.9+
    - 与 pc/ 相同的三方库（edge_tts / pydub / soundfile / scipy / numpy）

用法:
    python scripts/gen_hi_hello_pcm.py
    # 可选参数：
    #   --text "你好！"        覆盖默认文本
    #   --voice zh-CN-XiaoxiaoNeural
    #   --rate "-10%"
    #   --pitch "-1Hz"
    #   --out  <path>          覆盖默认输出路径
"""

from __future__ import annotations

import argparse
import io
import struct
import sys
import time
from datetime import datetime, timezone
from math import gcd
from pathlib import Path
from typing import Optional, Tuple

# --------------------------------------------------------------------------
# 路径
# --------------------------------------------------------------------------

PROJECT_DIR = Path(__file__).resolve().parent.parent
PC_DIR = PROJECT_DIR / "pc"
ASSETS_DIR = PROJECT_DIR / "firmware" / "esp32" / "src" / "assets"
DEFAULT_OUTPUT = ASSETS_DIR / "hi_hello.h"

# --------------------------------------------------------------------------
# 默认文本 / TTS 参数（与 pc/config.py 保持一致）
# --------------------------------------------------------------------------

DEFAULT_TEXT = "你好！"
DEFAULT_VOICE = "zh-CN-XiaoxiaoNeural"
DEFAULT_RATE = "-10%"
DEFAULT_PITCH = "-1Hz"

# 目标采样率（与 pc/config.py::RECORD_SAMPLE_RATE 保持一致）
TARGET_SAMPLE_RATE = 16000


# --------------------------------------------------------------------------
# 打印时间戳工具
# --------------------------------------------------------------------------

def _log(msg: str) -> None:
    ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    print(f"[{ts}] {msg}", flush=True)


# --------------------------------------------------------------------------
# 主流程
# --------------------------------------------------------------------------

def main(argv: Optional[list] = None) -> int:
    parser = argparse.ArgumentParser(description="生成 hi_hello.h 音频资源")
    parser.add_argument("--text", default=DEFAULT_TEXT, help="要合成的文字")
    parser.add_argument("--voice", default=DEFAULT_VOICE, help="Edge TTS 音色")
    parser.add_argument("--rate", default=DEFAULT_RATE, help="语速，如 '-10%%'")
    parser.add_argument("--pitch", default=DEFAULT_PITCH, help="音调，如 '-1Hz'")
    parser.add_argument("--out", default=str(DEFAULT_OUTPUT), help="输出头文件路径")
    args = parser.parse_args(argv)

    t0 = time.time()
    _log(f"start text={args.text!r} voice={args.voice}")

    # ------------------------------------------------------------
    # 1. 引入 pc/tts.py 生成 WAV
    # ------------------------------------------------------------
    if str(PC_DIR) not in sys.path:
        sys.path.insert(0, str(PC_DIR))

    try:
        from tts import TTSEngine  # type: ignore
    except Exception as e:
        _log(f"ERROR import tts.TTSEngine failed: {e}")
        return 1

    engine = TTSEngine(voice=args.voice, rate=args.rate, pitch=args.pitch)
    _log("calling edge_tts...")
    wav_bytes = engine.synthesize(args.text)
    if wav_bytes is None:
        _log("ERROR tts.synthesize() returned None")
        return 2
    _log(f"wav bytes = {len(wav_bytes)}")

    # ------------------------------------------------------------
    # 2. WAV -> mono -> 16 kHz -> int16 PCM
    #    与 pc/wifi_server.py::_send_play_header() 保持一致的预处理流程
    # ------------------------------------------------------------
    try:
        import soundfile as sf
        import numpy as np
        from scipy.signal import resample_poly
    except Exception as e:
        _log(f"ERROR missing audio deps: {e}")
        return 3

    audio, sr = sf.read(io.BytesIO(wav_bytes), always_2d=True)
    _log(f"wav sample_rate={sr} shape={audio.shape}")

    if audio.shape[1] > 1:
        audio = np.mean(audio, axis=1)
    else:
        audio = audio[:, 0]

    if sr != TARGET_SAMPLE_RATE:
        g = gcd(int(sr), TARGET_SAMPLE_RATE)
        audio = resample_poly(
            audio,
            TARGET_SAMPLE_RATE // g,
            int(sr) // g,
        )

    audio = np.clip(audio, -1.0, 1.0)
    pcm = (audio * 32767).astype(np.int16).tobytes()
    _log(f"pcm bytes = {len(pcm)} (mono int16 @ {TARGET_SAMPLE_RATE} Hz)")

    # ------------------------------------------------------------
    # 3. 写入 C 头文件
    # ------------------------------------------------------------
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    # 生成 uint8 字节数组（每行 16 字节，元素间用逗号分隔）
    lines: list[str] = []
    for i in range(0, len(pcm), 16):
        chunk = pcm[i : i + 16]
        hex_str = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append("    " + hex_str)
    pcm_lines = ",\n".join(lines)

    gen_ts = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%SZ")
    pcm_size = len(pcm)
    sample_rate_lit = str(TARGET_SAMPLE_RATE)

    header_content = f"""// ============================================================
// hi_hello.h - "你好！" PCM16 mono @ {TARGET_SAMPLE_RATE} Hz
//
// 生成工具: scripts/gen_hi_hello_pcm.py
// 生成时间: {gen_ts}
// 源文本:   {args.text}
// TTS:      voice={args.voice} rate={args.rate} pitch={args.pitch}
//
// 使用方式（见 firmware/esp32/src/main.cpp）:
//     playChunk((uint8_t *) HI_HELLO_PCM, HI_HELLO_PCM_SIZE);
//
// 注意:
//     - const 数组默认放 flash（.rodata），不占 PSRAM
//     - 长度偶对齐（16-bit 采样要求）
//     - 采样率必须与 firmware 里 SAMPLE_RATE 一致，否则音调错乱
// ============================================================

#ifndef HI_HELLO_H
#define HI_HELLO_H

#include <stdint.h>
#include <stddef.h>

// PCM 总字节数（原始，未做 mono→stereo 上采样）
#define HI_HELLO_PCM_SIZE  ({pcm_size}U)

// 采样率（16 kHz）
#define HI_HELLO_SAMPLE_RATE  ({sample_rate_lit}U)

// 采样格式
#define HI_HELLO_CHANNELS  (1)
#define HI_HELLO_SAMPLE_WIDTH  (2)

// 原始 PCM 字节数组
static const uint8_t HI_HELLO_PCM[HI_HELLO_PCM_SIZE] = {{
{pcm_lines}
}};

#endif  // HI_HELLO_H
"""

    out_path.write_text(header_content, encoding="utf-8")
    _log(f"wrote {out_path}  ({out_path.stat().st_size} bytes)")

    # ------------------------------------------------------------
    # 校验：读取 PCM 数组大小是否偶数（16-bit 采样要求）
    # ------------------------------------------------------------
    assert len(pcm) % 2 == 0, "PCM byte size must be even"

    elapsed = time.time() - t0
    _log(f"done in {elapsed:.2f}s  (pcm={len(pcm)} B, samples={len(pcm) // 2})")

    # 同时保存一份 WAV 便于人耳验证（可选）
    wav_path = out_path.with_suffix(".wav")
    wav_path.write_bytes(wav_bytes)
    _log(f"wav saved for reference: {wav_path}  ({wav_path.stat().st_size} bytes)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
