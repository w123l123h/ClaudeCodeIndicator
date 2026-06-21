# GPIO10 充电检测功能规范

## Why
需要通过 GPIO10 检测充电状态，当检测到高电平时打印"充电中"信息。这是一个简单的硬件检测功能，用于指示设备是否正在充电。

## What Changes
- 在 config.h 中添加 CHARGE_DETECT_GPIO 定义（GPIO10）
- 在 Application 类中添加 GPIO10 初始化和中断处理逻辑
- 配置 GPIO10 为输入模式，高电平触发中断
- 中断触发时打印"充电中"日志信息

## Impact
- Affected specs: 无
- Affected code:
  - `main/config.h` - 添加 GPIO 定义
  - `main/application.cpp` - 添加 GPIO 初始化和中断处理
  - `main/application.h` - 添加声明

## ADDED Requirements

### Requirement: GPIO10 充电检测
系统应检测 GPIO10 的高电平状态，当检测到高电平时输出"充电中"信息。

#### Scenario: 充电检测初始化
- **WHEN** Application 初始化时
- **THEN** GPIO10 被配置为输入模式，启用高电平触发中断

#### Scenario: 高电平触发
- **WHEN** GPIO10 检测到高电平
- **THEN** 打印日志信息"充电中"

#### Scenario: 中断处理
- **WHEN** GPIO10 发生中断
- **THEN** 正确处理中断并输出相应状态信息