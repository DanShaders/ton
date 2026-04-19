"""Transparent HTTP proxy that routes per-run Prometheus queries.

Grafana talks to one URL per run — ``/runs/<run_id>/prom/...`` — without
caring whether the run is live, archived, or has to be lazy-booted. This
module is the place that resolves that URL to an actual local container.
"""

import logging
from collections.abc import Iterable
from typing import final

import httpx
from fastapi import APIRouter, HTTPException, Request, Response
from fastapi.responses import StreamingResponse

from .runs import RunsManager

logger = logging.getLogger(__name__)

_UPSTREAM_TIMEOUT = httpx.Timeout(connect=5.0, read=60.0, write=60.0, pool=5.0)

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
    def __init__(self, runs: RunsManager):
        self.runs = runs
        self._client = httpx.AsyncClient(timeout=_UPSTREAM_TIMEOUT)

    async def aclose(self) -> None:
        await self._client.aclose()

    def router(self) -> APIRouter:
        router = APIRouter()

        # Catch-all rather than enumerate /api/v1/... + /-/... — Prometheus adds endpoints.
        @router.api_route(
            "/runs/{run_id}/prom/{path:path}",
            methods=["GET", "POST", "HEAD", "OPTIONS"],
        )
        async def _(run_id: str, path: str, request: Request) -> Response:
            return await self._proxy(run_id, path, request)

        return router

    async def _proxy(self, run_id: str, path: str, request: Request) -> Response:
        host_port = await self.runs.acquire_query_pin(run_id)
        if host_port is None:
            raise HTTPException(status_code=404, detail=f"Run {run_id} not queryable")

        # From here on, every path must eventually ``release_query_pin``.
        release = self.runs.release_query_pin
        upstream_url = f"http://127.0.0.1:{host_port}/{path}"
        body = await request.body()
        headers = _strip_hop_by_hop(request.headers.items())

        try:
            upstream_req = self._client.build_request(
                request.method,
                upstream_url,
                params=request.query_params,
                headers=headers,
                content=body if body else None,
            )
            upstream_resp = await self._client.send(upstream_req, stream=True)
        except httpx.HTTPError as e:
            await release(run_id)
            logger.warning(f"Upstream error for run {run_id}: {e}")
            raise HTTPException(status_code=502, detail=f"Upstream error: {e}") from e

        response_headers = _strip_hop_by_hop(upstream_resp.headers.items())

        async def iter_body():
            try:
                async for chunk in upstream_resp.aiter_raw():
                    yield chunk
            finally:
                try:
                    await upstream_resp.aclose()
                finally:
                    await release(run_id)

        return StreamingResponse(
            iter_body(),
            status_code=upstream_resp.status_code,
            headers=response_headers,
        )


def _strip_hop_by_hop(items: Iterable[tuple[str, str]]) -> dict[str, str]:
    return {k: v for k, v in items if k.lower() not in _HOP_BY_HOP_HEADERS}
