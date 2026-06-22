# 蓝牙实时扫描功能 - 产品需求文档

## Overview
- **Summary**: 修改蓝牙扫描机制，将"等待扫描完成后批量处理"改为"扫描到设备立即处理"，提升配对响应速度。
- **Purpose**: 减少用户等待时间，当设备进入扫描范围时立即尝试连接，而不必等待整个扫描周期完成。
- **Target Users**: 使用 ClaudeCodeIndicator 进行首次配对的用户。

## Goals
- 实现实时设备发现和处理，无需等待扫描超时
- 保持原有功能完整性（设备排序、过滤等）
- 提高配对流程的响应速度

## Non-Goals (Out of Scope)
- 不修改蓝牙连接和验证逻辑
- 不修改配对确认对话框逻辑
- 不修改保活和重连机制

## Background & Context
当前实现使用 `BleakScanner.discover()` 同步等待整个扫描周期（由 `BLE_SCAN_TIMEOUT` 配置）完成后，才返回设备列表并逐个尝试连接。这导致用户需要等待完整扫描周期才能开始配对流程。

## Functional Requirements
- **FR-1**: 扫描到设备后立即触发回调，而不是等待扫描完成
- **FR-2**: 支持按 RSSI 和设备名称排序的实时处理
- **FR-3**: 优先处理名为 "ClaudeCodeIndicator" 的设备

## Non-Functional Requirements
- **NFR-1**: 保持与现有代码的兼容性
- **NFR-2**: 不增加额外的内存开销

## Constraints
- **Technical**: 使用 Bleak 库的 Scanner API，需要支持回调模式
- **Dependencies**: Bleak >= 0.20.0

## Assumptions
- BleakScanner 支持回调模式（`detection_callback` 参数）
- 实时处理不会导致连接尝试过于频繁

## Acceptance Criteria

### AC-1: 实时设备发现
- **Given**: 蓝牙扫描正在进行
- **When**: 发现新设备
- **Then**: 立即调用回调函数处理该设备，无需等待扫描完成
- **Verification**: `programmatic`

### AC-2: 设备优先级处理
- **Given**: 扫描到多个设备，其中包含名为 "ClaudeCodeIndicator" 的设备
- **When**: 设备发现回调被触发
- **Then**: "ClaudeCodeIndicator" 设备优先于其他设备被处理
- **Verification**: `human-judgment`

### AC-3: 扫描取消支持
- **Given**: 扫描正在进行且已找到目标设备
- **When**: 配对成功
- **Then**: 立即停止扫描
- **Verification**: `programmatic`

## Open Questions
- [ ] 当多个设备同时被发现时，是否需要进行去重处理？
- [ ] 是否需要限制同一设备的重复处理？