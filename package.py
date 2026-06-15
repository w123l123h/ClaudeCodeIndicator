#!/usr/bin/env python3
"""
打包脚本：将客户分发文件打包为 ClaudeCodeIndicator.zip
解压后 start.bat 和 install.bat 以及子目录统一位于 ClaudeCodeIndicator 目录下。
"""
import zipfile
import os
from pathlib import Path

ROOT = Path(__file__).parent
OUTPUT = ROOT / "ClaudeCodeIndicator.zip"

# 顶层目录名（解压后所有内容都在该目录内）
TOP_DIR = "ClaudeCodeIndicator"

INCLUDE_FILES = [
    "start.pyw",
    "install.bat",
]

INCLUDE_DIRS = [
    "desktop-relay",
    "hook-notifier",
    "installer",
]


def package():
    with zipfile.ZipFile(OUTPUT, 'w', zipfile.ZIP_DEFLATED) as zf:
        # 添加根目录下的独立文件
        for f in INCLUDE_FILES:
            path = ROOT / f
            if path.exists():
                arcname = f"{TOP_DIR}/{f}"
                zf.write(path, arcname)
                print(f"  + {arcname}")

        # 添加需要递归包含的目录
        for d in INCLUDE_DIRS:
            dir_path = ROOT / d
            if not dir_path.exists():
                continue
            for file in dir_path.rglob("*"):
                if file.is_file() and "__pycache__" not in str(file):
                    relative = file.relative_to(ROOT)
                    arcname = f"{TOP_DIR}/{relative.as_posix()}"
                    zf.write(file, arcname)
                    print(f"  + {arcname}")

    print(f"\nPackaged: {OUTPUT} ({os.path.getsize(OUTPUT)} bytes)")


if __name__ == "__main__":
    package()