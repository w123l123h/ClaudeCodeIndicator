import asyncio
import logging
import sys
import os
import threading

# 确保工作目录为项目根 (desktop-relay 的父目录)
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from config import (
    KEEPALIVE_INTERVAL, KEEPALIVE_RESPONSE_TIMEOUT, KEEPALIVE_MAX_FAILURES,
    EVENT_LED_MAP, SAVED_RETRY_INTERVAL, SCAN_RETRY_INTERVAL,
)
from ble_client import BleClientManager
from http_server import HttpRelayServer
from pairing import show_pairing_dialog

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(name)s] %(levelname)s: %(message)s'
)
logger = logging.getLogger("main")


class DesktopRelay:
    def __init__(self):
        self.ble = BleClientManager()
        self.http = HttpRelayServer(self._on_http_message)
        self.alive_event = asyncio.Event()
        self.keepalive_failures = 0
        self._shutting_down = False

        # 配对状态管理
        self._pairing_in_progress = False
        self._pairing_address: str | None = None
        self._pairing_lock = asyncio.Lock()

        # 配对队列
        self._pairing_queue = []
        self._queue_lock = threading.Lock()
        
        # 已配对失败过的设备（用于去重）
        self._failed_devices = set()

        # 队列处理是否已启动（防重复触发）
        self._queue_processing_started = False

        self.ble.set_message_callback(self._on_ble_message)

    async def shutdown(self):
        """优雅退出：停止 HTTP，断开 BLE"""
        if self._shutting_down:
            return
        self._shutting_down = True
        logger.info("Shutdown initiated...")
        try:
            await self.http.stop()
        except Exception as e:
            logger.warning(f"HTTP stop error: {e}")
        await self.ble.cleanup()
        logger.info("Shutdown complete")

    async def _start_pairing(self, address: str) -> bool:
        """开始配对，设置配对状态。返回是否成功开始配对。"""
        async with self._pairing_lock:
            if self._pairing_in_progress:
                logger.warning(
                    f"Pairing already in progress with {self._pairing_address}"
                )
                return False
            self._pairing_in_progress = True
            self._pairing_address = address
            logger.info(f"Pairing started with {address}")
            return True

    async def _end_pairing(self):
        """结束配对，清除配对状态。"""
        async with self._pairing_lock:
            if self._pairing_in_progress:
                logger.info(f"Pairing ended with {self._pairing_address}")
            self._pairing_in_progress = False
            self._pairing_address = None

    async def _enqueue_device(self, address: str, name: str, rssi: int) -> None:
        """将设备添加到配对队列，包含去重检查"""
        # 检查是否已在队列中
        with self._queue_lock:
            for device in self._pairing_queue:
                if device['address'] == address:
                    logger.debug(f"Device {address} already in queue, skipping")
                    return
            self._pairing_queue.append({
                'address': address,
                'name': name,
                'rssi': rssi
            })
            queue_size = len(self._pairing_queue)
        logger.info(f"Device enqueued: {address} (RSSI={rssi}, name={name}), queue size: {queue_size}")

    def _dequeue_device(self) -> dict | None:
        """从队列取出下一个设备，返回设备信息字典，如果队列为空返回 None"""
        with self._queue_lock:
            if not self._pairing_queue:
                return None
            device = self._pairing_queue.pop(0)
        logger.info(f"Device dequeued: {device['address']}, remaining: {len(self._pairing_queue)}")
        return device

    def _dequeue_device_by_address(self, address: str) -> dict | None:
        """按地址精确出队，返回设备信息字典，未找到返回 None"""
        with self._queue_lock:
            for i, device in enumerate(self._pairing_queue):
                if device['address'] == address:
                    return self._pairing_queue.pop(i)
        return None

    def _clear_queue(self) -> None:
        """清空配对队列"""
        with self._queue_lock:
            count = len(self._pairing_queue)
            self._pairing_queue.clear()
        if count > 0:
            logger.info(f"Queue cleared, removed {count} devices")

    def _get_queue_size(self) -> int:
        """获取队列大小"""
        with self._queue_lock:
            return len(self._pairing_queue)

    def _on_ble_message(self, msg):
        logger.info(f"BLE notify: {msg}")
        if msg == "ALIVE":
            self.alive_event.set()

    def _translate_event(self, event: str) -> str | None:
        """将事件名翻译为 JSON LED 指令字符串"""
        import json
        cmd = EVENT_LED_MAP.get(event)
        if cmd is None:
            logger.warning(f"Unknown event: {event}")
            return None
        return json.dumps(cmd, separators=(',', ':'))

    async def _on_http_message(self, msg: str):
        """HTTP hook 消息处理：翻译事件 → JSON，然后通过 BLE 发送"""
        json_cmd = self._translate_event(msg)
        if json_cmd:
            await self.ble.send(json_cmd)
        else:
            # 如果不在 EVENT_LED_MAP 中，直接透传（用于未来扩展）
            await self.ble.send(msg)

    async def _pairing_flow(self, address, name):
        """首次配对流程（调用者需确保已调用 _start_pairing）"""
        saved = self.ble.get_saved_device()
        if saved:
            logger.info(f"Already paired with: {saved}")
            return True

        logger.info(f"Starting pairing flow with {address}, {name}")
        
        try:
            # 步骤1: 发送 PAIR_CONFIRM
            logger.debug(f"Sending PAIR_CONFIRM to {address}, {name}")
            await self.ble.send("PAIR_CONFIRM")
            await asyncio.sleep(0.5)

            # 步骤2: 弹出用户确认对话框
            logger.debug(f"Showing pairing dialog for {address}, {name}")
            loop = asyncio.get_event_loop()
            try:
                confirmed = await loop.run_in_executor(
                    None, show_pairing_dialog, address, name
                )
            except Exception as dialog_error:
                logger.error(f"Pairing dialog error: {dialog_error}", exc_info=True)
                raise

            # 步骤3: 根据用户选择处理
            if confirmed:
                self.ble.save_device(address)
                logger.debug(f"Sending PAIR_SUCCESS to {address}")
                try:
                    await self.ble.send("PAIR_SUCCESS")
                except Exception as send_error:
                    logger.error(f"Failed to send PAIR_SUCCESS: {send_error}", exc_info=True)
                    # 即使发送失败，设备已保存，下次可直连
                    logger.warning(f"Device saved but PAIR_SUCCESS send failed: {address}")
                
                logger.info(f"Pairing successful, saved: {address}")
                return True
            else:
                logger.info(f"Pairing cancelled by user for {address}")
                try:
                    await self.ble.disconnect()
                    logger.debug(f"Disconnected from {address} after user cancellation")
                except Exception as disconnect_error:
                    logger.warning(f"Disconnect error after cancellation: {disconnect_error}")
                return False
                
        except asyncio.TimeoutError as e:
            logger.error(f"Pairing flow timeout for {address}: {e}", exc_info=True)
            try:
                await self.ble.disconnect()
                logger.debug(f"Disconnected from {address} after timeout")
            except Exception as disconnect_error:
                logger.warning(f"Disconnect error after timeout: {disconnect_error}")
            return False
            
        except ConnectionError as e:
            logger.error(f"Pairing flow connection error for {address}: {e}", exc_info=True)
            try:
                await self.ble.disconnect()
                logger.debug(f"Disconnected from {address} after connection error")
            except Exception as disconnect_error:
                logger.warning(f"Disconnect error after connection error: {disconnect_error}")
            return False
            
        except Exception as e:
            logger.error(f"Pairing flow unexpected error for {address}: {e}", exc_info=True)
            try:
                await self.ble.disconnect()
                logger.debug(f"Disconnected from {address} after error")
            except Exception as disconnect_error:
                logger.warning(f"Disconnect error after exception: {disconnect_error}")
            return False

    async def _keepalive_task(self):
        logger.info("Keepalive task started")
        while True:
            if not self.ble.is_connected:
                logger.info("Keepalive skipped: not connected")
                await asyncio.sleep(KEEPALIVE_INTERVAL)
                continue

            try:
                logger.info("Sending KEEPALIVE heartbeat")
                self.alive_event.clear()
                await self.ble.send("KEEPALIVE")
                logger.info("KEEPALIVE heartbeat sent successfully")

                await asyncio.wait_for(
                    self.alive_event.wait(),
                    timeout=KEEPALIVE_RESPONSE_TIMEOUT
                )
                self.keepalive_failures = 0
                logger.info("Keepalive OK - response received")
            except asyncio.TimeoutError:
                self.keepalive_failures += 1
                logger.warning(f"Keepalive timeout ({self.keepalive_failures}/{KEEPALIVE_MAX_FAILURES})")

                if self.keepalive_failures >= KEEPALIVE_MAX_FAILURES:
                    logger.error("Keepalive failed — forcing reconnect")
                    await self.ble.disconnect()
                    self.keepalive_failures = 0
            except Exception as e:
                # BLE 写入异常（设备断开等），不崩溃，等 reconnect_loop 恢复
                logger.warning(f"Keepalive send error: {e}")
                self.keepalive_failures = 0
            
            # 发送心跳后等待指定间隔再发送下一次
            await asyncio.sleep(KEEPALIVE_INTERVAL)

    async def run(self):
        try:
            # 先启动 HTTP，确保 hook 消息不会丢失
            await self.http.start()

            saved = self.ble.get_saved_device()

            if saved:
                # 阶段1: 已保存设备，只直连不扫描
                logger.info(f"Saved device: {saved}, connecting...")
                while True:
                    if await self.ble.connect_and_verify(saved):
                        # 连接成功后发送成功消息
                        try:
                            await self.ble.send("PAIR_SUCCESS")
                            logger.info("Sent PAIR_SUCCESS to saved device")
                        except Exception as send_error:
                            logger.warning(f"Failed to send PAIR_SUCCESS: {send_error}")
                        break
                    logger.warning(
                        f"Failed to connect saved device, "
                        f"retrying in {SAVED_RETRY_INTERVAL}s..."
                    )
                    await asyncio.sleep(SAVED_RETRY_INTERVAL)
            else:
                # 阶段2: 无已保存设备，阻塞式扫描+配对
                paired = False
                while not paired:
                    try:
                        paired_event = asyncio.Event()
                        paired_address = None

                        async def _on_device_found(address, name, rssi):
                            nonlocal paired_address
                            
                            # 标准化地址（大写）用于比较
                            address = address.upper()
                            
                            # 如果已经配对成功，跳过所有后续设备
                            if paired_event.is_set():
                                logger.debug(f"Skipping {address}: already paired")
                                return
                            
                            # 检查设备是否正在配对中
                            async with self._pairing_lock:
                                if self._pairing_address and self._pairing_address.upper() == address:
                                    logger.debug(f"Skipping {address}: device is being paired")
                                    return
                            
                            # 检查设备是否已配对失败过
                            if address in self._failed_devices:
                                logger.debug(f"Skipping {address}: device has been paired and failed before")
                                return
                            
                            # 将设备添加到配对队列（包含去重检查）
                            await self._enqueue_device(address, name, rssi)
                            
                            # 如果没有设备在配对中，从队列取出设备开始配对
                            if self._pairing_in_progress:
                                logger.debug(
                                    f"Pairing in progress, {address} enqueued, "
                                    f"queue size: {self._get_queue_size()}"
                                )
                                return
                            
                            # 从队列取出设备开始配对
                            device = self._dequeue_device()
                            if device is None:
                                logger.debug("Queue empty after dequeue, no device to pair")
                                return
                            
                            address = device['address']
                            name = device['name']
                            
                            # 设置配对状态
                            if not await self._start_pairing(address):
                                logger.info(
                                    f"Skipping {address}: pairing already in progress"
                                )
                                return
                            
                            # 注意：不要在这里 stop_scan()！
                            # 否则 scan_devices() 会提前返回，paired_event 还未设置，
                            # 外层代码会误认为配对失败并启动新一轮扫描。
                            # stop_scan() 只在配对成功后调用（见下方 success 分支）。

                            logger.info(f"Starting pairing attempt 1 with {address}")
                            
                            # 配对循环：失败后继续处理队列中的下一个设备
                            attempt = 1
                            while True:
                                success = False
                                try:
                                    # 尝试连接和验证设备
                                    logger.debug(f"Connecting to {address}, {name}...")
                                    if await self.ble.connect_and_verify(address):
                                        logger.debug(f"Connected to {address}, {name}, starting pairing flow")
                                        if await self._pairing_flow(address, name):
                                            success = True
                                            paired_address = address
                                            paired_event.set()
                                            self._clear_queue()
                                            # 配对成功后停止扫描
                                            self.ble.stop_scan()
                                            logger.info(f"Pairing completed successfully with {address}")
                                        else:
                                            logger.info(f"Pairing flow returned False for {address}")
                                    else:
                                        logger.warning(f"Failed to connect/verify {address}")
                                        
                                except asyncio.TimeoutError as e:
                                    logger.error(f"Timeout during pairing with {address}: {e}", exc_info=True)
                                except ConnectionError as e:
                                    logger.error(f"Connection error during pairing with {address}: {e}", exc_info=True)
                                except Exception as e:
                                    logger.error(f"Unexpected error during pairing with {address}: {e}", exc_info=True)
                                finally:
                                    # 配对流程结束，清除状态
                                    logger.debug(f"Cleaning up pairing state for {address}")
                                    await self._end_pairing()
                                    # 仅在配对失败时断开连接；成功时保持连接以供 keepalive 使用
                                    if not success:
                                        try:
                                            if self.ble.is_connected:
                                                await self.ble.disconnect()
                                                logger.debug(f"Disconnected from {address}")
                                        except Exception as disconnect_error:
                                            logger.warning(f"Disconnect error: {disconnect_error}")
                                
                                if success:
                                    break
                                
                                # 配对失败，记录设备到失败列表
                                self._failed_devices.add(address)
                                logger.info(f"Pairing failed with {address}, checking queue...")
                                
                                # 配对失败，检查队列是否有下一个设备
                                device = self._dequeue_device()
                                if device is None:
                                    logger.info("Queue empty after pairing failure, will restart scan")
                                    break
                                
                                attempt += 1
                                logger.info(f"Continuing with next device from queue")
                                
                                address = device['address']
                                name = device['name']
                                rssi = device['rssi']
                                
                                # 设置新设备的配对状态
                                if not await self._start_pairing(address):
                                    logger.info(f"Skipping {address}: pairing already in progress")
                                    continue
                                
                                logger.info(
                                    f"Retrying with next device: {address} "
                                    f"(RSSI={rssi}, name={name})"
                                )

                        await self.ble.scan_devices(detection_callback=_on_device_found)

                        if paired_event.is_set():
                            paired = True
                        else:
                            logger.warning(
                                f"No devices found or paired, "
                                f"retrying in {SCAN_RETRY_INTERVAL}s..."
                            )
                            # 清空队列和失败设备列表，重新开始
                            self._clear_queue()
                            self._failed_devices.clear()
                            await asyncio.sleep(SCAN_RETRY_INTERVAL)
                    except Exception as e:
                        logger.error(
                            f"Scan/connect error: {e}, "
                            f"retrying in {SCAN_RETRY_INTERVAL}s..."
                        )
                        await asyncio.sleep(SCAN_RETRY_INTERVAL)

            # 阶段3: 保活 + 重连
            await asyncio.gather(
                self._keepalive_task(),
                self.ble.reconnect_loop(),
            )
        finally:
            await self.shutdown()


if __name__ == "__main__":
    relay = DesktopRelay()
    try:
        asyncio.run(relay.run())
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
