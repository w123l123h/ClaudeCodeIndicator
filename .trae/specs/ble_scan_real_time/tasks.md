# 蓝牙实时扫描功能 - 实现计划

## [x] Task 1: 修改 BleClientManager 支持实时扫描回调
- **Priority**: P0
- **Depends On**: None
- **Description**: 
  - 修改 `scan_devices` 方法，添加回调参数支持实时设备发现
  - 使用 BleakScanner 的异步上下文管理器模式，支持 `detection_callback`
  - 实现设备去重逻辑，避免重复处理同一设备
- **Acceptance Criteria Addressed**: AC-1, AC-3
- **Test Requirements**:
  - `programmatic` TR-1.1: 扫描方法应接受回调函数参数
  - `programmatic` TR-1.2: 扫描应支持提前停止（取消）
  - `human-judgement` TR-1.3: 代码应保持清晰的结构和适当的注释

## [x] Task 2: 修改 main.py 配对流程使用实时扫描
- **Priority**: P0
- **Depends On**: Task 1
- **Description**: 
  - 修改 `run()` 方法中的配对流程
  - 替换原来的 `await self.ble.scan_devices()` 调用
  - 使用新的实时扫描接口，扫描到设备立即尝试连接
- **Acceptance Criteria Addressed**: AC-1, AC-2, AC-3
- **Test Requirements**:
  - `programmatic` TR-2.1: 设备发现后应立即尝试连接
  - `human-judgement` TR-2.2: 配对流程逻辑应清晰可读

## [x] Task 3: 添加扫描取消机制
- **Priority**: P1
- **Depends On**: Task 1
- **Description**: 
  - 实现扫描取消方法，允许在配对成功后立即停止扫描
  - 使用 asyncio.Event 或类似机制实现扫描停止信号
- **Acceptance Criteria Addressed**: AC-3
- **Test Requirements**:
  - `programmatic` TR-3.1: 配对成功后应能立即停止扫描
  - `programmatic` TR-3.2: 扫描停止后不应继续发现新设备
