import asyncio
import logging

from config import TCP_HOST, TCP_PORT

logger = logging.getLogger(__name__)

VALID_MESSAGES = {"WORKING", "WAITING_USER", "COMPLETED", "ERROR"}


class TcpRelayServer:
    def __init__(self, ble_send_callback):
        self._ble_send = ble_send_callback
        self._server = None

    async def start(self):
        self._server = await asyncio.start_server(
            self._handle_client, TCP_HOST, TCP_PORT
        )
        logger.info(f"TCP server listening on {TCP_HOST}:{TCP_PORT}")

    async def _handle_client(self, reader, writer):
        try:
            data = await asyncio.wait_for(reader.read(1024), timeout=5.0)
            msg = data.decode().strip()
            peer = writer.get_extra_info('peername')
            logger.info(f"TCP from {peer}: {msg}")

            if msg in VALID_MESSAGES:
                await self._ble_send(msg)
            else:
                logger.warning(f"Unknown TCP message: {msg}")
        except asyncio.TimeoutError:
            pass
        except Exception as e:
            logger.error(f"TCP error: {e}")
        finally:
            writer.close()
            await writer.wait_closed()

    async def stop(self):
        if self._server:
            self._server.close()
            await self._server.wait_closed()
