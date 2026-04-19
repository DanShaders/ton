"""HTTP proxies for Prometheus traffic, in both directions.

Two directions that both funnel through this module:

1. **Grafana → daemon → live/archive prometheus container.**
   :class:`PromProxy` serves ``/runs/{run_id}/prom/{path}`` — the URL
   grafana's provisioned datasource points at. Resolves a query pin on
   the run and streams the upstream response back.

2. **Prometheus container → daemon → scrape target.**
   :meth:`PromProxy.scrape_router` serves
   ``/scrape/{run_id}/{node_name}/metrics`` — Prometheus inside the
   per-run container is configured to hit
   ``127.0.0.1:<daemon_port>/scrape/...`` instead of the real target.
   The node address is resolved via the run's actor
   (:meth:`RunsSupervisor.resolve_node_address`), not SQLite. This
   keeps the container's network egress restricted to the daemon's
   port (whitelist-of-one via pasta ``-T``) while still supporting
   remote scrape targets.

Cancel zone
-----------

This module is in the **uvicorn zone** — request handlers and the
streaming body generator may be cancelled by uvicorn at any ``await``
when the server is shutting down. Cancellation cannot corrupt actor
state because pin acquisition and release go through the actor system
(see the "Cancel zone" block in :mod:`.runs`). The local concerns that
remain:

- The pin is kept alive across the streaming handoff by the detach
  dance: ``async with query_pin(...)`` acquires; we construct the full
  :class:`StreamingResponse` (which can raise) *before* calling
  :meth:`QueryPin.detach`; on any raise in that region we explicitly
  ``await upstream.aclose()`` and let the CM release the pin. Only
  after the response is fully built and cannot raise do we detach —
  nothing between detach and return can fail, so the pin can't be
  orphaned.
- Once detached, the generator's ``finally`` owns both the upstream
  stream and the pin. Starlette's ``StreamingResponse`` always drives
  the generator to closure (normal completion, client disconnect, or
  server shutdown), so the finally runs on every path.
- ``pin.release()`` in the generator's ``finally`` may itself be
  cancelled; see the actor-zone invariant — the actor still processes
  the release message, bounded by supervisor shutdown.
- Scrape-proxy requests are short (single GET of a prometheus metrics
  endpoint with a bounded timeout); cancellation just aborts the
  outbound httpx call and returns 5xx to the container, no shared state.
"""

import logging
from collections.abc import AsyncGenerator, Iterable
from typing import final

import httpx
from fastapi import APIRouter, HTTPException, Request, Response
from fastapi.responses import StreamingResponse

from .runs import QueryPin, RunsSupervisor

logger = logging.getLogger(__name__)

_UPSTREAM_TIMEOUT = httpx.Timeout(connect=5.0, read=60.0, write=60.0, pool=5.0)
_SCRAPE_TIMEOUT = httpx.Timeout(connect=2.0, read=30.0, write=10.0, pool=2.0)

# Headers that are meaningless or harmful when forwarded between a client
# and an upstream we've independently connected to.
_HOP_BY_HOP_HEADERS = frozenset(
    {
        "connection",
        "keep-alive",
        "proxy-authenticate",
        "proxy-authorization",
        "te",
        "trailers",
        "transfer-encoding",
        "upgrade",
        "host",
        "content-length",
    }
)


