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
   The node address is pulled from the run's
   :class:`~.models.RunSnapshot` (via :meth:`RunsSupervisor.snapshot`),
   not SQLite. This keeps the container's network egress restricted to
   the daemon's port (whitelist-of-one via pasta ``-T``) while still
   supporting remote scrape targets.

Dormant-run range-query clamp
-----------------------------

When a dormant run gets a ``POST`` or ``GET /api/v1/query_range``,
:meth:`_proxy_query` clamps the ``start`` / ``end`` parameters to the
run's actual lifetime before forwarding — in both the URL query string
and the form-urlencoded body Grafana sends when its datasource is
configured with ``httpMethod: POST``. Prom's 5 min lookback-delta
otherwise carries the last scrape's value forward past ``end_time``,
so overlaid dormant runs in Grafana visually bleed into each other.
Only ``query_range`` needs it; instant queries return a single point
and metadata endpoints aren't affected by lookback at all.

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
import urllib.parse
from collections.abc import AsyncGenerator, Iterable
from datetime import datetime
from typing import final

import httpx
from fastapi import APIRouter, HTTPException, Request, Response
from fastapi.responses import StreamingResponse

from .models import RunSnapshot, RunStatus
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

    Both paths read the run's state through :meth:`RunsSupervisor.snapshot`,
    the single actor-backed source of truth, rather than re-querying SQLite.
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
        snap = await self.runs.snapshot(run_id)
        if snap is None or snap.status != RunStatus.LIVE:
            raise HTTPException(
                status_code=404,
                detail=f"Run {run_id} is not live",
            )
        address = next(
            (n.address for n in snap.metadata.nodes if n.name == node_name),
            None,
        )
        if address is None:
            raise HTTPException(
                status_code=404,
                detail=f"No node {node_name} in run {run_id}",
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
            snap = await self.runs.snapshot(run_id)
            # Grafana's Prometheus datasource is configured with
            # ``httpMethod: POST``, so ``query_range`` puts the time range
            # in the form-encoded body, not the URL. Clamp both; only the
            # one Grafana actually uses will have anything to touch.
            params = httpx.QueryParams(
                tuple(_clamp_time_range(
                    list(request.query_params.multi_items()), snap, path
                ))
            )
            raw_body = await request.body()
            body = _clamp_body(raw_body, request.headers.get("content-type"), snap, path)
            headers = _strip_hop_by_hop(request.headers.items())
            upstream_url = f"http://127.0.0.1:{pin.port}/{path}"
            try:
                upstream_req = self.client.build_request(
                    request.method,
                    upstream_url,
                    params=params,
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


# Only ``query_range`` is clamped. A range query's response is what
# draws the visual carry-forward line in Grafana (5 min lookback-delta
# extends the last scraped value past the actual gap). Instant ``query``
# returns a single point and doesn't exhibit the horizontal-bleed
# problem; metadata endpoints (``/series`` etc.) return index entries
# within a window, not values, so their ``start`` / ``end`` don't need
# clamping either. Params per
# https://prometheus.io/docs/prometheus/latest/querying/api/#range-queries
# — we only touch ``start`` / ``end``.
_RANGE_QUERY_PATH = "api/v1/query_range"


def _clamp_body(
    body: bytes, content_type: str | None, snap: RunSnapshot | None, path: str
) -> bytes:
    """Same clamp as for URL params, but against a form-urlencoded body.

    Grafana with ``httpMethod: POST`` sends the time range as
    ``application/x-www-form-urlencoded`` — ``start=...&end=...&query=...``
    in the body. For anything else (no body, JSON remote-write, etc.) we
    leave it alone.
    """
    if not body or snap is None:
        return body
    if content_type is None or not content_type.startswith(
        "application/x-www-form-urlencoded"
    ):
        return body
    try:
        decoded = body.decode("utf-8")
    except UnicodeDecodeError:
        return body
    items = urllib.parse.parse_qsl(decoded, keep_blank_values=True)
    clamped = _clamp_time_range(items, snap, path)
    if clamped == items:
        return body
    return urllib.parse.urlencode(clamped).encode("utf-8")


def _clamp_time_range(
    items: list[tuple[str, str]], snap: RunSnapshot | None, path: str
) -> list[tuple[str, str]]:
    """Clamp ``query_range`` ``start`` / ``end`` to a dormant run's lifetime.

    Why: Prometheus's 5 min lookback-delta carries forward the last known
    sample for up to ~5 min past a scrape gap. When a run is DORMANT (the
    WS closed, the container is gone) no fresh samples can arrive, so
    asking Prom for values past ``end_time`` produces ghosts — horizontal
    lines that visually bleed into overlaid runs in Grafana. Forcing the
    window to ``[start_time, end_time]`` prevents that without touching
    Prom internals.

    Prom accepts ``start`` / ``end`` as either a unix timestamp (float)
    or an RFC3339 string. Anything we can't parse we leave alone — the
    clamp is a best-effort optimization, not a validation step.
    """
    if snap is None or snap.status != RunStatus.DORMANT or snap.end_time is None:
        return items
    if path.strip("/") != _RANGE_QUERY_PATH:
        return items
    end_ts = snap.end_time.timestamp()
    start_ts = snap.start_time.timestamp()
    out: list[tuple[str, str]] = []
    for k, v in items:
        if k == "end":
            ts = _parse_prom_time(v)
            if ts is not None and ts > end_ts:
                out.append((k, str(end_ts)))
                continue
        elif k == "start":
            ts = _parse_prom_time(v)
            if ts is not None and ts < start_ts:
                out.append((k, str(start_ts)))
                continue
        out.append((k, v))
    return out


def _parse_prom_time(v: str) -> float | None:
    """Parse a Prometheus ``start`` / ``end`` value — unix float or RFC3339.

    Returns ``None`` on anything we can't parse so callers pass the raw
    value through unchanged instead of rewriting into a guess.
    """
    try:
        return float(v)
    except ValueError:
        pass
    try:
        # ``Z`` suffix is RFC3339; ``fromisoformat`` only accepts it
        # starting with 3.11 — convert to ``+00:00`` defensively.
        return datetime.fromisoformat(v.replace("Z", "+00:00")).timestamp()
    except ValueError:
        return None
