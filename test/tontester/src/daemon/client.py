"""Test-harness side of the daemon IPC protocol defined in :mod:`daemon.ipc`."""

import asyncio
import logging
from pathlib import Path
from typing import final

import httpx
from pydantic import TypeAdapter, ValidationError
from websockets.asyncio.client import ClientConnection, unix_connect
from websockets.exceptions import ConnectionClosed

from .ipc import (
    CompleteMessage,
    HeartbeatMessage,
    RegisterError,
    RegisterMessage,
    RegisterOk,
)
from .storage import TestMetadata

logger = logging.getLogger(__name__)


DEFAULT_HEARTBEAT_INTERVAL_SECONDS = 10.0
DEFAULT_INITIAL_BACKOFF_SECONDS = 1.0
DEFAULT_MAX_BACKOFF_SECONDS = 30.0
# Cap on how many consecutive "run_already_active" responses we'll tolerate
# during reconnect before giving up. Tuned for the brief server-side window
# between WS close and ``runs.release`` firing — not for a real collision
# with another harness.
DEFAULT_RUN_ALREADY_ACTIVE_RETRIES = 5


class RegisterFailed(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(f"Daemon refused run registration ({code}): {message}")
        self.code: str = code
        self.detail: str = message


type _Response = RegisterOk | RegisterError


_response_adapter: TypeAdapter[_Response] = TypeAdapter(_Response)


@final
class DashboardClient:
    def __init__(
        self,
        socket_path: Path,
        *,
        heartbeat_interval_seconds: float = DEFAULT_HEARTBEAT_INTERVAL_SECONDS,
        initial_backoff_seconds: float = DEFAULT_INITIAL_BACKOFF_SECONDS,
        max_backoff_seconds: float = DEFAULT_MAX_BACKOFF_SECONDS,
        run_already_active_retries: int = DEFAULT_RUN_ALREADY_ACTIVE_RETRIES,
    ):
        self.socket_path = socket_path
        self._heartbeat_interval = heartbeat_interval_seconds
        self._initial_backoff = initial_backoff_seconds
        self._max_backoff = max_backoff_seconds
        self._run_already_active_retries = run_already_active_retries
        self._run_id: str | None = None
        self._metadata: TestMetadata | None = None
        self._ws: ClientConnection | None = None
        self._supervisor: asyncio.Task[None] | None = None
        self._closed = asyncio.Event()

    async def register(self, run_id: str, metadata: TestMetadata) -> RegisterOk:
        """Open the WS, send the register handshake, spawn the supervisor."""
        if self._supervisor is not None:
            raise RuntimeError("DashboardClient.register called twice")

        self._run_id = run_id
        self._metadata = metadata

        ws, response = await self._open_and_register(run_id, metadata)
        self._ws = ws
        self._supervisor = asyncio.create_task(
            self._supervise(run_id, metadata), name=f"dashboard-supervisor-{run_id}"
        )
        return response

    async def aclose(self) -> None:
        self._closed.set()
        if self._supervisor is not None:
            _ = self._supervisor.cancel()
            try:
                await self._supervisor
            except asyncio.CancelledError:
                pass
            self._supervisor = None
        if self._ws is not None:
            try:
                await self._ws.send(CompleteMessage(action="complete").model_dump_json())
            except Exception:
                pass
            try:
                await self._ws.close()
            except Exception:
                pass
            self._ws = None

    async def _supervise(self, run_id: str, metadata: TestMetadata) -> None:
        """Hold the WS open. On failure, reconnect and re-register."""
        backoff = self._initial_backoff
        already_active_attempts = 0
        while not self._closed.is_set():
            ws = self._ws
            if ws is None:
                try:
                    ws, _ = await self._open_and_register(run_id, metadata)
                except RegisterFailed as e:
                    if (
                        e.code == "run_already_active"
                        and already_active_attempts < self._run_already_active_retries
                    ):
                        # Transient: the daemon's ``runs.release`` for our
                        # prior WS hasn't fired yet. Back off and retry.
                        already_active_attempts += 1
                        logger.info(
                            (
                                f"Reconnect saw stale ``run_already_active``; "
                                f"retry {already_active_attempts} in {backoff:.1f}s"
                            )
                        )
                    else:
                        logger.error(f"Dashboard daemon refused reconnect: {e}")
                        return
                    try:
                        _ = await asyncio.wait_for(self._closed.wait(), timeout=backoff)
                        return
                    except asyncio.TimeoutError:
                        pass
                    backoff = min(backoff * 2, self._max_backoff)
                    continue
                except Exception:
                    logger.warning(
                        f"Dashboard reconnect failed; retrying in {backoff:.1f}s",
                        exc_info=True,
                    )
                    try:
                        _ = await asyncio.wait_for(self._closed.wait(), timeout=backoff)
                        return
                    except asyncio.TimeoutError:
                        pass
                    backoff = min(backoff * 2, self._max_backoff)
                    continue
                self._ws = ws
                logger.info(f"Dashboard daemon reconnected for run {run_id}")
                backoff = self._initial_backoff
                already_active_attempts = 0

            try:
                await self._heartbeat(ws)
            except asyncio.CancelledError:
                raise
            except Exception:
                logger.warning(
                    f"Dashboard WS dropped; reconnecting in {backoff:.1f}s",
                    exc_info=True,
                )
                try:
                    await ws.close()
                except Exception:
                    pass
                self._ws = None
                try:
                    _ = await asyncio.wait_for(self._closed.wait(), timeout=backoff)
                    return
                except asyncio.TimeoutError:
                    pass
                backoff = min(backoff * 2, self._max_backoff)

    async def _heartbeat(self, ws: ClientConnection) -> None:
        while not self._closed.is_set():
            await asyncio.sleep(self._heartbeat_interval)
            await ws.send(HeartbeatMessage(action="heartbeat").model_dump_json())

    async def _open_and_register(
        self, run_id: str, metadata: TestMetadata
    ) -> tuple[ClientConnection, RegisterOk]:
        ws = await unix_connect(
            path=str(self.socket_path),
            uri=f"ws://daemon/runs/{run_id}",
        )
        try:
            hello = RegisterMessage(action="register", metadata=metadata)
            await ws.send(hello.model_dump_json())
            raw = await ws.recv()
        except ConnectionClosed, OSError:
            try:
                await ws.close()
            except Exception:
                pass
            raise

        if isinstance(raw, bytes):
            raw = raw.decode("utf-8")

        try:
            response = _response_adapter.validate_json(raw)
        except ValidationError as e:
            await ws.close()
            raise RuntimeError(f"Unexpected daemon response: {raw[:200]}") from e

        if isinstance(response, RegisterError):
            await ws.close()
            raise RegisterFailed(response.code, response.message)

        return ws, response


async def probe_daemon(socket_path: Path, timeout: float = 2.0) -> bool:
    """Check whether a daemon is responding on the given UDS socket."""
    transport = httpx.AsyncHTTPTransport(uds=str(socket_path))
    async with httpx.AsyncClient(
        transport=transport, base_url="http://daemon", timeout=timeout
    ) as client:
        try:
            response = await client.get("/health")
            return response.status_code == 200
        except httpx.HTTPError:
            return False
