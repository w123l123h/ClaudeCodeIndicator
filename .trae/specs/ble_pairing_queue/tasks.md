# 队列式配对流程 - 实现计划

## [x] Task 1: 添加配对队列管理
- **Priority**: P0
- **Depends On**: None
- **Description**:
  - 在 `DesktopRelay` 类中添加配对队列 `_pairing_queue`
  - 添加队列管理方法：
    - `_enqueue_device(address, name, rssi)`: 将设备添加到队列
    - `_dequeue_device()`: 从队列取出下一个设备
    - `_clear_queue()`: 清空队列
    - `_get_queue_size()`: 获取队列大小
  - 确保队列操作是线程安全的（使用 asyncio.Lock）
- **Acceptance Criteria Addressed**: 配对队列管理
- **Test Requirements**:
  - `programmatic` TR-1.1: 队列能正确添加、取出和清空设备
  - `programmatic` TR-1.2: 队列操作是线程安全的

## [ ] Task 2: 修改扫描回调支持入队
- **Priority**: P0
- **Depends On**: Task 1
- **Description**:
  - 修改 `_on_device_found` 回调函数
  - 将发现的设备添加到配对队列而不是立即配对
  - 如果队列只有一个设备且没有设备在配对中，开始配对
  - 否则只入队，等待当前配对完成后处理
- **Acceptance Criteria Addressed**: 设备入队
- **Test Requirements**:
  - `programmatic` TR-2.1: 发现的设备能正确入队
  - `programmatic` TR-2.2: 队列为空时能正确开始配对

## [x] Task 2: 修改扫描回调支持入队

## [x] Task 3: 修改配对失败处理逻辑

## [x] Task 4: 修改配对成功处理逻辑

## [x] Task 5: 添加队列状态日志
- **Priority**: P1
- **Depends On**: Task 1
- **Description**:
  - 在关键操作点添加队列状态日志
  - 设备入队时记录日志
  - 队列处理时记录日志
  - 队列为空时记录日志
- **Acceptance Criteria Addressed**: 队列状态显示
- **Test Requirements**:
  - `human-judgement` TR-5.1: 日志能清晰显示队列状态变化

## [x] Task 6: 添加队列去重逻辑
- **Priority**: P0
- **Depends On**: Task 1
- **Description**:
  - 在 `_enqueue_device` 方法中添加去重检查
  - 检查设备是否已在队列中（通过地址比较）
  - 检查设备是否正在配对中（通过 `_pairing_address`）
  - 如果设备已存在，跳过入队并记录日志
- **Acceptance Criteria Addressed**: 队列去重
- **Test Requirements**:
  - `programmatic` TR-6.1: 重复设备不会被添加到队列
  - `programmatic` TR-6.2: 正在配对的设备不会被添加到队列
  - `human-judgement` TR-6.3: 去重时有清晰的日志提示

## [x] Task 7: 添加重新扫描清空队列逻辑
- **Priority**: P0
- **Depends On**: None
- **Description**:
  - 在重新扫描前清空队列
  - 在重新扫描前清空失败设备列表
  - 确保每次重新扫描都是全新的开始
- **Acceptance Criteria Addressed**: 重新扫描清空
- **Test Requirements**:
  - `programmatic` TR-7.1: 重新扫描前队列被清空
  - `programmatic` TR-7.2: 重新扫描前失败设备列表被清空

# Task Dependencies
- [Task 2] depends on [Task 1]
- [Task 3] depends on [Task 2]
- [Task 4] depends on [Task 2]
- [Task 5] depends on [Task 1]
- [Task 6] depends on [Task 1]