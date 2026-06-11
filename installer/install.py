#!/usr/bin/env python3
"""
Claude Code Indicator 安装程序
1. 安装 Python 依赖 (bleak, aiohttp)
2. 配置 Claude Code hooks (HTTP 类型)
"""
import subprocess
import sys
import json
import os
from pathlib import Path

PYTHON_EXE = r"C:\Users\joe06\AppData\Local\Programs\Python\Python313\python.exe"
HTTP_PORT = 54321


def install_deps():
    """安装所需 Python 依赖"""
    deps = ["bleak", "aiohttp"]
    for dep in deps:
        print(f"Checking {dep}...")
        try:
            __import__(dep)
            print(f"  {dep}: installed")
        except ImportError:
            print(f"  {dep}: installing...")
            subprocess.check_call(
                [PYTHON_EXE, "-m", "pip", "install", dep],
                stdout=sys.stdout, stderr=sys.stderr
            )
            print(f"  {dep}: installed successfully")


def setup_hooks():
    """配置 Claude Code hooks — HTTP 类型，4 种通知状态"""
    settings_path = Path.home() / ".claude" / "settings.json"

    # 读取现有配置
    settings = {}
    if settings_path.exists():
        with open(settings_path, 'r') as f:
            settings = json.load(f)

    hook_url = f"http://127.0.0.1:{HTTP_PORT}/hook"
    http_hook = {"type": "http", "url": hook_url}

    settings["hooks"] = {
        "UserPromptExpansion": [
            {"matcher": "*", "hooks": [http_hook]}
        ],
        "PreToolUse": [
            {"matcher": "*", "hooks": [http_hook]}
        ],
        "PermissionRequest": [
            {"matcher": "*", "hooks": [http_hook]}
        ],
        "PostToolUse": [
            {"matcher": "*", "hooks": [http_hook]}
        ],
        "Stop": [
            {"matcher": "*", "hooks": [http_hook]}
        ]
    }

    # 写入
    os.makedirs(settings_path.parent, exist_ok=True)
    with open(settings_path, 'w') as f:
        json.dump(settings, f, indent=2, ensure_ascii=False)

    print(f"Hooks configured in: {settings_path}")


def verify():
    """验证安装"""
    errors = []

    # 检查依赖
    for dep in ["bleak", "aiohttp"]:
        try:
            __import__(dep)
            print(f"  [OK] {dep} available")
        except ImportError:
            errors.append(f"{dep} not installed")

    # 检查 settings.json
    settings_path = Path.home() / ".claude" / "settings.json"
    if settings_path.exists():
        with open(settings_path) as f:
            data = json.load(f)
            hooks = data.get("hooks", {})
            for event in ["UserPromptExpansion", "PreToolUse", "PermissionRequest", "PostToolUse", "Stop"]:
                if event in hooks:
                    # 检查是否为 HTTP hook
                    entry = hooks[event][0]
                    hook = entry.get("hooks", [{}])[0] if entry.get("hooks") else {}
                    if hook.get("type") == "http":
                        print(f"  [OK] {event} hook (HTTP) configured")
                    else:
                        print(f"  [OK] {event} hook configured")
                else:
                    errors.append(f"{event} hook not found in settings.json")
    else:
        errors.append("settings.json not found")

    if errors:
        print(f"\nWARNING: {len(errors)} issue(s):")
        for e in errors:
            print(f"  - {e}")
    else:
        print("\nInstallation successful!")


if __name__ == "__main__":
    print("=== Claude Code Indicator Installer ===\n")
    install_deps()
    setup_hooks()
    verify()
