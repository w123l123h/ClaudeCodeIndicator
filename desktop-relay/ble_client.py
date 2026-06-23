import asyncio
import logging
from bleak import BleakScanner, BleakClient

from config import (
    BLE_SERVICE_UUID, BLE_CHAR_UUID, BLE_SCAN_TIMEOUT,
    DEVICE_CONFIG_FILE,
    RECONNECT_BASE_DELAY,
)

logger = logging.getLogger(__name__)


class BleClientManager:
    def __init__(self):
        self._client: BleakClient | None = None
        self._char = None
        self._connected = False
        self._message_callback = None
        self._target_device = None
        self._scanner = None
        self._scan_stop_event = None
        self._discovered_devices = set()

    def set_message_callback(self, cb):
        self._message_callback = cb

    @property
    def is_connected(self):
        return self._connected and self._client and self._client.is_connected

    @property
    def address(self):
        return self._client.address if self._client else None

    def get_saved_device(self):
        import os, json
        if os.path.exists(DEVICE_CONFIG_FILE):
            with open(DEVICE_CONFIG_FILE, 'r') as f:
                data = json.load(f)
                return data.get('device_name')
        return None

    def save_device(self, name):
        import os, json
        with open(DEVICE_CONFIG_FILE, 'w') as f:
            json.dump({'device_name': name}, f)

    async def scan_devices(self, detection_callback=None):
        """扫描 BLE 设备，返回按 RSSI 降序的列表 [(addr, name, rssi), ...]
        
        Args:
            detection_callback: 可选回调函数，当发现设备时立即调用，参数为 (addr, name, rssi)
        """
        logger.info("Scanning for devices...")
        self._discovered_devices = set()
        self._scan_stop_event = asyncio.Event()
        
        def _on_detected(device, adv):
            if self._scan_stop_event.is_set():
                return
            
            addr = device.address
            if addr in self._discovered_devices:
                return
            self._discovered_devices.add(addr)
            
            name = adv.local_name or "(no name)"
            rssi = adv.rssi if adv.rssi is not None else -999
            logger.info(f"  Found: {addr}  RSSI={rssi:>4}  name={name}")
            
            if detection_callback:
                asyncio.create_task(detection_callback(addr, name, rssi))

        self._scanner = BleakScanner(detection_callback=_on_detected, return_adv=True)
        scanner = self._scanner
        
        try:
            await scanner.start()
            await asyncio.wait_for(self._scan_stop_event.wait(), timeout=BLE_SCAN_TIMEOUT)
        except asyncio.TimeoutError:
            pass
        finally:
            await scanner.stop()

        dev_list = []
        discovered = scanner.discovered_devices
        if isinstance(discovered, dict):
            for addr, (device, adv) in discovered.items():
                name = adv.local_name or "(no name)"
                rssi = adv.rssi if adv.rssi is not None else -999
                dev_list.append((addr, name, rssi))
        elif isinstance(discovered, list):
            for device in discovered:
                addr = device.address
                name = getattr(device, 'name', None) or getattr(device, 'local_name', None) or "(no name)"
                rssi = getattr(device, 'rssi', None)
                if rssi is None:
                    rssi = -999
                dev_list.append((addr, name, rssi))

        dev_list.sort(key=lambda x: x[2], reverse=True)

        for d in dev_list:
            if d[1] == "ClaudeCodeIndicator":
                dev_list.remove(d)
                dev_list.insert(0, d)
        return dev_list

    def stop_scan(self):
        """停止正在进行的扫描"""
        if self._scan_stop_event:
            self._scan_stop_event.set()
            logger.info("Scan stopped")

    async def connect_and_verify(self, address):
        """连接并检查是否有目标 Service"""
        # 清理旧连接
        if self._client:
            try:
                await self._client.disconnect()
            except Exception:
                pass
            self._client = None
            self._char = None
            self._connected = False

        try:
            client = BleakClient(address, timeout=10.0, disconnected_callback=self._on_disconnect)
            await client.connect()
            logger.info(f"Connected to {address}, checking services...")

            # 查找目标 service/characteristic
            found_char = None
            for service in client.services:
                if service.uuid == BLE_SERVICE_UUID:
                    for char in service.characteristics:
                        if char.uuid == BLE_CHAR_UUID:
                            found_char = char
                            break

            if found_char:
                self._client = client
                self._char = found_char
                self._connected = True
                await self._client.start_notify(found_char.uuid, self._on_notify)
                logger.info(f"Target device confirmed: {address}")
                return True
            else:
                logger.info(f"Not our device: {address}")
                await client.disconnect()
                return False

        except Exception as e:
            logger.warning(f"Failed to connect {address}: {e}")
            return False

    def _on_disconnect(self, client):
        logger.warning("BLE disconnected")
        self._connected = False
        self._client = None
        self._char = None

    def _on_notify(self, sender, data):
        msg = data.decode().strip()
        logger.info(f"Notify: {msg}")
        if self._message_callback:
            self._message_callback(msg)

    async def send(self, message):
        if not self._client or not self._char:
            logger.warning(f"BLE not connected — dropping message: {message}")
            return
        try:
            data = (message + '\n').encode()
            await self._client.write_gatt_char(self._char.uuid, data)
            logger.info(f"Sent: {message}")
        except Exception as e:
            logger.warning(f"Send failed ({message}): {e}")
            self._connected = False

    async def disconnect(self):
        if self._client:
            await self._client.disconnect()
            self._connected = False

    async def cleanup(self):
        """优雅退出：断开 BLE 并清理所有资源"""
        logger.info("Cleaning up BLE...")
        try:
            await self.disconnect()
        except Exception as e:
            logger.warning(f"Disconnect during cleanup failed: {e}")
        self._client = None
        self._char = None
        self._connected = False
        self._target_device = None
        logger.info("BLE cleanup complete")

    async def reconnect_loop(self):
        while True:
            try:
                if not self.is_connected:
                    saved = self.get_saved_device()
                    if not saved:
                        logger.warning("No saved device, skipping reconnect")
                        await asyncio.sleep(5)
                        continue

                    logger.info(f"Reconnecting to saved device: {saved}")
                    success = await self.connect_and_verify(saved)
                    if success:
                        logger.info("Reconnected successfully")
                        # 发送成功消息，通知设备连接成功
                        try:
                            await self.send("PAIR_SUCCESS")
                            logger.info("Sent PAIR_SUCCESS to reconnected device")
                        except Exception as send_error:
                            logger.warning(f"Failed to send PAIR_SUCCESS: {send_error}")
                            # 消息发送失败不影响重连状态
                    else:
                        logger.warning(f"Reconnect failed, retrying in {RECONNECT_BASE_DELAY}s...")
                        await asyncio.sleep(RECONNECT_BASE_DELAY)
                else:
                    # 已连接时让出事件循环，避免忙循环饿死 keepalive 等任务
                    await asyncio.sleep(1)
            except Exception as e:
                logger.error(f"Reconnect loop error: {e}")
                await asyncio.sleep(1)
