# Fix BLE Reconnect After Abnormal Exit — Design

**Date:** 2026-06-12
**Status:** approved

## Problem

`main.py` exits abnormally (crash, kill, Ctrl+C) → Python restart cannot reconnect to ESP32 → must power-cycle hardware.

### Root Cause

ESP32 disconnect callback in `ble_server.cpp` calls `ble_gap_adv_start()` **without checking the return value**. NimBLE silently fails to start advertising (likely EBUSY because the previous advertising session is still registered). ESP32's application-level state says "advertising" (green LED blinks), but no BLE advertisement packets are on air.

Evidence: after disconnect, phone BLE scanner cannot see the ESP32 device.

### Why Reset Hardware Works

ESP32 reboot clears all NimBLE state. `start_advertise()` has retry logic that handles EBUSY, so fresh-boot advertising always succeeds.

## Design

### Scope

Single file: `firmware/main/ble_server.cpp` + header declaration in `firmware/main/ble_server.h`

### New Method: `_advertise_with_retry()`

```
void BleServer::_advertise_with_retry()
    1. ble_gap_adv_stop()          — clear any stale NimBLE advertising state
    2. vTaskDelay(50ms)            — let NimBLE process the stop event
    3. ble_gap_adv_start() x50 retries (20ms interval) — start with EBUSY/EINVAL tolerance
    4. Log result (success or error code)
```

### Call Sites Changed

All four locations that currently call `ble_gap_adv_start` raw (no return check) → `self->_advertise_with_retry()`:

| Location | Event |
|----------|-------|
| `gap_event_cb` → `BLE_GAP_EVENT_DISCONNECT` | Normal disconnect |
| `gap_event_cb` → `BLE_GAP_EVENT_CONNECT` (failed) | Connection rejected |
| `gap_event_cb` → `BLE_GAP_EVENT_ADV_COMPLETE` | Advertising timed out |
| `BleServer::start_advertise()` (initial boot) | Current retry loop replaced |

### Why Not More Complex

- **No FreeRTOS timer needed**: NimBLE gap callbacks run in the NimBLE host task context. Calling `ble_gap_adv_start` synchronously is safe — no threading issue.
- **50 ms delay after stop is critical**: Without it, NimBLE has not yet processed the internal ADV_COMPLETE event, and the subsequent `ble_gap_adv_start` gets EBUSY.
- **Retry cap of 50 × 20ms = 1s**: EBUSY typically resolves within 100–200ms. 1 second is generous headroom.

### Non-Goals

- No changes to Python desktop-relay side (the existing reconnect_loop already works)
- No changes to BLE connection parameters or watchdog (those are already in place and working)
- No protocol-level changes

## Verification

1. Connect Python ↔ ESP32 normally → kill Python → verify ESP32 log shows "Advertising started" after disconnect
2. Phone BLE scanner confirms the device is visible after disconnect
3. Restart Python → connects successfully without hardware reset
4. Repeat 3 times to confirm reliability
