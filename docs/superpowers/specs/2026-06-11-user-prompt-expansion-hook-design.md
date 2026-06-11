# UserPromptExpansion Hook — 设计规格

**日期**: 2026-06-11
**状态**: 已确认
**关联**: [[2026-06-10-claude-code-indicator-design]]

---

## 概述

在现有 Claude Code Indicator 的 HTTP hook 体系中增加 `UserPromptExpansion` 事件，当用户发送消息时立即触发 WORKING 状态（LED2 橙色常亮），比 PreToolUse 更早响应。

## 改动范围

### 1. `installer/install.py` — Hook 注册

在 `setup_hooks()` 的 hooks 字典中新增 `UserPromptExpansion` 项：

```python
settings["hooks"] = {
    "UserPromptExpansion": [                          # ← 新增
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
```

### 2. `desktop-relay/http_server.py` — 事件映射

**a) `_event_to_message()` 新增映射：**

```python
elif hook_event == "UserPromptExpansion":
    return "WORKING"
```

**b) `_handle_hook()` 修复 JSON 响应：**

当前返回 `text="OK"` 会导致 Claude Code 报错 `HTTP hook must return JSON, but got non-JSON response`。改为：

```python
return web.json_response({"status": "ok"})
```

### 3. `openspec/.../claude-code-integration/spec.md` — Spec 更新

在 "状态事件类型" requirement 中增加 UserPromptExpansion 场景。

## 事件流

```
用户输入 → UserPromptExpansion → HTTP POST /hook → WORKING → BLE → LED2 橙色常亮
                              →
          PreToolUse          → HTTP POST /hook → WORKING (幂等，LED 不变)
          PostToolUse         → HTTP POST /hook → WORKING (幂等，LED 不变)
          Stop (exit=0)       → HTTP POST /hook → COMPLETED → BLE → LED3 绿色
          Stop (exit≠0)       → HTTP POST /hook → ERROR → BLE → LED1 红色
```

UserPromptExpansion 是链路上最早的触发点，确保用户在按下回车后立即看到指示灯变化。

## 幂等性

WORKING 消息在 ESP32 端是幂等的——重复发送 WORKING 不会导致 LED 闪烁或状态异常，因此无需防抖逻辑。

## 验收

1. 运行 `install.bat` 后，`~/.claude/settings.json` 的 hooks 中包含 `UserPromptExpansion`
2. 用户在 Claude Code 中输入任意消息，指示灯显示橙色常亮（WORKING）
3. Claude Code 不再报 `HTTP hook must return JSON` 错误
