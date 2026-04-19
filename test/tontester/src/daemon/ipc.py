"""WebSocket entry point for test-harness <-> daemon IPC.

Protocol (endpoint ``/runs/{run_id}``):

1. Client opens the WebSocket.
2. Client sends the *handshake* message (``action="register"``) carrying
   :class:`TestMetadata`.
3. Daemon responds with Grafana / Prometheus URLs.
4. Client sends ``heartbeat`` every ~10 s until the run ends.
5. Client closes the socket (clean) — or daemon times out the heartbeat
   and closes with code 4000.

Either way, the run is archived when the WebSocket goes away.

Cancel zone
-----------

Handlers in this module run inside uvicorn, which cancels in-flight
handlers when it is stopping (see ``_uvicorn_serving`` in ``daemon.py``).
Every ``await`` here may therefore observe ``CancelledError``.

This is safe *for state* because all mutation crosses into the actor
system via :mod:`.runs`. The call path is ``handler → RunsSupervisor →
Queue.put → RunActor``; the ``Queue.put`` hop is atomic (unbounded queue,
no yielding), so once a message is enqueued, the actor processes it
regardless of the handler's fate. Cancelling a handler can at worst
drop the *reply* the handler was waiting for — the state transition
still runs. If cancellation hits *before* the message is enqueued (e.g.
between ``register()`` returning and ``release()`` being awaited), the
run stays LIVE in actor memory until ``supervisor.shutdown()`` issues
``_Shutdown``, which tears it down via the same ``_release_live`` path.
Bounded, not leaked.
"""

import asyncio
import logging
from collections.abc import AsyncGenerator
from contextlib import asynccontextmanager
from typing import Annotated, Literal, final

from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from pydantic import BaseModel, Field, TypeAdapter, ValidationError

from .models import TestMetadata
from .runs import DaemonStopped, RunAlreadyActive, RunsSupervisor, RunStartFailed

logger = logging.getLogger(__name__)


DEFAULT_HEARTBEAT_TIMEOUT_SECONDS = 30.0


class RegisterMessage(BaseModel):
    action: Literal["register"]
    metadata: TestMetadata


class HeartbeatMessage(BaseModel):
    action: Literal["heartbeat"]


class CompleteMessage(BaseModel):
    action: Literal["complete"]


type ClientMessage = Annotated[
    RegisterMessage | HeartbeatMessage | CompleteMessage,
    Field(discriminator="action"),
]

_client_message_adapter: TypeAdapter[ClientMessage] = TypeAdapter(ClientMessage)


class RegisterOk(BaseModel):
    status: Literal["ok"] = "ok"
    run_id: str
    dashboard_url: str
    grafana_url: str
    prometheus_url: str


class RegisterError(BaseModel):
    status: Literal["error"] = "error"
    code: str
    message: str


@final
class RunUrlResolver:
    """Computes the externally-visible URLs for a given run."""

    def __init__(self, dashboard_url: str, grafana_url: str):
        self.dashboard_url = dashboard_url
        self.grafana_url = grafana_url

    def for_run(self, run_id: str) -> RegisterOk:
        grafana = f"{self.grafana_url}/d/ton-overview?refresh=5s&var-datasource={run_id}"
        # Grafana proxies the datasource, but expose the direct proxy URL too
        # so users can hit Prometheus's raw UI if they need ad-hoc queries.
        prom = f"{self.dashboard_url}/runs/{run_id}/prom/"
        return RegisterOk(
            run_id=run_id,
            dashboard_url=self.dashboard_url,
            grafana_url=grafana,
            prometheus_url=prom,
        )


def build_ws_router(
    runs: RunsSupervisor,
    urls: RunUrlResolver,
    *,
    heartbeat_timeout_seconds: float = DEFAULT_HEARTBEAT_TIMEOUT_SECONDS,
) -> APIRouter:
    router = APIRouter()

    @router.websocket("/runs/{run_id}")
    async def _(ws: WebSocket, run_id: str) -> None:
        await _handle_run_ws(runs, urls, ws, run_id, heartbeat_timeout_seconds)

    return router


@asynccontextmanager
async def _run_registered(
    runs: RunsSupervisor, run_id: str, metadata: TestMetadata
) -> AsyncGenerator[None]:
    """Own a run's registration for the duration of this scope.

    Entering the scope registers the run (or raises
    :class:`RunAlreadyActive` / :class:`RunStartFailed` / :class:`DaemonStopped`
    to the caller). Exiting releases it best-effort — errors are logged
    and swallowed so the WebSocket teardown path cannot hang.
    """
    _ = await runs.register(run_id, metadata)
    try:
        yield
    finally:
        try:
            await runs.release(run_id)
        except Exception:
            logger.exception(f"Error finalizing run {run_id}")


async def _handle_run_ws(
    runs: RunsSupervisor,
    urls: RunUrlResolver,
    ws: WebSocket,
    run_id: str,
    heartbeat_timeout_seconds: float,
) -> None:
    await ws.accept()
    try:
        hello_raw = await asyncio.wait_for(ws.receive_text(), timeout=heartbeat_timeout_seconds)
        hello = _parse(hello_raw)
        if not isinstance(hello, RegisterMessage):
            await ws.send_text(
                RegisterError(
                    code="bad_handshake",
                    message="first message must be action=register",
                ).model_dump_json()
            )
            await ws.close(code=4001)
            return

        try:
            async with _run_registered(runs, run_id, hello.metadata):
                await ws.send_text(urls.for_run(run_id).model_dump_json())
                await _run_heartbeat_loop(ws, run_id, heartbeat_timeout_seconds)
        except RunAlreadyActive:
            await ws.send_text(
                RegisterError(
                    code="run_already_active",
                    message=f"run {run_id} is already active",
                ).model_dump_json()
            )
            await ws.close(code=4002)
        except RunStartFailed as e:
            await ws.send_text(RegisterError(code="start_failed", message=str(e)).model_dump_json())
            await ws.close(code=4003)
        except DaemonStopped:
            await ws.send_text(
                RegisterError(
                    code="daemon_stopping",
                    message="daemon is shutting down",
                ).model_dump_json()
            )
            await ws.close(code=4004)
    except WebSocketDisconnect:
        pass
    except Exception:
        logger.exception(f"Error on run WebSocket for {run_id}")


async def _run_heartbeat_loop(ws: WebSocket, run_id: str, heartbeat_timeout_seconds: float) -> None:
    while True:
        try:
            raw = await asyncio.wait_for(ws.receive_text(), timeout=heartbeat_timeout_seconds)
        except asyncio.TimeoutError:
            logger.warning(f"Heartbeat timeout for run {run_id}")
            await ws.close(code=4000)
            return
        msg = _parse(raw)
        match msg:
            case None:
                # Malformed frame would otherwise defeat the heartbeat timeout.
                await ws.close(code=4001)
                return
            case HeartbeatMessage():
                continue
            case CompleteMessage():
                await ws.close()
                return
            case RegisterMessage():
                # Protocol violation — a second register on a live WS
                # would otherwise reset the heartbeat window forever.
                logger.warning(f"Duplicate register on live WS for run {run_id}")
                await ws.close(code=4001)
                return


def _parse(raw: str) -> ClientMessage | None:
    try:
        return _client_message_adapter.validate_json(raw)
    except ValidationError:
        logger.warning(f"Unparseable client message: {raw[:200]}")
        return None
