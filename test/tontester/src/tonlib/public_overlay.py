import traceback
from typing import Self, final

from tonapi import ton_api

from .errors import LocalError
from .event_loop import TonlibEventLoop
from .tonlib_cdll import TonlibCDLL


@final
class PublicOverlayClient:
    def __init__(
        self,
        tonlib: TonlibCDLL,
        event_loop: TonlibEventLoop,
        config: ton_api.PublicOverlayClient_config,
    ):
        self._tonlib = tonlib
        self._event_loop = event_loop
        config_json = config.to_json().encode()
        self._client = tonlib.public_overlay_client_init(event_loop.loop, config_json)

    def __del__(self):
        assert self._client == 0, (
            "PublicOverlayClient not destroyed. Call 'aclose' before destroying the object."
        )

    async def aclose(self) -> None:
        if self._client == 0:
            return

        self._tonlib.public_overlay_client_destroy(self._client)
        self._client = 0

    async def __aenter__(self) -> Self:
        if not self._tonlib.public_overlay_client_await_ready(self._client):
            continuation_id, future = self._event_loop.create_awaitable_future()
            if continuation_id is not None:
                self._tonlib.public_overlay_client_await_suspend(self._client, continuation_id)
                try:
                    await future
                except BaseException:
                    self._tonlib.public_overlay_client_destroy(self._client)
                    self._client = 0

        if self._tonlib.public_overlay_client_is_error(self._client):
            error_code = self._tonlib.public_overlay_client_get_error_code(self._client)
            error_message = self._tonlib.public_overlay_client_get_error_message(
                self._client
            ).decode()
            self._tonlib.public_overlay_client_destroy(self._client)
            self._client = 0
            raise LocalError(error_code, error_message)

        return self

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc_val: BaseException | None,
        exc_tb: traceback.TracebackException | None,
    ) -> None:
        await self.aclose()
