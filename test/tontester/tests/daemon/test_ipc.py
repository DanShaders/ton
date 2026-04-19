"""End-to-end tests for the daemon's WebSocket IPC.

Runs a real uvicorn instance on an ephemeral port (``ws_server`` fixture)
and talks to it with the ``websockets`` client. Heartbeat timeout is shrunk
to 200 ms so the timeout test finishes in real time without a virtual clock.

These tests deliberately opt OUT of the virtual clock — we want real TCP
timing so the uvicorn + starlette WS machinery is exercised as in production.
"""

import asyncio
from pathlib import Path

import pytest
import uvicorn
from daemon.client import DashboardClient
from daemon.ipc import RegisterError, RegisterMessage, RegisterOk
from daemon.storage import NodeTarget
from daemon.storage import TestMetadata as _TestMetadata
from daemon.testing import WsServer
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed

pytestmark = pytest.mark.asyncio


def _md(*nodes: str) -> _TestMetadata:
    return _TestMetadata(
        description="test",
        nodes=[NodeTarget(name=n, address=f"127.0.0.1:{5000 + i}") for i, n in enumerate(nodes)],
    )


def _register(metadata: _TestMetadata) -> str:
    return RegisterMessage(action="register", metadata=metadata).model_dump_json()


async def test_register_returns_ok_payload(ws_server: WsServer):
    async with connect(f"{ws_server.url}/runs/r1") as ws:
        await ws.send(_register(_md("n0")))
        raw = await ws.recv()
        assert isinstance(raw, str)
        ok = RegisterOk.model_validate_json(raw)
        assert ok.run_id == "r1"
        assert "run-r1" in ok.grafana_url


async def test_bad_handshake_is_rejected(ws_server: WsServer):
    async with connect(f"{ws_server.url}/runs/r1") as ws:
        await ws.send("not a register message")
        raw = await ws.recv()
        assert isinstance(raw, str)
        err = RegisterError.model_validate_json(raw)
        assert err.code == "bad_handshake"
        # Server will close with 4001; let the client observe.
        with pytest.raises(ConnectionClosed) as exc_info:
            _ = await ws.recv()
        assert exc_info.value.rcvd is not None
        assert exc_info.value.rcvd.code == 4001


async def test_run_already_active(ws_server: WsServer):
    async with connect(f"{ws_server.url}/runs/r1") as ws1:
        await ws1.send(_register(_md("n0")))
        _ = await ws1.recv()

        async with connect(f"{ws_server.url}/runs/r1") as ws2:
            await ws2.send(_register(_md("n0")))
            raw = await ws2.recv()
            assert isinstance(raw, str)
            err = RegisterError.model_validate_json(raw)
            assert err.code == "run_already_active"
            with pytest.raises(ConnectionClosed) as exc_info:
                _ = await ws2.recv()
            assert exc_info.value.rcvd is not None
            assert exc_info.value.rcvd.code == 4002


async def test_heartbeat_timeout_closes_with_4000(ws_server: WsServer):
    async with connect(f"{ws_server.url}/runs/r1") as ws:
        await ws.send(_register(_md("n0")))
        _ = await ws.recv()
        # Send no heartbeats — server should close after 200 ms.
        with pytest.raises(ConnectionClosed) as exc_info:
            _ = await asyncio.wait_for(ws.recv(), timeout=2.0)
        assert exc_info.value.rcvd is not None
        assert exc_info.value.rcvd.code == 4000


async def test_explicit_complete_closes_cleanly(ws_server: WsServer):
    async with connect(f"{ws_server.url}/runs/r1") as ws:
        await ws.send(_register(_md("n0")))
        _ = await ws.recv()
        await ws.send('{"action":"complete"}')
        with pytest.raises(ConnectionClosed) as exc_info:
            _ = await asyncio.wait_for(ws.recv(), timeout=2.0)
        # Normal closure; starlette sends 1000 for an un-coded close.
        assert exc_info.value.rcvd is not None
        assert exc_info.value.rcvd.code == 1000


