"""
piopre.py - PlatformIO pre-build 钩子

在 PlatformIO 编译固件前自动执行 gen_secrets.py，
从项目根目录 config.local.json 生成：

    firmware/esp32/src/secrets.local.h
"""

import subprocess
import sys
from pathlib import Path


# PlatformIO 运行 extra_scripts 时的当前目录：
# firmware/esp32
#
# 因此：
#   ../../scripts/gen_secrets.py
#   ../../config.local.json
#
# 都可以从这里定位到项目根目录。
FIRMWARE_DIR = Path.cwd()
PROJECT_ROOT = FIRMWARE_DIR.parent.parent
GEN_SCRIPT = PROJECT_ROOT / "scripts" / "gen_secrets.py"


def main() -> int:
    print(f"[piopre] running: python {GEN_SCRIPT}")

    result = subprocess.run(
        [sys.executable, str(GEN_SCRIPT)],
        cwd=str(PROJECT_ROOT),
    )

    if result.returncode != 0:
        print(
            f"[piopre] ERROR: gen_secrets.py exited with {result.returncode}",
            file=sys.stderr,
        )
        return 1

    print("[piopre] done")
    return 0


main()