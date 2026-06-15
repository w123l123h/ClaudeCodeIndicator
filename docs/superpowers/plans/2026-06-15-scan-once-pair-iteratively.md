# Scan Once, Pair Iteratively — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重构蓝牙扫描/配对循环，扫描一次后逐个尝试所有设备，配对被拒时继续下一个而不重新扫描；已保存设备仅直连不扫描。

**Architecture:** 拆分 `ble_client` 的扫描和连接为独立方法，`main.py` 负责流程编排。`scan_devices()` 返回有序设备列表，`connect_and_verify()` 负责单设备连接验证，`run()` 实现三段式主循环。

**Tech Stack:** Python 3.12+, Bleak, asyncio

---

**Files modified:**
- `desktop-relay/config.py` — 新增 2 个常量
- `desktop-relay/ble_client.py` — 新增 `scan_devices()`，`_connect_and_verify` → `connect_and_verify`，删除 `scan_and_connect()`，重写 `reconnect_loop()`
- `desktop-relay/main.py` — 修改 `_pairing_flow(address)`，重写 `run()`

---

### Task 1: 添加配置常量

**Files:**
- Modify: `desktop-relay/config.py`

- [ ] **Step 1: 在 config.py 末尾添加两个新常量**

在文件末尾（`EVENT_LED_MAP` 之后）添加：

```python
# 已保存设备直连重试间隔（秒）
SAVED_RETRY_INTERVAL = 3
# 扫描全部失败后重试间隔（秒）
SCAN_RETRY_INTERVAL = 5
```

- [ ] **Step 2: 提交**

```bash
git add desktop-relay/config.py
git commit -m "feat: add SAVED_RETRY_INTERVAL and SCAN_RETRY_INTERVAL constants

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: ble_client — 新增 scan_devices()，公开 connect_and_verify()

**Files:**
- Modify: `desktop-relay/ble_client.py`

- [ ] **Step 1: 添加 scan_devices() 方法**

在 `save_device()` 方法之后（约第 44 行后），插入新方法：

```python
    async def scan_devices(self):
        """扫描 BLE 设备，返回按 RSSI 降序的列表 [(addr, name, rssi), ...]"""
        logger.info("Scanning for devices...")
        discovered = await BleakScanner.discover(timeout=BLE_SCAN_TIMEOUT, return_adv=True)

        dev_list = []
        for addr, (device, adv) in discovered.items():
            name = adv.local_name or "(no name)"
            rssi = adv.rssi
            dev_list.append((addr, name, rssi if rssi is not None else -999))
            logger.info(f"  Found: {addr}  RSSI={rssi if rssi else '?':>4}  name={name}")

        # 按 RSSI 从强到弱排序
        dev_list.sort(key=lambda x: x[2], reverse=True)
        return dev_list
```

- [ ] **Step 2: 将 `_connect_and_verify` 重命名为 `connect_and_verify`**

找到 `async def _connect_and_verify(self, address):`，将 `_connect_and_verify` 改为 `connect_and_verify`。

方法内部代码不变。

- [ ] **Step 3: 提交**

```bash
git add desktop-relay/ble_client.py
git commit -m "feat: add scan_devices(), rename _connect_and_verify to public

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: ble_client — 删除 scan_and_connect()，重写 reconnect_loop()

**Files:**
- Modify: `desktop-relay/ble_client.py`

- [ ] **Step 1: 删除 scan_and_connect() 方法**

删除 `async def scan_and_connect(self):` 整个方法（原第 46-61 行）。

- [ ] **Step 2: 重写 reconnect_loop()**

用以下代码替换现有的 `async def reconnect_loop(self):` 方法（原第 168-186 行）：

```python
    async def reconnect_loop(self):
        while True:
            try:
                if not self.is_connected:
                    saved = self.get_saved_device()
                    if not saved:
                        logger.warning("No saved device, skipping reconnect")
                        await asyncio.sleep(5)
                        continue

                    logger.info(f"Reconnecting to saved device: {saved}")
                    success = await self.connect_and_verify(saved)
                    if success:
                        logger.info("Reconnected successfully")
                    else:
                        logger.warning(f"Reconnect failed, retrying in {RECONNECT_BASE_DELAY}s...")
                        await asyncio.sleep(RECONNECT_BASE_DELAY)
                else:
                    await asyncio.sleep(1)
            except Exception as e:
                logger.error(f"Reconnect loop error: {e}")
                await asyncio.sleep(1)
```

- [ ] **Step 3: 检查 ble_client.py 不再有对 scan_and_connect 的引用**

