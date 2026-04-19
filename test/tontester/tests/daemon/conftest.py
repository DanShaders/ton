import asyncio
import time
from collections.abc import AsyncIterator
from pathlib import Path
from typing import cast

import aiotools
import pytest_asyncio
import uvicorn
from daemon.ipc import RunUrlResolver, build_ws_router
from daemon.testing import Rig, WsServer, build_rig
from fastapi import FastAPI


@pytest_asyncio.fixture
async def virtual_clock():
    """Opt-in virtual asyncio clock.

    ``asyncio.sleep`` / ``asyncio.wait_for`` become instant; the loop's
    clock advances only to wake scheduled callbacks. Opt-in (not autouse)
    because ipc tests run against real uvicorn and want real I/O timing.
    """
    with aiotools.VirtualClock().patch_loop():
        yield


@pytest_asyncio.fixture
async def rig(tmp_path: Path):
    r = build_rig(tmp_path)
    try:
        yield r
    finally:
        await r.manager.shutdown()
        r.sqlite.close()


@pytest_asyncio.fixture
async def hooked_rig(tmp_path: Path):
    r = build_rig(tmp_path, hooked_storage=True)
    try:
        yield r
    finally:
        await r.manager.shutdown()
        r.sqlite.close()


@pytest_asyncio.fixture
async def ws_server(rig: Rig) -> AsyncIterator[WsServer]:
    """Real uvicorn on an ephemeral port, running only the ws IPC router.

    Heartbeat is deliberately short (200 ms) so timeout tests are fast
    without needing a virtual clock — ipc tests exercise real TCP I/O and
    want real timing.
    """
    app = FastAPI()
    urls = RunUrlResolver(dashboard_url="http://daemon.test", grafana_url="http://grafana.test")
    app.include_router(build_ws_router(rig.manager, urls, heartbeat_timeout_seconds=0.2))

    config = uvicorn.Config(
        app,
        host="127.0.0.1",
        port=0,
        log_level="error",
        lifespan="off",
        ws="websockets-sansio",
    )
    server = uvicorn.Server(config)
    serve_task = asyncio.create_task(server.serve(), name="test-uvicorn")

    deadline = time.monotonic() + 5.0
    while not server.started and time.monotonic() < deadline:
        await asyncio.sleep(0.01)
    assert server.started, "uvicorn did not start within 5s"
    assert server.servers, "uvicorn has no bound server"
    sockname = cast("tuple[str, int]", server.servers[0].sockets[0].getsockname())
    host, port = sockname
    try:
        yield WsServer(url=f"ws://{host}:{port}", rig=rig)
    finally:
        server.should_exit = True
        try:
            _ = await asyncio.wait_for(serve_task, timeout=5.0)
        except asyncio.TimeoutError:
            _ = serve_task.cancel()
            try:
                _ = await serve_task
            except asyncio.CancelledError, Exception:
                pass
