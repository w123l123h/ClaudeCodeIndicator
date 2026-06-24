# BLE 扫描优先队列配对 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 扫描期间 ClaudeCodeIndicator 设备立即连接，其他设备排队等待扫描结束/10 秒超时后按 RSSI 降序连接。

**Architecture:** 修改 `DesktopRelay` 的回调行为和 `run()` 的扫描编排逻辑，提取 `_try_pair_device` 复用连接+配对循环，新增 `_dequeue_device_by_address` 和 `_process_pairing_queue`。`ble_client.py` 不改。

**Tech Stack:** Python 3.11+, asyncio, bleak

## Global Constraints

- `ble_client.py` 不改
- `config.py` 不改
- 已保存设备直连流程不变
- `_keepalive_task` 不变
- 配对对话框 UI 不变

---

### Task 1: Add `_dequeue_device_by_address` method

**Files:**
- Modify: `desktop-relay/main.py`

**Interfaces:**
- Produces: `DesktopRelay._dequeue_device_by_address(self, address: str) -> dict | None`

- [ ] **Step 1: Add the method after `_dequeue_device`**

After `_dequeue_device` (line 104), add:

```python
    def _dequeue_device_by_address(self, address: str) -> dict | None:
        """按地址精确出队，返回设备信息字典，未找到返回 None"""
        with self._queue_lock:
            for i, device in enumerate(self._pairing_queue):
                if device['address'] == address:
                    return self._pairing_queue.pop(i)
        return None
```

- [ ] **Step 2: Verify the file parses correctly**

```bash
python -c "import ast; ast.parse(open('desktop-relay/main.py').read()); print('OK')"
```

Expected: `OK`

- [ ] **Step 3: Commit**

```bash
git add desktop-relay/main.py
git commit -m "feat: add _dequeue_device_by_address for precise queue removal"
```

---

### Task 2: Add `_queue_processing_started` flag to `__init__`

**Files:**
- Modify: `desktop-relay/main.py`

**Interfaces:**
- Produces: `self._queue_processing_started: bool` (initialized `False`)

- [ ] **Step 1: Add the flag in `__init__`**

In `__init__`, after `self._failed_devices = set()` (line 43), add:

```python
        # 队列处理是否已启动（防重复触发）
        self._queue_processing_started = False
```

- [ ] **Step 2: Verify parse**

```bash
python -c "import ast; ast.parse(open('desktop-relay/main.py').read()); print('OK')"
```

Expected: `OK`

- [ ] **Step 3: Commit**

```bash
git add desktop-relay/main.py
git commit -m "feat: add _queue_processing_started flag to prevent duplicate queue processing"
```

---

### Task 3: Extract `_try_pair_device` and rewrite `_on_device_found`

**Files:**
- Modify: `desktop-relay/main.py`

**Interfaces:**
- Consumes: `_dequeue_device_by_address(self, address: str) -> dict | None` (from Task 1)
- Produces: `DesktopRelay._try_pair_device(self, address: str, name: str, paired_event: asyncio.Event) -> bool`
- Produces: `_on_device_found` callback with new behavior (always enqueue, ClaudeCodeIndicator priority)

- [ ] **Step 1: Add `_try_pair_device` method**

Add before `_keepalive_task` (around line 217). This extracts the connect+pair loop from the current `_on_device_found`:

```python
    async def _try_pair_device(self, address: str, name: str,
                               paired_event: asyncio.Event) -> bool:
        """尝试连接并配对单个设备。成功返回 True，失败返回 False。"""
        if not await self._start_pairing(address):
            return False

        success = False
        try:
            if await self.ble.connect_and_verify(address):
                logger.debug(f"Connected to {address}, {name}, starting pairing flow")
                if await self._pairing_flow(address, name):
                    success = True
                    paired_event.set()
                    self._clear_queue()
                    self.ble.stop_scan()
                    logger.info(f"Pairing completed successfully with {address}")
                else:
                    logger.info(f"Pairing flow returned False for {address}")
            else:
                logger.warning(f"Failed to connect/verify {address}")

        except asyncio.TimeoutError as e:
            logger.error(f"Timeout during pairing with {address}: {e}", exc_info=True)
        except ConnectionError as e:
            logger.error(f"Connection error during pairing with {address}: {e}", exc_info=True)
        except Exception as e:
            logger.error(f"Unexpected error during pairing with {address}: {e}", exc_info=True)
        finally:
            await self._end_pairing()
            if not success:
                try:
                    if self.ble.is_connected:
                        await self.ble.disconnect()
                        logger.debug(f"Disconnected from {address}")
                except Exception as disconnect_error:
                    logger.warning(f"Disconnect error: {disconnect_error}")

        if not success:
            self._failed_devices.add(address)

        return success
```

