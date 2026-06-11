#!/usr/bin/env python3
"""
Claude Code Hook 通知脚本
由 Claude Code settings.json hooks 调用，将状态事件通过 TCP 发送给桌面中继。

事件映射：
  PostToolUse → WORKING
  Notification → WAITING_USER
  Stop (exit_code=0) → COMPLETED
  Stop (exit_code≠0) → ERROR
"""
import socket
import sys
import json

TCP_HOST = "127.0.0.1"
TCP_PORT = 54321
TIMEOUT = 2  # 秒


def send_message(msg):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(TIMEOUT)
        sock.connect((TCP_HOST, TCP_PORT))
        sock.sendall((msg + "\n").encode())
        sock.close()
    except (socket.timeout, ConnectionRefusedError, OSError):
        # 主程序未运行，静默忽略
        pass


def main():
    if len(sys.argv) < 2:
        sys.exit(0)

    event_type = sys.argv[1]

    # 读取 hook 传入的上下文 (stdin JSON)
    context = {}
    try:
        raw = sys.stdin.read()
        if raw.strip():
            context = json.loads(raw)
    except (json.JSONDecodeError, Exception):
        pass

    if event_type == "tool_start":
        send_message("WORKING")

    elif event_type == "notification":
        # Claude Code 弹出通知（提问/权限等）= 等待用户
        send_message("WAITING_USER")

    elif event_type == "stop":
        exit_code = context.get("exit_code", 0)
        if exit_code == 0:
            send_message("COMPLETED")
        else:
            send_message("ERROR")


if __name__ == "__main__":
    main()
