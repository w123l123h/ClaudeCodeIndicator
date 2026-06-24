# BLE 扫描优先队列配对 — Design

**日期:** 2026-06-24
**状态:** 已批准

## 问题

当前配对流程在扫描期间发现任何设备就立即开始逐个连接配对（FIFO），不区分优先级。如果周围有多个蓝牙设备，可能先连接到无关设备上浪费时间。

## 目标

- 扫描到设备后一律先入队
- 如果设备名是 `ClaudeCodeIndicator`，立即连接配对（最高优先级，跳过队列）
- 其他设备只留在队列中，不立即连接
- 如果扫描期间没有 `ClaudeCodeIndicator` 配对成功，在 **扫描结束** 或 **扫描开始 10 秒后**（两个触发源并发，哪个先到都行），按 RSSI 降序逐个处理队列中的设备

## 流程

```
ble.scan_devices(detection_callback)  +  asyncio.sleep(10)
        │                                      │
        ├─ 发现设备 → _on_device_found         │
        │   ├─ 一律入队                        │
        │   ├─ name == "ClaudeCodeIndicator"   │
        │   │   └─ 按地址精确出队 → 立即连接配对 │
        │   └─ 其他 → 只入队，不连接            │
        │                                      │
        ├─ 扫描结束 ──────────────────────────┼─→ if 未配对 → _process_pairing_queue()
        │                                      │
        └──────────────────────────────────────┼─ 10秒到 → if 未配对 → _process_pairing_queue()
```

- 两个触发源完全独立，互不干扰
- 先到的先触发，后到的通过 `_queue_processing_started` 标记跳过
- 扫描不会被中断或提前停止

## main.py 改动

### `_on_device_found` 回调 — 重写

| 当前行为 | 新行为 |
|---------|--------|
| 设备入队后，如果没有配对进行中，立即 pop 队首并开始配对 | 设备一律先入队 |
| 不区分设备名称 | `name == "ClaudeCodeIndicator"` → 按地址精确出队，立即连接配对 |
| | 其他名称 → 只留在队列中，return |

### 新增 `_dequeue_device_by_address(address)` — 按地址精确出队

`ClaudeCodeIndicator` 立即连接时需要从队列中取出它自己，而不是 pop 队首。现有 `_dequeue_device()` 只支持 FIFO pop(0)。

```python
def _dequeue_device_by_address(self, address: str) -> dict | None:
    """按地址精确出队，返回设备信息字典，未找到返回 None"""
    with self._queue_lock:
        for i, device in enumerate(self._pairing_queue):
            if device['address'] == address:
                return self._pairing_queue.pop(i)
    return None
```

### 新增 `_process_pairing_queue()` — 扫描结束后处理队列

```python
async def _process_pairing_queue(self):
    """按 RSSI 降序处理配对队列中的剩余设备"""
    with self._queue_lock:
        devices = sorted(self._pairing_queue, key=lambda d: d['rssi'], reverse=True)
        self._pairing_queue.clear()
    
    for device in devices:
        if not await self._start_pairing(device['address']):
            continue
        # 复用现有连接+配对循环逻辑...
```

### `run()` 阶段2 — asyncio.wait 双触发

```python
# 并发：扫描 + 10秒计时器
scan_task = asyncio.create_task(
    self.ble.scan_devices(detection_callback=_on_device_found)
)
timer_task = asyncio.create_task(asyncio.sleep(10))

done, pending = await asyncio.wait(
    [scan_task, timer_task],
    return_when=asyncio.FIRST_COMPLETED
)

# 防重复触发
if not paired_event.is_set() and not self._queue_processing_started:
    self._queue_processing_started = True
    await self._process_pairing_queue()
```

### 新增 `_queue_processing_started` 标记

防止两个触发源（扫描结束 + 10s 计时器）重复调用 `_process_pairing_queue`。

## ble_client.py

不改。

## config.py

不改。

## 边界情况

| 情况 | 行为 |
|------|------|
| 扫描到多个 `ClaudeCodeIndicator` | 第一个立即连接，后续的被 `paired_event.is_set()` 跳过 |
| `ClaudeCodeIndicator` 配对失败 | 设备加入 `_failed_devices`，继续扫描 |
| 10s 计时器先触发，扫描还在跑 | 触发队列处理，扫描继续正常跑 |
| 扫描结束先触发 | 同上，直接处理队列 |
| 两个触发源几乎同时到达 | `_queue_processing_started` 防重 |
| 队列为空（没扫描到任何设备） | `_process_pairing_queue` 空操作，外层正常重试 |
| 扫描期间已配对成功 | 两个触发源都检查 `paired_event.is_set()`，跳过 |

## 不在范围内

- `_keepalive_task` 不变
- 已保存设备直连流程不变
- 配对对话框 UI 不变
- 固件侧不变
