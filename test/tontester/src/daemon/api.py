"""FastAPI application: dashboard HTTP API, WebSocket IPC, and Prometheus proxy."""

import asyncio
import contextlib
import logging
from collections.abc import AsyncGenerator, Callable
from typing import Literal, final

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import RedirectResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from .ipc import RunUrlResolver, build_ws_router
from .models import NodeTarget, RunMetadata
from .prom_proxy import PromProxy
from .protocols import StorageBackend
from .runs import RunsSupervisor

logger = logging.getLogger(__name__)


class RunInfo(BaseModel):
    run_id: str
    status: str
    start_time: str
    end_time: str | None
    description: str
    git_branch: str
    git_commit_id: str
    nodes: list[NodeTarget]


class DaemonUrls(BaseModel):
    dashboard_url: str
    grafana_url: str


class InitialSnapshot(BaseModel):
    """Full dashboard state sent once per connection on connect."""

    type: Literal["initial"] = "initial"
    info: DaemonUrls
    runs: list[RunInfo]


class RunUpdate(BaseModel):
    """One row's fresh state after a transition (register, release, …).

    Runs are never deleted from storage, so the frontend upserts by
    ``run.run_id``.
    """

    type: Literal["run_update"] = "run_update"
    run: RunInfo


def _run_to_info(run: RunMetadata) -> RunInfo:
    return RunInfo(
        run_id=run.run_id,
        status=run.status.value,
        start_time=run.start_time.isoformat(),
        end_time=run.end_time.isoformat() if run.end_time else None,
        description=run.metadata.description,
        git_branch=run.metadata.git_branch,
        git_commit_id=run.metadata.git_commit_id,
        nodes=run.metadata.nodes,
    )


@final
class DashboardBus:
    """Authoritative in-memory cache + fan-out for dashboard updates.

    Hub-and-spoke: every state change from the supervisor lands here via
    :meth:`notify` (sync, fire-and-forget). The bus spawns a background
    task that reads the one affected row from storage and pushes a
    :class:`RunUpdate` to every subscriber. New WS connections get
    :meth:`snapshot` from the cache — no per-connection ``list_runs``.

    Fire-and-forget is deliberate: the supervisor's register/release
    return path must not block on UI work. Tasks are tracked so they can
    be awaited at shutdown; a supervisor call triggers at most one DB
    read + one Queue.put_nowait per subscriber, so backpressure isn't a
    real concern.
    """

    def __init__(self, storage: StorageBackend, info: DaemonUrls):
        self._storage = storage
        self._info = info
        self._cache: dict[str, RunInfo] = {}
        self._subscribers: set[asyncio.Queue[RunUpdate]] = set()
        self._tasks: set[asyncio.Task[None]] = set()

    async def load_initial(self) -> None:
        """Populate the cache from storage. Called once at daemon startup,
        before uvicorn accepts connections — after this point the cache
        is kept current by :meth:`notify`."""
        items = await self._storage.list_runs(limit=100)
        self._cache = {r.run_id: _run_to_info(r) for r in items}

    def notify(self, run_id: str) -> None:
        task = asyncio.create_task(self._push_run(run_id), name=f"dashboard-push-{run_id}")
        self._tasks.add(task)
        task.add_done_callback(self._tasks.discard)

    async def aclose(self) -> None:
        """Wait for in-flight push tasks. Called from the daemon's
        shutdown stack so we don't orphan the DB reads."""
        if self._tasks:
            _ = await asyncio.gather(*self._tasks, return_exceptions=True)

    async def _push_run(self, run_id: str) -> None:
        try:
            row = await self._storage.get_run_metadata(run_id)
        except Exception:
            logger.exception(f"Dashboard push: storage read failed for {run_id}")
            return
        if row is None:
            # New runs are registered by the supervisor before it notifies,
            # so a missing row here would be a storage-layer bug.
            logger.warning(f"Dashboard push: no row for {run_id}")
            return
        info = _run_to_info(row)
        self._cache[run_id] = info
        update = RunUpdate(run=info)
        for q in self._subscribers:
            q.put_nowait(update)

    def snapshot(self) -> InitialSnapshot:
        return InitialSnapshot(info=self._info, runs=list(self._cache.values()))

    @contextlib.asynccontextmanager
    async def subscribe(self) -> AsyncGenerator[asyncio.Queue[RunUpdate]]:
        q: asyncio.Queue[RunUpdate] = asyncio.Queue()
        self._subscribers.add(q)
        try:
            yield q
        finally:
            self._subscribers.discard(q)


def create_app(
    *,
    storage: StorageBackend,
    runs: RunsSupervisor,
    url_resolver: RunUrlResolver,
    frontend_dir: str,
    on_shutdown_request: Callable[[], None],
    bus: DashboardBus,
) -> tuple[FastAPI, PromProxy]:
    """Returns the FastAPI app and the owned :class:`PromProxy`.

    The proxy holds an httpx client that must be closed on daemon shutdown
    (see ``daemon.py``'s AsyncExitStack) — FastAPI has no built-in resource
    lifetime hook we want to rely on here, so the caller holds the handle
    directly.
    """
    app = FastAPI(title="TON Dashboard Daemon")

    proxy = PromProxy(runs)

    @app.get("/api/info")
    async def _() -> DaemonUrls:
        return DaemonUrls(
            dashboard_url=url_resolver.dashboard_url,
            grafana_url=url_resolver.grafana_url,
        )

    @app.get("/api/runs")
    async def _() -> list[RunInfo]:
        items = await storage.list_runs(limit=100)
        return [_run_to_info(run) for run in items]

    @app.get("/api/runs/{run_id}")
    async def _(run_id: str) -> RunInfo:
        run = await storage.get_run_metadata(run_id)
        if not run:
            raise HTTPException(status_code=404, detail="Run not found")
        return _run_to_info(run)

    @app.get("/health")
    async def _() -> dict[str, str]:
        return {}

    @app.post("/admin/shutdown")
    async def _() -> dict[str, str]:
        on_shutdown_request()
        return {}

    @app.get("/grafana")
    async def _() -> RedirectResponse:
        return RedirectResponse(f"{url_resolver.grafana_url}/d/ton-overview?refresh=5s")

    @app.websocket("/ws/dashboard")
    async def _(ws: WebSocket) -> None:
        """Push dashboard state to the browser.

        One frame on connect (``InitialSnapshot`` from the cache), then one
        per state change (``RunUpdate`` with just the affected row). Nothing
        in this handler hits storage — the bus owns the reads.
        """
        await ws.accept()
        try:
            async with bus.subscribe() as q:
                await ws.send_text(bus.snapshot().model_dump_json())
                while True:
                    update = await q.get()
                    await ws.send_text(update.model_dump_json())
        except WebSocketDisconnect:
            pass
        except Exception:
            logger.exception("Dashboard WS error")

    # Grafana → daemon → per-run Prometheus (live + lazy-boot archive).
    app.include_router(proxy.query_router())

    # Per-run Prometheus → daemon → real scrape targets. Keeps the
    # prometheus container's egress whitelist down to one port (ours).
    app.include_router(proxy.scrape_router())

    # Test-harness IPC.
    app.include_router(build_ws_router(runs, url_resolver))

    # Dashboard SPA — mounted last so it doesn't shadow API routes.
    app.mount("/", StaticFiles(directory=frontend_dir, html=True), name="frontend")

    return app, proxy
