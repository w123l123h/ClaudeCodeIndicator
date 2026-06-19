# LED 状态管理类封装规范

## Why
当前 `LedState` 结构体及其相关逻辑分散在 `main.cpp` 中，使用全局变量和静态函数管理状态，代码耦合度高且难以维护。封装为 C++ 类可以提高代码的模块化程度、可读性和可维护性。

## What Changes
- 创建 `LedStateManager` 类，封装 LED 状态管理逻辑
- 将 `LedState` 结构体作为类的私有成员
- 将所有 LED 状态相关的全局变量和函数封装到类中
- 重构 `main.cpp`，使用新的 `LedStateManager` 类

## Impact
- Affected specs: 无
- Affected code:
  - `main/main.cpp` - 移除 LedState 结构体和相关全局变量/函数
  - `main/led_state_manager.h` - 新建头文件
  - `main/led_state_manager.cpp` - 新建实现文件

## ADDED Requirements

### Requirement: LED 状态管理类
系统应提供 `LedStateManager` 类来管理所有 LED 的运行时状态。

#### Scenario: 初始化 LED 状态管理器
- **WHEN** 创建 `LedStateManager` 实例并调用 `init()` 方法
- **THEN** 所有 LED 状态被初始化为默认值，定时器句柄为空

#### Scenario: 执行 LED 指令
- **WHEN** 调用 `apply_command()` 方法设置 LED 状态
- **THEN** LED 状态被正确更新，定时器被正确管理

#### Scenario: 处理 LED 超时
- **WHEN** LED 定时器超时
- **THEN** LED 被关闭，闪烁状态被重置

### Requirement: 状态查询接口
`LedStateManager` 类应提供状态查询接口。

#### Scenario: 查询闪烁状态
- **WHEN** 调用 `is_blinking()` 方法
- **THEN** 返回指定 LED 的闪烁状态

#### Scenario: 查询颜色
- **WHEN** 调用 `get_color()` 方法
- **THEN** 返回指定 LED 的当前颜色

### Requirement: 回调支持
`LedStateManager` 类应支持设置回调函数。

#### Scenario: 设置低电量回调
- **WHEN** 调用 `set_battery_low()` 方法
- **THEN** LED0 的状态根据低电量标志被正确处理

#### Scenario: 处理配对状态
- **WHEN** 调用 `handle_pair_confirm()` 或 `handle_pair_success()` 方法
- **THEN** 所有 LED 状态被正确处理

### Requirement: 主循环集成
`LedStateManager` 类应提供 `tick()` 方法用于主循环调用。

#### Scenario: 闪烁处理
- **WHEN** 主循环调用 `tick()` 方法
- **THEN** 闪烁的 LED 根据周期正确切换状态

#### Scenario: 休眠处理
- **WHEN** 调用 `prepare_sleep()` 方法
- **THEN** 所有 LED 被关闭，定时器被暂停