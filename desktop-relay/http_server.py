import json
import logging

from aiohttp import web

from config import HTTP_HOST, HTTP_PORT

logger = logging.getLogger(__name__)

VALID_MESSAGES = {"WORKING", "WAITING_USER", "COMPLETED", "ERROR"}


class HttpRelayServer:
    def __init__(self, ble_send_callback):
        self._ble_send = ble_send_callback
        self._runner = None

    async def start(self):
        app = web.Application()
        app.router.add_post("/hook", self._handle_hook)
        self._runner = web.AppRunner(app)
        await self._runner.setup()
        site = web.TCPSite(self._runner, HTTP_HOST, HTTP_PORT)
        await site.start()
        logger.info(f"HTTP server listening on http://{HTTP_HOST}:{HTTP_PORT}")

    async def _handle_hook(self, request):
        try:
            body = await request.json()
        except Exception:
            logger.warning("Invalid JSON in hook request")
            return web.json_response({"status": "error", "message": "Bad JSON"}, status=400)

        hook_event = body.get("hook_event_name", "")
        logger.info(f"HTTP hook: {hook_event}")

        msg = self._event_to_message(hook_event, body)
        if msg and msg in VALID_MESSAGES:
            await self._ble_send(msg)
        else:
            logger.debug(f"Ignored hook event: {hook_event}")

        return web.json_response({"status": "ok"})

    def _event_to_message(self, hook_event, body):
        if hook_event == "PreToolUse":
            # AskUserQuestion = Claude 在等用户回答 → 闪烁
            if body.get("tool_name") == "AskUserQuestion":
                return "WAITING_USER"
            return "WORKING"
        elif hook_event == "UserPromptExpansion":
            return "WORKING"
        elif hook_event in ("PermissionRequest",):
            # 权限弹窗 = 需要用户介入 → 闪烁
            return "WAITING_USER"
        elif hook_event in ("PostToolUse", "PostToolBatch"):
            # 工具执行完 → 切回工作中
            return "WORKING"
        elif hook_event == "Stop":
            exit_code = body.get("exit_code", 0)
            return "COMPLETED" if exit_code == 0 else "ERROR"
        return None

    async def stop(self):
        if self._runner:
            await self._runner.cleanup()
