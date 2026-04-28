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
from typing import Protocol, final

from pydantic import BaseModel

from .resources import ResourceLike
from .store import InMemoryStore

# ---- subprocess mocking -----------------------------------------------


@final
class MockProcess:
    """``asyncio.subprocess.Process``-shaped mock for deterministic tests.

    Mixing real ``asyncio.subprocess`` with ``virtual_clock`` is racy:
    the virtual loop advances time independently of real SIGCHLD
    delivery, so a reap task that ``await``s ``process.wait()`` may
    park indefinitely even though the real kernel has long since
    reaped. Tests that exercise supervisor / runtime lifecycle should
    use this mock instead of forking real children.

    ``MockProcess`` exposes the surface the supervisor uses:
    ``pid``, ``returncode``, sync ``terminate()`` / ``kill()``, async
    ``wait()``. Test driver calls :meth:`mock_exit` (or
    :meth:`auto_exit_on_signal`) to deterministically resolve the
    wait. No fork; no real signals; no virtual-time interaction with
    real SIGCHLD.
    """

    def __init__(self, *, pid: int = 9999):
        self._pid: int = pid
        self._returncode: int | None = None
        self._exited: asyncio.Event = asyncio.Event()
        self.terminate_called: int = 0
        self.kill_called: int = 0
        # If True, terminate() / kill() implicitly mock_exit themselves.
        # Off by default so tests can observe a stuck process.
        self._auto_exit: bool = False

    @property
    def pid(self) -> int:
        return self._pid

    @property
    def returncode(self) -> int | None:
        return self._returncode

    def terminate(self) -> None:
        self.terminate_called += 1
        if self._auto_exit and self._returncode is None:
            self.mock_exit(returncode=-15)  # SIGTERM

    def kill(self) -> None:
        self.kill_called += 1
        if self._auto_exit and self._returncode is None:
            self.mock_exit(returncode=-9)  # SIGKILL

    async def wait(self) -> int:
        _ = await self._exited.wait()
        assert self._returncode is not None
        return self._returncode

    def mock_exit(self, *, returncode: int = 0) -> None:
        """Test driver: resolve ``wait()`` with the given exit code.
        Idempotent — second call is a no-op (matches real behavior of
        an already-exited process).
        """
        if self._returncode is not None:
            return
        self._returncode = returncode
        self._exited.set()

    def auto_exit_on_signal(self) -> None:
        """Test driver: have ``terminate()`` / ``kill()`` immediately
        resolve ``wait()``. Mirrors a well-behaved process that
        responds to signals."""
        self._auto_exit = True


class _SetattrCallable(Protocol):
    def __call__(self, target: object, name: str, value: object, /) -> None: ...


class _MonkeyPatchLike(Protocol):
    setattr: _SetattrCallable


def mock_subprocess_exec(
    monkeypatch: _MonkeyPatchLike,
    *,
    pid: int = 9999,
    auto_exit: bool = True,
) -> MockProcess:
    """Patch ``asyncio.create_subprocess_exec`` to return a fresh
    :class:`MockProcess`. Returns the mock so the test can drive its
    lifecycle.

    ``monkeypatch`` is the pytest fixture, typed structurally to keep
    this module pytest-import-free.

    With ``auto_exit=True`` (default), ``terminate``/``kill`` resolve
    ``wait()`` automatically — the well-behaved-process case. Tests
    that simulate stuck processes pass ``auto_exit=False`` and call
    :meth:`MockProcess.mock_exit` explicitly.
    """
    process = MockProcess(pid=pid)
    if auto_exit:
        process.auto_exit_on_signal()

    async def _create(*_args: object, **_kwargs: object) -> MockProcess:
        return process

    monkeypatch.setattr(asyncio, "create_subprocess_exec", _create)
    return process


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
        pending = frozenset(t for t in asyncio.all_tasks() if t is not current and not t.done())
        if pending == last:
            stable += 1
            if stable >= stable_iterations:
                return
        else:
            stable = 0
            last = pending
    raise TimeoutError(
        (
            f"asyncio loop did not become idle within {timeout}s "
            f"(last pending: {sorted(t.get_name() for t in last)})"
        )
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
