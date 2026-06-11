#!/usr/bin/env python3
"""测试用：验证 hook 是否被触发，写入时间戳到文件"""
import json
import sys
from datetime import datetime
from pathlib import Path

log_file = Path(__file__).parent / "hook_test.log"

# 读取 stdin
context = {}
try:
    raw = sys.stdin.read()
    if raw.strip():
        context = json.loads(raw)
except:
    pass

with open(log_file, "a") as f:
    f.write(f"[{datetime.now().isoformat()}] Hook fired!\n")
    f.write(f"  args: {sys.argv}\n")
    f.write(f"  context keys: {list(context.keys())}\n")
    f.write(f"  hook_event_name: {context.get('hook_event_name', 'N/A')}\n")
    f.write(f"  prompt: {context.get('prompt', 'N/A')[:100]}\n")
    f.write("---\n")
