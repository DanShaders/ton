"""FastAPI application: dashboard HTTP API, WebSocket IPC, and Prometheus proxy."""

from collections.abc import Callable
from typing import final

from fastapi import FastAPI, HTTPException
from fastapi.responses import RedirectResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from .ipc import RunUrlResolver, build_ws_router
from .prom_proxy import PromProxy
from .runs import RunsManager
from .storage import NodeTarget, RunMetadata, StorageBackend


class RunInfo(BaseModel):
    run_id: str
    status: str
    start_time: str
    end_time: str | None
    description: str
    git_branch: str
    git_commit_id: str
    nodes: list[NodeTarget]
    host_port: int


class DaemonUrls(BaseModel):
    dashboard_url: str
    grafana_url: str


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
        host_port=run.host_port,
    )


@final
class AppState:
    """Lifecycle-managed state held on the FastAPI app for clean shutdown."""

    def __init__(self, proxy: PromProxy):
        self.proxy = proxy


def create_app(
    *,
    storage: StorageBackend,
    runs: RunsManager,
    url_resolver: RunUrlResolver,
    frontend_dir: str,
    on_shutdown_request: Callable[[], None],
) -> tuple[FastAPI, AppState]:
    app = FastAPI(title="TON Dashboard Daemon")

    proxy = PromProxy(runs)
    state = AppState(proxy=proxy)

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
        return {"status": "ok"}

    @app.post("/admin/shutdown")
    async def _() -> dict[str, str]:
        on_shutdown_request()
        return {"status": "shutting-down"}

    @app.get("/grafana")
    async def _() -> RedirectResponse:
        return RedirectResponse(f"{url_resolver.grafana_url}/d/ton-overview?refresh=5s")

    # Per-run Prometheus proxy (live + lazy-boot archive).
    app.include_router(proxy.router())

    # Test-harness IPC.
    app.include_router(build_ws_router(runs, url_resolver))

    # Dashboard SPA — mounted last so it doesn't shadow API routes.
    app.mount("/", StaticFiles(directory=frontend_dir, html=True), name="frontend")

    return app, state