async def test_duplicate_register_on_live_ws_closes_4001(ws_server: WsServer):
    """Bug: a client sending a second ``register`` on an already-live WS
    used to just log and continue, indefinitely extending the heartbeat
    deadline. Fix: treat it as a protocol violation and close.
    """
    async with connect(f"{ws_server.url}/runs/r1") as ws:
        await ws.send(_register(_md("n0")))
        _ = await ws.recv()  # RegisterOk
        await ws.send(_register(_md("n0")))
        with pytest.raises(ConnectionClosed) as exc_info:
            _ = await asyncio.wait_for(ws.recv(), timeout=2.0)
        assert exc_info.value.rcvd is not None
        assert exc_info.value.rcvd.code == 4001


# -------- regression: register-while-shutting-down must send a structured error


async def test_client_retries_transient_run_already_active(tmp_path: Path):
    """Bug: if the daemon's ``runs.release`` for a prior WS hasn't fired
    by the time a reconnect's register arrives, the daemon returns
    ``run_already_active``. The supervisor used to give up permanently.
    Fix: treat that specific code as transient on reconnect.
    """
    register_calls = 0
    done = asyncio.Event()
    app = FastAPI()

    @app.websocket("/runs/{run_id}")
    async def _scripted_ws(ws: WebSocket, run_id: str) -> None:
        nonlocal register_calls
        await ws.accept()
        try:
            _ = await ws.receive_text()
        except WebSocketDisconnect:
            return
        register_calls += 1
        n = register_calls
        ok_payload = RegisterOk(
            run_id=run_id,
            dashboard_url="http://d",
            grafana_url="http://g",
            prometheus_url="http://p",
        ).model_dump_json()
        if n == 1:
            # Accept the initial register, then drop to trigger reconnect.
            await ws.send_text(ok_payload)
            await asyncio.sleep(0.05)
            await ws.close(code=1001)
        elif n == 2:
            # Reconnect attempt: simulate the stale-LIVE window.
            await ws.send_text(
                RegisterError(code="run_already_active", message="stale").model_dump_json()
            )
            await ws.close(code=4002)
        else:
            await ws.send_text(ok_payload)
            done.set()
            try:
                while True:
                    _ = await ws.receive_text()
            except WebSocketDisconnect:
                pass

    _ = _scripted_ws  # silence unused-function warning; registered via decorator

    socket_path = tmp_path / "scripted.sock"
    config = uvicorn.Config(
        app,
        uds=str(socket_path),
        log_level="error",
        lifespan="off",
        ws="websockets-sansio",
    )
    server = uvicorn.Server(config)
    serve_task = asyncio.create_task(server.serve())
    try:
        while not server.started:
            await asyncio.sleep(0.01)

        client = DashboardClient(
            socket_path,
            heartbeat_interval_seconds=0.05,
            initial_backoff_seconds=0.05,
            max_backoff_seconds=0.1,
            run_already_active_retries=3,
        )
        _ = await client.register("test", _md("n0"))
        _ = await asyncio.wait_for(done.wait(), timeout=5.0)
        assert register_calls >= 3
        await client.aclose()
    finally:
        server.should_exit = True
        try:
            _ = await asyncio.wait_for(serve_task, timeout=5.0)
        except asyncio.TimeoutError:
            _ = serve_task.cancel()


async def test_register_after_shutdown_sends_daemon_stopping(ws_server: WsServer):
    await ws_server.rig.manager.shutdown()

    async with connect(f"{ws_server.url}/runs/r1") as ws:
        await ws.send(_register(_md("n0")))
        raw = await ws.recv()
        assert isinstance(raw, str), "expected a structured error, not a connection drop"
        err = RegisterError.model_validate_json(raw)
        assert err.code == "daemon_stopping", (
            f"expected daemon_stopping, got {err.code}: {err.message}"
        )
        with pytest.raises(ConnectionClosed) as exc_info:
            _ = await ws.recv()
        assert exc_info.value.rcvd is not None
        assert exc_info.value.rcvd.code == 4004
