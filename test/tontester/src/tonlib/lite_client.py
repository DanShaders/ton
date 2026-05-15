import json
import traceback
from typing import cast, final

from tonapi import lite_api, ton_api

from tl import JSONSerializable, TLRequest

from .errors import LocalError, RemoteError
from .event_loop import TonlibEventLoop
from .tonlib_cdll import TonlibCDLL


@final
class LiteClient:
    def __init__(
        self,
        tonlib: TonlibCDLL,
        event_loop: TonlibEventLoop,
        config: ton_api.LiteClient_config,
    ):
        self._tonlib = tonlib
        self._event_loop = event_loop
        config_json = config.to_json().encode()
        self._client = tonlib.lite_client_create(event_loop.loop, config_json)

        if tonlib.lite_client_is_error(self._client):
            error_code = tonlib.lite_client_get_error_code(self._client)
            error_message = tonlib.lite_client_get_error_message(self._client).decode()
            tonlib.lite_client_destroy(self._client)
            self._client = 0
            raise LocalError(error_code, error_message)

    def __del__(self):
        assert self._client == 0, (
            "LiteClient not destroyed. Call 'close' before destroying the object."
        )

    def close(self) -> None:
        if self._client == 0:
            return

        self._tonlib.lite_client_destroy(self._client)
        self._client = 0

    def __enter__(self):
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_val: BaseException | None,
        exc_tb: traceback.TracebackException | None,
    ):
        self.close()

    async def request(self, request: TLRequest) -> JSONSerializable:
        response = self._tonlib.lite_client_request(self._client, request.to_json().encode())

        try:
            if not self._tonlib.response_await_ready(response):
                continuation_id, future = self._event_loop.create_awaitable_future()
                if continuation_id is not None:
                    self._tonlib.response_await_suspend(response, continuation_id)
                await future

            if self._tonlib.response_is_error(response):
                error_code = self._tonlib.response_get_error_code(response)
                error_message = self._tonlib.response_get_error_message(response).decode()
                raise LocalError(error_code, error_message)

            response_json = self._tonlib.response_get_response(response).decode()
            response_json = cast(JSONSerializable, json.loads(response_json))

            if (
                isinstance(response_json, dict)
                and response_json.get("@type", None) == "liteServer.error"
            ):
                error = lite_api.LiteServer_error.from_dict(response_json)
                raise RemoteError(error.code, error.message)

            return response_json
        finally:
            self._tonlib.response_destroy(response)
