import asyncio
import logging
import sys
import os

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

    async def _pairing_flow(self, address):
        """首次配对流程"""
        saved = self.ble.get_saved_device()
        if saved:
            logger.info(f"Already paired with: {saved}")
            return True

        # 连接后发送 PAIR_CONFIRM
        await self.ble.send("PAIR_CONFIRM")
        await asyncio.sleep(0.5)

        # 弹出对话框
        loop = asyncio.get_event_loop()
        confirmed = await loop.run_in_executor(
            None, show_pairing_dialog, address
        )

        if confirmed:
            self.ble.save_device(address)
            await self.ble.send("PAIR_SUCCESS")
            logger.info(f"Pairing successful, saved: {address}")
            return True
        else:
            await self.ble.disconnect()
            logger.info("Pairing cancelled by user")
            return False

    async def _keepalive_task(self):
        while True:
            await asyncio.sleep(KEEPALIVE_INTERVAL)
            if not self.ble.is_connected:
                continue

            try:
                self.alive_event.clear()
                await self.ble.send("KEEPALIVE")

                await asyncio.wait_for(
                    self.alive_event.wait(),
                    timeout=KEEPALIVE_RESPONSE_TIMEOUT
                )
                self.keepalive_failures = 0
                logger.debug("Keepalive OK")
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
                        break
                    logger.warning(
                        f"Failed to connect saved device, "
                        f"retrying in {SAVED_RETRY_INTERVAL}s..."
                    )
                    await asyncio.sleep(SAVED_RETRY_INTERVAL)
            else:
                # 阶段2: 无已保存设备，扫描+逐个尝试配对
                paired = False
                while not paired:
                    devices = await self.ble.scan_devices()
                    if not devices:
                        logger.warning(
                            f"No devices found, "
                            f"retrying in {SCAN_RETRY_INTERVAL}s..."
                        )
                        await asyncio.sleep(SCAN_RETRY_INTERVAL)
                        continue

                    for address, name, rssi in devices:
                        logger.info(
                            f"Trying: {address} (RSSI={rssi}, name={name})"
                        )
                        if not await self.ble.connect_and_verify(address):
                            continue
                        if await self._pairing_flow(address):
                            paired = True
                            break

                    if not paired:
                        logger.warning(
                            f"All devices exhausted, "
                            f"rescanning in {SCAN_RETRY_INTERVAL}s..."
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