```bash
grep -n "scan_and_connect" desktop-relay/ble_client.py
```

预期：无输出。

- [ ] **Step 4: 提交**

```bash
git add desktop-relay/ble_client.py
git commit -m "refactor: remove scan_and_connect, rewrite reconnect_loop for saved-device-only

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 4: main.py — 修改 _pairing_flow()，重写 run()

**Files:**
- Modify: `desktop-relay/main.py`

- [ ] **Step 1: 更新 config.py 导入**

修改 `main.py` 第 9-12 行，从 config 导入新增常量：

```python
from config import (
    KEEPALIVE_INTERVAL, KEEPALIVE_RESPONSE_TIMEOUT, KEEPALIVE_MAX_FAILURES,
    EVENT_LED_MAP, SAVED_RETRY_INTERVAL, SCAN_RETRY_INTERVAL,
)
```

- [ ] **Step 2: 修改 _pairing_flow() 接受 address 参数**

用以下代码替换 `_pairing_flow(self)` 方法（原第 70-95 行）：

```python
    async def _pairing_flow(self, address):
        """首次配对流程"""
        saved = self.ble.get_saved_device()
        if saved:
            logger.info(f"Already paired with: {saved}")
            return True

        # 连接后发送 PAIR_CONFIRM
        await self.ble.send("PAIR_CONFIRM")
        await asyncio.sleep(0.5)

        # 弹出对话框
        loop = asyncio.get_event_loop()
        confirmed = await loop.run_in_executor(
            None, show_pairing_dialog, address
        )

        if confirmed:
            self.ble.save_device(address)
            await self.ble.send("PAIR_SUCCESS")
            logger.info(f"Pairing successful, saved: {address}")
            return True
        else:
            await self.ble.disconnect()
            logger.info("Pairing cancelled by user")
            return False
```

- [ ] **Step 3: 重写 run() 方法**

用以下代码替换 `run(self)` 方法（原第 126-148 行）：

```python
    async def run(self):
        try:
            # 先启动 HTTP，确保 hook 消息不会丢失
            await self.http.start()

            saved = self.ble.get_saved_device()

            if saved:
                # 阶段1: 已保存设备，只直连不扫描
                logger.info(f"Saved device: {saved}, connecting...")
                while True:
                    if await self.ble.connect_and_verify(saved):
                        break
                    logger.warning(
                        f"Failed to connect saved device, "
                        f"retrying in {SAVED_RETRY_INTERVAL}s..."
                    )
                    await asyncio.sleep(SAVED_RETRY_INTERVAL)
            else:
                # 阶段2: 无已保存设备，扫描+逐个尝试配对
                paired = False
                while not paired:
                    devices = await self.ble.scan_devices()
                    if not devices:
                        logger.warning(
                            f"No devices found, "
                            f"retrying in {SCAN_RETRY_INTERVAL}s..."
                        )
                        await asyncio.sleep(SCAN_RETRY_INTERVAL)
                        continue

                    for address, name, rssi in devices:
                        logger.info(
                            f"Trying: {address} (RSSI={rssi}, name={name})"
                        )
                        if not await self.ble.connect_and_verify(address):
                            continue
                        if await self._pairing_flow(address):
                            paired = True
                            break

                    if not paired:
                        logger.warning(
                            f"All devices exhausted, "
                            f"rescanning in {SCAN_RETRY_INTERVAL}s..."
                        )
                        await asyncio.sleep(SCAN_RETRY_INTERVAL)

            # 阶段3: 保活 + 重连
            await asyncio.gather(
                self._keepalive_task(),
                self.ble.reconnect_loop(),
            )
        finally:
            await self.shutdown()
```

- [ ] **Step 4: 检查 main.py 不再有对 scan_and_connect 的引用**

```bash
grep -n "scan_and_connect" desktop-relay/main.py
```

预期：无输出。

- [ ] **Step 5: 提交**

```bash
git add desktop-relay/main.py
git commit -m "refactor: rewrite run() with three-phase scan-once-pair-iteratively logic

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 5: 最终验证

- [ ] **Step 1: 检查语法正确**

```bash
cd desktop-relay && python -c "import config; print('config OK')" && python -c "import ble_client; print('ble_client OK')" && python -c "import main; print('main OK')"
```

预期：三次 "OK"，无 ImportError 或 SyntaxError。

- [ ] **Step 2: 查看完整 diff 确认改动**

```bash
git diff HEAD~4..HEAD --stat
git diff HEAD~4..HEAD
```

确认改动范围仅限 `config.py`、`ble_client.py`、`main.py` 三个文件。