- [ ] **Step 2: Rewrite `_on_device_found` callback with new behavior**

Replace the entire `_on_device_found` function body (currently lines 285-410 in `run()`). The new version: always enqueue first, only immediately connect ClaudeCodeIndicator devices:

```python
                        async def _on_device_found(address, name, rssi):
                            # 标准化地址（大写）用于比较
                            address = address.upper()

                            # 如果已经配对成功，跳过所有后续设备
                            if paired_event.is_set():
                                return

                            # 检查设备是否正在配对中
                            async with self._pairing_lock:
                                if self._pairing_address and self._pairing_address.upper() == address:
                                    logger.debug(f"Skipping {address}: device is being paired")
                                    return

                            # 检查设备是否已配对失败过
                            if address in self._failed_devices:
                                logger.debug(f"Skipping {address}: device has been paired and failed before")
                                return

                            # 一步：所有设备一律先入队
                            await self._enqueue_device(address, name, rssi)

                            # 二步：只有 ClaudeCodeIndicator 才立即连接
                            if name != "ClaudeCodeIndicator":
                                return

                            # 三步：如果正在配对中，跳过（设备留在队列，由 _process_pairing_queue 处理）
                            if self._pairing_in_progress:
                                return

                            # 四步：按地址精确出队（取出自己，不是队首）
                            device = self._dequeue_device_by_address(address)
                            if device is None:
                                return

                            logger.info(f"ClaudeCodeIndicator found, immediate pairing: {address}")
                            await self._try_pair_device(address, name, paired_event)
```

- [ ] **Step 3: Verify parse**

```bash
python -c "import ast; ast.parse(open('desktop-relay/main.py').read()); print('OK')"
```

Expected: `OK`

- [ ] **Step 4: Commit**

```bash
git add desktop-relay/main.py
git commit -m "feat: extract _try_pair_device, rewrite _on_device_found with ClaudeCodeIndicator priority"
```

---

### Task 4: Add `_process_pairing_queue` method

**Files:**
- Modify: `desktop-relay/main.py`

**Interfaces:**
- Consumes: `_try_pair_device(self, address, name, paired_event) -> bool` (from Task 3)
- Consumes: `_queue_processing_started` flag (from Task 2)
- Produces: `DesktopRelay._process_pairing_queue(self, paired_event: asyncio.Event) -> None`

- [ ] **Step 1: Add the method before `_keepalive_task`**

After `_try_pair_device`, add:

```python
    async def _process_pairing_queue(self, paired_event: asyncio.Event):
        """按 RSSI 降序处理配对队列中的剩余设备（扫描结束/超时后调用）"""
        with self._queue_lock:
            devices = sorted(self._pairing_queue, key=lambda d: d['rssi'], reverse=True)
            self._pairing_queue.clear()

        if not devices:
            logger.info("Queue empty, no devices to process")
            return

        logger.info(f"Processing queue: {len(devices)} devices sorted by RSSI")
        for device in devices:
            if paired_event.is_set():
                logger.info("Already paired, stopping queue processing")
                return

            address = device['address']
            name = device['name']
            rssi = device['rssi']

            logger.info(f"Trying device from queue: {address} (RSSI={rssi}, name={name})")
            success = await self._try_pair_device(address, name, paired_event)
            if success:
                return
            logger.info(f"Device {address} failed, trying next in queue...")
```

- [ ] **Step 2: Verify parse**

```bash
python -c "import ast; ast.parse(open('desktop-relay/main.py').read()); print('OK')"
```

Expected: `OK`

- [ ] **Step 3: Commit**

