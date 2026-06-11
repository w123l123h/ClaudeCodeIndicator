# HTTP Hook Migration Design

**Date:** 2026-06-11
**Status:** Approved

## Context

当前 Claude Code hooks 通过 shell command（PowerShell/Python）发送原始 TCP 消息到 desktop-relay。PowerShell 命令存在 shell 转义问题（`$` 变量被 Git Bash 展开），且原始 TCP 协议不够标准化。

将通信方式从 "command hook + raw TCP" 改为 Claude Code 原生 "HTTP hook + HTTP server"，消除 shell 依赖，简化架构。

## Architecture

```
Before:
  Hook event → PowerShell/Python (shell escaping issues!)
    → raw TCP socket → TcpRelayServer → BLE → ESP32

After:
  Hook event → HTTP POST /hook (JSON, no shell involved)
    → HttpRelayServer (aiohttp) → BLE → ESP32
```

## Changes

### 1. New file: `desktop-relay/http_server.py`

HTTP server using aiohttp, listening on `127.0.0.1:54321`.

Single endpoint: `POST /hook`

Receives Claude Code hook JSON body:
```json
{
  "hook_event_name": "PreToolUse",
  "tool_name": "Bash",
  "tool_input": {...},
  "exit_code": 0
}
```

Event mapping (server-side):
| hook_event_name | condition | LED message |
|---|---|---|
| PreToolUse | — | WORKING |
| PermissionRequest | — | WAITING_USER |
| Stop | exit_code == 0 | COMPLETED |
| Stop | exit_code != 0 | ERROR |

### 2. Modify: `desktop-relay/main.py`

- Replace `from tcp_server import TcpRelayServer` → `from http_server import HttpRelayServer`
- Replace `self.tcp = TcpRelayServer(...)` → `self.http = HttpRelayServer(...)`
- Replace `await self.tcp.start()` → `await self.http.start()`

### 3. Modify: `desktop-relay/config.py`

- Rename `TCP_HOST` → `HTTP_HOST`, `TCP_PORT` → `HTTP_PORT`
- Port stays `54321`

### 4. Modify: `~/.claude/settings.json` hooks

All hooks use Claude Code native `"type": "http"`:
```json
{
  "hooks": {
    "PreToolUse": [
      {"matcher": "*", "hooks": [
        {"type": "http", "url": "http://127.0.0.1:54321/hook"}
      ]}
    ],
    "PermissionRequest": [
      {"matcher": "*", "hooks": [
        {"type": "http", "url": "http://127.0.0.1:54321/hook"}
      ]}
    ],
    "Stop": [
      {"matcher": "*", "hooks": [
        {"type": "http", "url": "http://127.0.0.1:54321/hook"}
      ]}
    ]
  }
}
```

### 5. Update: `installer/install.py`

`setup_hooks()` generates the HTTP hook config instead of command hooks.

### 6. Retire: `tcp_server.py`, `notify.py`

- `tcp_server.py` — no longer needed
- `notify.py` — hooks no longer call it; may keep as manual test tool

## Benefits

- **Zero shell escaping** — HTTP hooks send JSON directly, no shell involved
- **Less code** — no PowerShell one-liners, no notify.py for Stop logic
- **Server-side event discrimination** — Stop event's exit_code checked in Python, not shell
- **Debuggable** — `curl -X POST localhost:54321/hook -H 'Content-Type: application/json' -d '{"hook_event_name":"PreToolUse"}'`

## Verification

1. Start desktop-relay (aiohttp server on :54321)
2. `curl -X POST localhost:54321/hook -H 'Content-Type: application/json' -d '{"hook_event_name":"PreToolUse"}'` → ESP32 LED orange
3. `curl ... -d '{"hook_event_name":"PermissionRequest"}'` → LED orange blink
4. `curl ... -d '{"hook_event_name":"Stop","exit_code":0}'` → LED green
5. `curl ... -d '{"hook_event_name":"Stop","exit_code":1}'` → LED red
6. In Claude Code: trigger each event naturally, verify ESP32 responds
