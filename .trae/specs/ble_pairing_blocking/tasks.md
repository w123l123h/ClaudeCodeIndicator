# 阻塞式蓝牙配对流程改进 - 实现计划

## [x] Task 1: 添加配对状态管理
- **Priority**: P0
- **Depends On**: None
- **Description**:
  - 在 `DesktopRelay` 类中添加配对状态管理变量 `_pairing_in_progress` 和 `_pairing_address`
  - 添加方法 `_start_pairing(address)` 和 `_end_pairing()` 来管理配对状态
  - 确保配对状态的原子性操作
- **Acceptance Criteria Addressed**: 设备处理锁定机制
- **Test Requirements**:
  - `programmatic` TR-1.1: 配对状态应能正确设置和清除
  - `programmatic` TR-1.2: 同一时间只能有一个设备在配对中

## [x] Task 2: 修改配对流程为阻塞式
- **Priority**: P0
- **Depends On**: Task 1
- **Description**:
  - 修改 `_on_device_found` 回调函数，在开始配对前检查配对状态
  - 如果已有设备在配对中，跳过当前设备
  - 开始配对时，立即停止扫描
  - 配对成功后，设置配对完成标志
  - 配对失败后，重新启动扫描
- **Acceptance Criteria Addressed**: 阻塞式配对流程
- **Test Requirements**:
  - `programmatic` TR-2.1: 配对开始时应立即停止扫描
  - `programmatic` TR-2.2: 配对失败后应重新启动扫描
  - `human-judgement` TR-2.3: 配对流程应清晰可读，避免竞态条件

## [x] Task 3: 优化配对流程错误处理
- **Priority**: P1
- **Depends On**: Task 2
- **Description**:
  - 在 `_pairing_flow` 方法中添加更完善的错误处理
  - 确保配对失败时正确清理状态
  - 添加日志记录，方便调试
- **Acceptance Criteria Addressed**: 配对流程状态管理
- **Test Requirements**:
  - `programmatic` TR-3.1: 配对失败时应正确清理状态
  - `programmatic` TR-3.2: 配对失败时应断开连接
  - `human-judgement` TR-3.3: 错误处理应完善，避免资源泄漏

# Task Dependencies
- [Task 2] depends on [Task 1]
- [Task 3] depends on [Task 2]