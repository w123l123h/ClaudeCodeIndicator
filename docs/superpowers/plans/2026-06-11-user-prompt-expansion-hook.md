# UserPromptExpansion Hook Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 Claude Code Indicator 的 HTTP hook 体系中增加 UserPromptExpansion 事件 → WORKING 状态映射，同时修复 HTTP hook JSON 响应 bug。

**Architecture:** 在现有 `install.py` hooks 字典中增加 `UserPromptExpansion` 项，在 `http_server.py` 的事件映射方法中增加对应分支，并将响应从 `text/plain` 改为 `application/json`。纯增量改动，不影响现有 hook 事件。

**Tech Stack:** Python 3.13, aiohttp

---

### Task 1: 修复 http_server.py JSON 响应 bug

**Files:**
- Modify: `desktop-relay/http_server.py:42`

- [ ] **Step 1: 修改 `_handle_hook()` 返回值为 JSON 响应**

将第 42 行：
```python
return web.Response(status=200, text="OK")
```
改为：
```python
return web.json_response({"status": "ok"})
```

- [ ] **Step 2: 验证语法**

```bash
cd D:/develop/projects/esp32Projects/ClaudeCodeIndicator && C:/Users/joe06/AppData/Local/Programs/Python/Python313/python.exe -m py_compile desktop-relay/http_server.py
```
Expected: 无输出（编译成功）

- [ ] **Step 3: Commit**

```bash
git add desktop-relay/http_server.py
git commit -m "fix: HTTP hook return JSON response instead of plain text

Claude Code requires HTTP hooks to return JSON. The previous
`text=\"OK\"` response caused 'HTTP hook must return JSON' errors.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: 添加 UserPromptExpansion 事件映射

**Files:**
- Modify: `desktop-relay/http_server.py:45-60`

- [ ] **Step 1: 在 `_event_to_message()` 中添加 UserPromptExpansion 分支**

在 `_event_to_message()` 方法中，`elif hook_event in ("PostToolUse", "PostToolBatch"):` 之前插入：

```python
        elif hook_event == "UserPromptExpansion":
            return "WORKING"
```

完整的方法变为：
```python
    def _event_to_message(self, hook_event, body):
        if hook_event == "PreToolUse":
            # AskUserQuestion = Claude 在等用户回答 → 闪烁
            if body.get("tool_name") == "AskUserQuestion":
                return "WAITING_USER"
            return "WORKING"
        elif hook_event == "UserPromptExpansion":
            return "WORKING"
        elif hook_event in ("PermissionRequest",):
            # 权限弹窗 = 需要用户介入 → 闪烁
            return "WAITING_USER"
        elif hook_event in ("PostToolUse", "PostToolBatch"):
            # 工具执行完 → 切回工作中
            return "WORKING"
        elif hook_event == "Stop":
            exit_code = body.get("exit_code", 0)
            return "COMPLETED" if exit_code == 0 else "ERROR"
        return None
```

- [ ] **Step 2: 验证语法**

```bash
cd D:/develop/projects/esp32Projects/ClaudeCodeIndicator && C:/Users/joe06/AppData/Local/Programs/Python/Python313/python.exe -m py_compile desktop-relay/http_server.py
```
Expected: 无输出（编译成功）

- [ ] **Step 3: Commit**

```bash
git add desktop-relay/http_server.py
git commit -m "feat: add UserPromptExpansion hook → WORKING mapping

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: 更新 install.py 注册 UserPromptExpansion hook

**Files:**
- Modify: `installer/install.py:47-60`

- [ ] **Step 1: 在 hooks 字典中添加 UserPromptExpansion 项**

将 `setup_hooks()` 中的 `settings["hooks"]` 赋值改为：

```python
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
```

- [ ] **Step 2: 同步更新 verify() 中的事件列表**

将 `verify()` 函数第 88 行的事件列表从：
```python
for event in ["PreToolUse", "PermissionRequest", "PostToolUse", "Stop"]:
```
改为：
```python
for event in ["UserPromptExpansion", "PreToolUse", "PermissionRequest", "PostToolUse", "Stop"]:
```

- [ ] **Step 3: 验证语法**

```bash
cd D:/develop/projects/esp32Projects/ClaudeCodeIndicator && C:/Users/joe06/AppData/Local/Programs/Python/Python313/python.exe -m py_compile installer/install.py
```
Expected: 无输出（编译成功）

- [ ] **Step 4: Commit**

```bash
git add installer/install.py
git commit -m "feat: register UserPromptExpansion hook in installer

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 4: 更新 OpenSpec spec 文档

**Files:**
- Modify: `openspec/changes/claude-code-indicator/specs/claude-code-integration/spec.md`

- [ ] **Step 1: 在 "状态事件类型" requirement 中增加 UserPromptExpansion 场景**

在第 10-15 行的 requirement 描述中，在 "工具调用开始" 之前添加：

```markdown
- 用户消息展开（UserPromptExpansion 触发）→ `WORKING`
```

完整变为：
```markdown
### Requirement: 状态事件类型
通知脚本 SHALL 支持以下 Hook 事件映射：
- 用户消息展开（UserPromptExpansion 触发）→ `WORKING`
- 工具调用开始（PreToolUse 触发）→ `WORKING`
- 等待用户确认（PermissionRequest 触发）→ `WAITING_USER`
- 任务停止（Stop 触发）→ `COMPLETED` 或 `ERROR`
```

- [ ] **Step 2: 增加 UserPromptExpansion 的 Scenario**

在 "Scenario: 工作中通知" 之后添加：

```markdown
#### Scenario: 用户消息展开通知
- **WHEN** 用户在 Claude Code 中输入消息（UserPromptExpansion 触发）
- **THEN** 通过 HTTP POST 向 `http://127.0.0.1:54321/hook` 发送事件
- **THEN** 电脑端主程序通过 BLE 发送 `WORKING` 消息
- **THEN** 指示灯 LED2 显示橙色常亮
```

- [ ] **Step 3: Commit**

```bash
git add openspec/changes/claude-code-indicator/specs/claude-code-integration/spec.md
git commit -m "docs: add UserPromptExpansion hook to claude-code-integration spec

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 5: 端到端验证

- [ ] **Step 1: 重新运行安装程序**

```bash
cd D:/develop/projects/esp32Projects/ClaudeCodeIndicator && C:/Users/joe06/AppData/Local/Programs/Python/Python313/python.exe installer/install.py
```
Expected: 输出 `Installation successful!`

- [ ] **Step 2: 验证 settings.json 包含 UserPromptExpansion**

```bash
C:/Users/joe06/AppData/Local/Programs/Python/Python313/python.exe -c "import json; data=json.load(open(r'C:\Users\joe06\.claude\settings.json')); print('UserPromptExpansion' in data.get('hooks', {}))"
```
Expected: `True`

- [ ] **Step 3: 验证 hooks 列表完整性**

```bash
C:/Users/joe06/AppData/Local/Programs/Python/Python313/python.exe -c "import json; data=json.load(open(r'C:\Users\joe06\.claude\settings.json')); [print(k) for k in data.get('hooks', {}).keys()]"
```
Expected:
```
UserPromptExpansion
PreToolUse
PermissionRequest
PostToolUse
Stop
```
