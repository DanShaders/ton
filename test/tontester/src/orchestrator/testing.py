"""Test-harness helpers co-located with the production package.

Importable from anywhere as ``from orchestrator.testing import …``.
Lives in ``src/orchestrator/`` (not under ``tests/``) because pytest's
``conftest.py`` isn't a normal module and ``tests/`` directories aren't
packages — putting helpers there triggers basedpyright's
``reportImplicitRelativeImport`` and breaks import paths in subtle ways.
The codebase already follows this pattern for ``daemon.testing``.

Currently exported: subscription-based wait helpers used by the
agent / namespace / workloadset smoke tests instead of busy-wait
``while … await asyncio.sleep(0.01)`` polling loops. Polling is slow,
flaky, and ignores the watch primitive the store already provides.
Both helpers subscribe synchronously before iterating so events fired
immediately after the call returns are observed without a race window.
"""

import asyncio
from collections.abc import Callable

from pydantic import BaseModel

from .resources import ResourceLike
from .store import InMemoryStore


async def wait_for_event[T: ResourceLike[BaseModel, BaseModel]](
    store: InMemoryStore,
    resource_type: type[T],
    predicate: Callable[[T], bool],
    *,
    timeout: float = 2.0,
) -> T:
    """Subscribe to ``resource_type`` and return the first resource that
    satisfies ``predicate``.

    Order is: open subscription (synchronous bus registration), scan
    ``store.list`` for an already-matching row, then iterate live
    events. The catch-up scan covers the common test pattern of "do
    an action, then wait for its consequence" without forcing the
    caller to interleave a manual subscription open and the apply
    call — anything observable in ``store.list`` after the open is
    either matched immediately or will fire as a later event we
    haven't missed. Times out via ``asyncio.wait_for``; the
    surrounding ``async with`` guarantees bus cleanup on
    ``TimeoutError``.
    """

    async def _drain() -> T:
        async with store.subscription(resource_type) as sub:
            for current in store.list(resource_type):
                if predicate(current):
                    return current
            async for event in sub:
                if predicate(event.resource):
                    return event.resource
            raise RuntimeError(
                f"subscription for {resource_type.__name__} closed before predicate matched"
            )

    return await asyncio.wait_for(_drain(), timeout=timeout)


async def wait_for_asyncio_idle(
    *,
    timeout: float = 1.0,
    stable_iterations: int = 3,
) -> None:
    """Yield until the asyncio loop's set of pending tasks stops changing.

    Pairs with ``aiotools.VirtualClock().patch_loop()`` to make tests
    instant and deterministic: the virtual loop's ``time()`` advances
    only when callbacks are scheduled, so the timeout is virtual.

    Heuristic: after each ``await asyncio.sleep(0)`` we snapshot the
    set of pending tasks (excluding ourselves). If the set is
    *identical* across ``stable_iterations`` consecutive yields, no
    task created another, completed, or got rescheduled — i.e. every
    surviving task is awaiting on a Future that isn't ready. Long-
    lived background loops (an agent's desired loop sitting on
    ``async for event in sub:``) appear in every snapshot and thus
    count as stable; the helper doesn't try to wait for them to
    finish, only for everything that *can* run to have run.

    Use this for "let scheduled work settle" — replaces hand-tuned
    ``await asyncio.sleep(0.05)``. For "wait for specific state",
    use :func:`wait_for_event` / :func:`wait_for_state` which subscribe
    to the bus and check predicates per event.

    Raises ``TimeoutError`` if stability isn't reached within
    ``timeout`` (virtual) seconds — typically meaning the system has a
    runaway scheduling loop.
    """
    current = asyncio.current_task()
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    last: frozenset[asyncio.Task[object]] = frozenset()
    stable = 0
    while loop.time() < deadline:
        await asyncio.sleep(0)
        pending = frozenset(
            t for t in asyncio.all_tasks() if t is not current and not t.done()
        )
        if pending == last:
            stable += 1
            if stable >= stable_iterations:
                return
        else:
            stable = 0
            last = pending
    raise TimeoutError(
        f"asyncio loop did not become idle within {timeout}s "
        f"(last pending: {sorted(t.get_name() for t in last)})"
    )


async def wait_for_state(
    store: InMemoryStore,
    resource_type: type[ResourceLike[BaseModel, BaseModel]],
    state_check: Callable[[], bool],
    *,
    timeout: float = 2.0,
) -> None:
    """Subscribe to ``resource_type`` then re-evaluate ``state_check``
    after each event until it returns True.

    Use this when the predicate spans multiple resources of the kind
    (e.g. "all replicas have finalizers", "no workloads left in this
    namespace") or watches an external side-channel (e.g.
    ``fake_runtime.applies``) — ``wait_for_event`` is per-resource.
    ``state_check`` is also evaluated once before iterating so
    already-true conditions return immediately, avoiding a deadlock
    if no further events are ever published.
    """

    async def _drain() -> None:
        async with store.subscription(resource_type) as sub:
            if state_check():
                return
            async for _event in sub:
                if state_check():
                    return
            raise RuntimeError(
                f"subscription for {resource_type.__name__} closed before state_check passed"
            )

    await asyncio.wait_for(_drain(), timeout=timeout)