@final
class PromProxy:
    """Both directions of per-run Prometheus HTTP proxying, sharing one
    httpx client + lifetime. Two router factories:

    * :meth:`query_router` — Grafana → daemon → live/archive prom.
    * :meth:`scrape_router` — per-run prom → daemon → real scrape target.

    Node addresses for scraping are resolved through the run's actor
    (:meth:`RunsSupervisor.resolve_node_address`), not SQLite, so the
    hot scrape path stays off-disk.
    """

    def __init__(self, runs: RunsSupervisor):
        self.runs = runs
        self.client = httpx.AsyncClient(timeout=_UPSTREAM_TIMEOUT)

    async def aclose(self) -> None:
        await self.client.aclose()

    def query_router(self) -> APIRouter:
        router = APIRouter()

        # Catch-all rather than enumerate /api/v1/... + /-/... — Prometheus adds endpoints.
        @router.api_route(
            "/runs/{run_id}/prom/{path:path}",
            methods=["GET", "POST", "HEAD", "OPTIONS"],
        )
        async def _(run_id: str, path: str, request: Request) -> Response:
            return await self._proxy_query(run_id, path, request)

        return router

    def scrape_router(self) -> APIRouter:
        router = APIRouter()

        @router.get("/scrape/{run_id}/{node_name}/metrics")
        async def _(run_id: str, node_name: str) -> Response:
            return await self._proxy_scrape(run_id, node_name)

        return router

    async def _proxy_scrape(self, run_id: str, node_name: str) -> Response:
        address = await self.runs.resolve_node_address(run_id, node_name)
        if address is None:
            raise HTTPException(
                status_code=404,
                detail=f"No live node {node_name} in run {run_id}",
            )
        try:
            resp = await self.client.get(f"http://{address}/metrics", timeout=_SCRAPE_TIMEOUT)
        except httpx.HTTPError as e:
            logger.warning(f"scrape proxy upstream error for {run_id}/{node_name}: {e}")
            raise HTTPException(status_code=502, detail=f"Upstream error: {e}") from e
        # Prometheus cares about Content-Type (openmetrics vs text plain)
        # and status; pass them through.
        content_type: str = resp.headers.get("content-type") or "text/plain"
        return Response(
            content=resp.content,
            status_code=resp.status_code,
            media_type=content_type,
        )

    async def _proxy_query(self, run_id: str, path: str, request: Request) -> Response:
        async with self.runs.query_pin(run_id) as pin:
            if pin.port is None:
                raise HTTPException(status_code=404, detail=f"Run {run_id} not queryable")
            body = await request.body()
            headers = _strip_hop_by_hop(request.headers.items())
            upstream_url = f"http://127.0.0.1:{pin.port}/{path}"
            try:
                upstream_req = self.client.build_request(
                    request.method,
                    upstream_url,
                    params=request.query_params,
                    headers=headers,
                    content=body if body else None,
                )
                upstream_resp = await self.client.send(upstream_req, stream=True)
            except httpx.HTTPError as e:
                logger.warning(f"Upstream error for run {run_id}: {e}")
                raise HTTPException(status_code=502, detail=f"Upstream error: {e}") from e

            # Build the full response BEFORE detaching so nothing between
            # detach and return can leak the pin or upstream stream. If
            # _strip_hop_by_hop or StreamingResponse.__init__ raises here,
            # we still own the pin via the CM and must explicitly close
            # the upstream stream — the CM will release the pin on unwind.
            try:
                response_headers = _strip_hop_by_hop(upstream_resp.headers.items())
                response = StreamingResponse(
                    _stream_upstream(upstream_resp, pin),
                    status_code=upstream_resp.status_code,
                    headers=response_headers,
                )
            except Exception:
                await upstream_resp.aclose()
                raise
            # Success: hand pin ownership to the generator's ``finally``.
            pin.detach()

        return response


async def _stream_upstream(upstream: httpx.Response, pin: QueryPin) -> AsyncGenerator[bytes]:
    """Stream body bytes from ``upstream`` and release ``pin`` on exit.

    The generator's ``finally`` runs on every exit path starlette can drive
    — normal completion, client disconnect, server shutdown — so ``pin``
    and ``upstream`` are released together regardless of why iteration ends.
    """
    try:
        async for chunk in upstream.aiter_raw():
            yield chunk
    finally:
        try:
            await upstream.aclose()
        finally:
            await pin.release()


def _strip_hop_by_hop(items: Iterable[tuple[str, str]]) -> dict[str, str]:
    return {k: v for k, v in items if k.lower() not in _HOP_BY_HOP_HEADERS}
