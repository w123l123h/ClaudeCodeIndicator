# Scan Once, Pair Iteratively — Design

**日期:** 2026-06-15
**状态:** 已批准

## 问题

当前 `scan_and_connect()` 在发现第一个匹配设备后就返回，如果配对被用户拒绝，外层循环会重新扫描再尝试。这种不必要的重复扫描浪费时间。

## 目标

- 扫描一次获取所有蓝牙设备，按信号强度逐个尝试
- 连接成功但配对被拒 → 继续下一个，不重新扫描
- 所有设备试完 → 重新扫描
- 如果 `device.json` 存在，只直连已保存设备，不扫描，直到连接成功
- `reconnect_loop` 也只重连已保存设备，不做扫描

## 流程

```
run()
 ├─ device.json 存在?
 │   └─ 是 → while True:
 │            connect_and_verify(saved)
 │              ├─ 成功 → break，进入 keepalive
 │              └─ 失败 → sleep 3s → 重试
 └─ 否 → while True:
           devices = scan_devices()
           for each device (sorted by RSSI desc):
             connect_and_verify → 失败 → 下一个
             connect_and_verify → 成功 → pairing_flow
               ├─ 用户确认 → save → break
               └─ 用户拒绝 → disconnect → 下一个
           全耗尽 → sleep 5s → 重新扫描
 └─ asyncio.gather(keepalive_task, reconnect_loop)
```

## ble_client.py 改动

| 变更 | 说明 |
|------|------|
| **新增** `scan_devices()` | 扫描返回 `[(addr, name, rssi), ...]`，按 RSSI 降序排列 |
| **公开** `connect_and_verify(address)` | 原 `_connect_and_verify`，逻辑不变 |
| **删除** `scan_and_connect()` | 不再需要组合方法 |
| **修改** `reconnect_loop()` | 改为只连 `get_saved_device()`，无已保存设备则跳过并警告 |

### `scan_devices()` 签名

```python
async def scan_devices(self) -> list[tuple[str, str, int]]:
    """
    扫描 BLE 设备，返回按 RSSI 降序的列表。
    每项: (address, name, rssi)
    name 可能为 "(no name)"。
    """
```

### `reconnect_loop()` 防御

```python
saved = self.get_saved_device()
if not saved:
    logger.warning("No saved device, skipping reconnect")
    await asyncio.sleep(5)
    continue
success = await self.connect_and_verify(saved)
if not success:
    await asyncio.sleep(RECONNECT_BASE_DELAY)
```

## main.py 改动

| 变更 | 说明 |
|------|------|
| **修改** `_pairing_flow(address)` | 参数显式传入，不再依赖 `self.ble.address` |
| **重写** `run()` | 三阶段流程 |
| **不变** | `_keepalive_task`, `shutdown`, `_on_ble_message`, `_on_http_message` |

## config.py 新增常量

```python
SAVED_RETRY_INTERVAL = 3   # 已保存设备重试间隔（秒）
SCAN_RETRY_INTERVAL = 5    # 扫描全部失败后重试间隔（秒）
```

## 边界情况

| 情况 | 行为 |
|------|------|
| 扫描到 0 个设备 | sleep 5s 重新扫描 |
| 所有设备连接失败 | sleep 5s 重新扫描 |
| 连接成功但 service UUID 不匹配 | 跳过，继续下一个 |
| 配对对话框用户点"否" | 断开连接，继续下一个 |
| 配对中途 BLE 断开 | 当作失败，继续下一个 |
| 同一设备在后续扫描中再次出现 | 可以重新尝试（不记忆失败设备跨扫描轮次） |
| `reconnect_loop` 运行时无已保存设备 | 警告并跳过，sleep 5s 后重试 |

## 不在范围内

- `_keepalive_task` 逻辑不变
- 配对对话框 UI 不变
- BLE 加密/绑定不变
- 固件侧逻辑不变