```bash
git add desktop-relay/main.py
git commit -m "feat: add _process_pairing_queue to handle devices after scan completes"
```

---

### Task 5: Modify `run()` phase 2 — asyncio.wait dual trigger

**Files:**
- Modify: `desktop-relay/main.py` — the `run()` method phase 2 (lines 278-430)

**Interfaces:**
- Consumes: All new methods from Tasks 1-4

- [ ] **Step 1: Replace phase 2 scan+pair block with dual-trigger logic**

Replace lines 278-430 (the entire `else` block of phase 2) with:

```python
            else:
                # 阶段2: 无已保存设备，扫描+配对
                paired = False
                while not paired:
                    try:
                        paired_event = asyncio.Event()
                        self._queue_processing_started = False

                        async def _on_device_found(address, name, rssi):
                            # 标准化地址（大写）用于比较
                            address = address.upper()

                            # 如果已经配对成功，跳过所有后续设备
                            if paired_event.is_set():
                                return

                            # 检查设备是否正在配对中
                            async with self._pairing_lock:
                                if self._pairing_address and self._pairing_address.upper() == address:
                                    logger.debug(f"Skipping {address}: device is being paired")
                                    return

                            # 检查设备是否已配对失败过
                            if address in self._failed_devices:
                                logger.debug(f"Skipping {address}: device has been paired and failed before")
                                return

                            # 一步：所有设备一律先入队
                            await self._enqueue_device(address, name, rssi)

                            # 二步：只有 ClaudeCodeIndicator 才立即连接
                            if name != "ClaudeCodeIndicator":
                                return

                            # 三步：如果正在配对中，跳过（设备留在队列）
                            if self._pairing_in_progress:
                                return

                            # 四步：按地址精确出队（取出自己，不是队首）
                            device = self._dequeue_device_by_address(address)
                            if device is None:
                                return

                            logger.info(f"ClaudeCodeIndicator found, immediate pairing: {address}")
                            await self._try_pair_device(address, name, paired_event)

                        # 并发：扫描 + 10秒计时器
                        scan_task = asyncio.create_task(
                            self.ble.scan_devices(detection_callback=_on_device_found)
                        )
                        timer_task = asyncio.create_task(asyncio.sleep(10))

                        done, pending = await asyncio.wait(
                            [scan_task, timer_task],
                            return_when=asyncio.FIRST_COMPLETED
                        )

                        # 防重复触发：先到先处理
                        if not paired_event.is_set() and not self._queue_processing_started:
                            self._queue_processing_started = True
                            await self._process_pairing_queue(paired_event)

                        # 取消未完成的计时器，保留扫描
                        if not timer_task.done():
                            timer_task.cancel()
                            try:
                                await timer_task
                            except asyncio.CancelledError:
                                pass

                        # 等待扫描自然结束
                        if not scan_task.done():
                            try:
                                await scan_task
                            except asyncio.CancelledError:
                                pass

                        if paired_event.is_set():
                            paired = True
                        else:
                            logger.warning(
                                f"No devices found or paired, "
                                f"retrying in {SCAN_RETRY_INTERVAL}s..."
                            )
                            self._clear_queue()
                            self._failed_devices.clear()
                            await asyncio.sleep(SCAN_RETRY_INTERVAL)
                    except Exception as e:
                        logger.error(
                            f"Scan/connect error: {e}, "
                            f"retrying in {SCAN_RETRY_INTERVAL}s..."
                        )
                        self._clear_queue()
                        self._failed_devices.clear()
                        await asyncio.sleep(SCAN_RETRY_INTERVAL)
```

- [ ] **Step 2: Verify parse**

```bash
python -c "import ast; ast.parse(open('desktop-relay/main.py').read()); print('OK')"
```

Expected: `OK`

- [ ] **Step 3: Review the full run() method for correctness**

- `_on_device_found` is defined inside the while loop so it captures `paired_event`
- `_queue_processing_started` reset each retry iteration
- Scan task is always awaited to completion
- Timer task is cancelled if not needed
- Exception handler also cleans up queue and failed_devices

- [ ] **Step 4: Commit**

```bash
git add desktop-relay/main.py
git commit -m "feat: dual-trigger scan+pair with ClaudeCodeIndicator priority queue"
```
