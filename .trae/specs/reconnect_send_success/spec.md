# 重新连接成功发送消息 Spec

## Why
当前配对流程在配对成功时会发送 `PAIR_SUCCESS` 消息，但对于已经保存的设备，重新连接成功后不会发送任何成功消息。用户希望无论设备是首次配对还是重新连接，连接成功后都发送成功消息通知设备。

## What Changes
- 在重新连接成功后发送成功消息（如 `CONNECT_SUCCESS` 或 `PAIR_SUCCESS`）
- 确保设备在任何情况下连接成功都能收到成功通知

## Impact
- Affected specs: ble_pairing_blocking, ble_pairing_queue
- Affected code: desktop-relay/ble_client.py (reconnect_loop), desktop-relay/main.py

## ADDED Requirements

### Requirement: 重新连接成功通知
系统应在成功连接到已保存设备后发送成功消息。

#### Scenario: 重新连接成功
- **WHEN** 系统通过 `reconnect_loop` 成功连接到已保存的设备
- **THEN** 系统应发送成功消息（如 `PAIR_SUCCESS`）
- **AND** 设备收到成功消息后可以执行相应的初始化操作

#### Scenario: 首次配对成功
- **WHEN** 系统首次配对成功
- **THEN** 系统应发送 `PAIR_SUCCESS` 消息（保持现有行为）

## MODIFIED Requirements

### Requirement: 重连流程
修改重连流程以支持发送成功消息。

**原有行为**: 重新连接成功后不发送任何消息
**修改后行为**: 重新连接成功后发送成功消息

## Acceptance Criteria

### AC-1: 重新连接成功发送消息
- **Given**: 设备已保存，系统正在重连
- **When**: 重连成功
- **Then**: 系统发送 `PAIR_SUCCESS` 消息
- **Verification**: `programmatic`

### AC-2: 首次配对成功发送消息
- **Given**: 设备未保存，系统正在配对
- **When**: 配对成功
- **Then**: 系统发送 `PAIR_SUCCESS` 消息
- **Verification**: `programmatic`