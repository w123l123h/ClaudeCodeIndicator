# Application 单例类规范

## Why
当前 `main.cpp` 中使用多个全局指针管理各类实例，代码结构松散，初始化逻辑分散。创建 `Application` 单例类可以将所有组件实例集中管理，提高代码的可维护性和可测试性，使 `main.cpp` 更加简洁。

## What Changes
- 创建 `Application` 单例类，包含所有组件实例作为成员变量
- 将 `main.cpp` 中的初始化逻辑、回调函数和主循环逻辑移入 `Application` 类
- 简化 `main.cpp`，仅调用 `Application::instance().run()`

## Impact
- Affected specs: 无
- Affected code:
  - `main/main.cpp` - 简化为仅调用 Application 启动
  - `main/application.h` - 新建头文件
  - `main/application.cpp` - 新建实现文件

## ADDED Requirements

### Requirement: Application 单例类
系统应提供 `Application` 单例类来管理所有组件实例和应用程序生命周期。

#### Scenario: 获取单例实例
- **WHEN** 调用 `Application::instance()` 方法
- **THEN** 返回唯一的 Application 实例

#### Scenario: 组件成员变量
- **WHEN** Application 实例创建
- **THEN** 包含 LedController、LedStateManager、BleServer、BatteryMonitor、PowerManager 成员变量

### Requirement: 应用初始化
Application 类应提供 `init()` 和 `run()` 方法管理应用生命周期。

#### Scenario: 初始化组件
- **WHEN** 调用 `init()` 方法
- **THEN** 初始化 NVS、LED、BLE、电池监测、电源管理等组件

#### Scenario: 自检序列
- **WHEN** 初始化过程中
- **THEN** 执行 LED 自检序列（红、橙、绿依次点亮）

#### Scenario: 运行主循环
- **WHEN** 调用 `run()` 方法
- **THEN** 进入主循环处理 LED 闪烁和 BLE 休眠

### Requirement: 回调处理
Application 类应内部处理所有 BLE 和电池回调。

#### Scenario: BLE 消息回调
- **WHEN** 收到 BLE 消息
- **THEN** Application 内部处理 JSON 指令、保活、配对等消息

#### Scenario: BLE 连接回调
- **WHEN** BLE 连接状态变化
- **THEN** Application 内部更新 LED 状态和电源管理

#### Scenario: 电池低电量回调
- **WHEN** 电池电量低于阈值
- **THEN** Application 内部设置 LED0 红色闪烁

### Requirement: main.cpp 简化
main.cpp 应仅包含 Application 启动调用。

#### Scenario: 应用入口
- **WHEN** `app_main()` 函数执行
- **THEN** 仅调用 `Application::instance().run()` 启动应用