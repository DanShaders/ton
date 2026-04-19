"""WebSocket entry point for test-harness <-> daemon IPC.

Single endpoint ``/runs/{run_id}``. The client:

1. Opens the WebSocket.
2. Sends the *handshake* message (``action="register"``) carrying
   :class:`TestMetadata`.
3. Receives a response with the Grafana / Prometheus URLs.
4. Sends a ``heartbeat`` every ~10 s until the run ends.
5. Closes the socket (clean) — or the daemon times out the heartbeat and
   closes with code 4000.

Either way, the run is archived when the WebSocket goes away. A crash on
either side lands the run in the same state (``archived`` on clean close,
``crashed`` on abnormal close) so the run is queryable regardless.
"""

import asyncio
import logging
from typing import Annotated, Literal, final

from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from pydantic import BaseModel, Field, TypeAdapter, ValidationError

from .runs import DaemonStopped, RunAlreadyActive, RunsManager, RunStartFailed
from .storage import TestMetadata

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
        grafana = f"{self.grafana_url}/d/ton-overview?refresh=5s&var-datasource=run-{run_id}"
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
    runs: RunsManager,
    urls: RunUrlResolver,
    *,
    heartbeat_timeout_seconds: float = DEFAULT_HEARTBEAT_TIMEOUT_SECONDS,
) -> APIRouter:
    router = APIRouter()

    @router.websocket("/runs/{run_id}")
    async def _(ws: WebSocket, run_id: str) -> None:
        await _handle_run_ws(runs, urls, ws, run_id, heartbeat_timeout_seconds)

    return router


async def _handle_run_ws(
    runs: RunsManager,
    urls: RunUrlResolver,
    ws: WebSocket,
    run_id: str,
    heartbeat_timeout_seconds: float,
) -> None:
    await ws.accept()
    registered = False
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
            _ = await runs.register(run_id, hello.metadata)
        except RunAlreadyActive:
            await ws.send_text(
                RegisterError(
                    code="run_already_active",
                    message=f"run {run_id} is already active",
                ).model_dump_json()
            )
            await ws.close(code=4002)
            return
        except RunStartFailed as e:
            await ws.send_text(
                RegisterError(
                    code="start_failed",
                    message=str(e),
                ).model_dump_json()
            )
            await ws.close(code=4003)
            return
        except DaemonStopped:
            await ws.send_text(
                RegisterError(
                    code="daemon_stopping",
                    message="daemon is shutting down",
                ).model_dump_json()
            )
            await ws.close(code=4004)
            return

        registered = True
        await ws.send_text(urls.for_run(run_id).model_dump_json())

        # Main loop: receive heartbeats / complete until disconnect.
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
    except WebSocketDisconnect:
        pass
    except Exception:
        logger.exception(f"Error on run WebSocket for {run_id}")
    finally:
        if registered:
            try:
                await runs.release(run_id)
            except Exception:
                logger.exception(f"Error finalizing run {run_id}")


def _parse(raw: str) -> ClientMessage | None:
    try:
        return _client_message_adapter.validate_json(raw)
    except ValidationError:
        logger.warning(f"Unparseable client message: {raw[:200]}")
        return None
