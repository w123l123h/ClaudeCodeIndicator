#!/usr/bin/env python3
"""
打包脚本：将客户分发文件打包为 ClaudeCodeIndicator.zip
解压后 start.bat 和 install.bat 在根目录，其余保持子目录结构。
"""
import zipfile
import os
from pathlib import Path

ROOT = Path(__file__).parent
OUTPUT = ROOT / "ClaudeCodeIndicator.zip"

INCLUDE_FILES = [
    "start.bat",
    "install.bat",
]

INCLUDE_DIRS = [
    "desktop-relay",
    "hook-notifier",
    "installer",
]


def package():
    with zipfile.ZipFile(OUTPUT, 'w', zipfile.ZIP_DEFLATED) as zf:
        for f in INCLUDE_FILES:
            path = ROOT / f
            if path.exists():
                zf.write(path, f)
                print(f"  + {f}")

        for d in INCLUDE_DIRS:
            dir_path = ROOT / d
            if not dir_path.exists():
                continue
            for file in dir_path.rglob("*"):
                if file.is_file() and "__pycache__" not in str(file):
                    arcname = str(file.relative_to(ROOT))
                    zf.write(file, arcname)
                    print(f"  + {arcname}")

    print(f"\nPackaged: {OUTPUT} ({os.path.getsize(OUTPUT)} bytes)")


if __name__ == "__main__":
    package()
