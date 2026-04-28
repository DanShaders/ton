# pyright: reportPrivateUsage=false
"""Cross-cutting Resource Protocol invariants.

The Resource Protocol's contract spans a small set of properties
that every concrete impl must satisfy:

1. ``shutdown()`` outside ``running()`` raises :exc:`ResourceNotRunning`.
2. ``shutdown()`` after ``running()`` exits raises
   :exc:`ResourceNotRunning`.
3. ``running()`` cannot be re-entered while already running.
4. ``stop_token`` is set once ``running()`` exits.
5. ``parent_token`` cascades: setting it cancels the resource's
   ``stop_token`` too.
6. ``shutdown()`` cancelled mid-flight (``wait_for``-style or
   external ``task.cancel``) still leaves the resource releasable
   via the surrounding ``__aexit__``.

Each property is exercised against multiple concrete Resources to
catch divergence (e.g. one impl forgets to flip ``_is_running``,
another swallows cancel).

The :func:`virtual_clock` fixture is on for everything that doesn't
touch a real subprocess; it makes ``wait_for`` timeouts instant
without sacrificing determinism.
"""

import asyncio
import contextlib
from collections.abc import AsyncGenerator
from pathlib import Path
from typing import Self, final, override

import pytest
from orchestrator import (
    Agent,
    Controller,
    ControllerRunner,
    FakeRuntime,
    InMemoryStore,
    ItemRef,
    Manager,
    Result,
    Runtime,
    RuntimeEvent,
    WatchSpec,
    Workload,
    WorkloadStatus,
)
from orchestrator.broadcast import BroadcastQueue
from orchestrator.lifecycle import ResourceNotRunning, StopToken
from orchestrator.testing import wait_for_asyncio_idle

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


# ---- helpers --------------------------------------------------------------


@final
class _IdleController(Controller[Workload]):
    """A no-op reconciler. Used when we just need a runner that runs."""

    @property
    @override
    def owned_kind(self) -> type[Workload]:
        return Workload

    @property
    @override
    def watches(self) -> list[WatchSpec]:
        return []

    @override
    async def reconcile(self, store: InMemoryStore, ref: ItemRef) -> Result:
        return Result(ok=True)


# ---- 1. shutdown outside running() raises -------------------------------


async def test_fake_runtime_shutdown_outside_running_raises():
    rt = FakeRuntime()
    with pytest.raises(ResourceNotRunning):
        await rt.shutdown()


async def test_controller_runner_shutdown_outside_running_raises():
    store = InMemoryStore()
    store.register_kind(Workload)
    runner: ControllerRunner[Workload] = ControllerRunner(_IdleController(), store=store)
    with pytest.raises(ResourceNotRunning):
        await runner.shutdown()


async def test_manager_shutdown_outside_running_raises():
    m = Manager()
    m.register_kind(Workload)
    with pytest.raises(ResourceNotRunning):
        await m.shutdown()


async def test_agent_shutdown_outside_running_raises(tmp_path: Path):
    rt = FakeRuntime()
    store = InMemoryStore()
    store.register_kind(Workload)
    agent = Agent(rt, store=store, state_dir=tmp_path / "agent")
    with pytest.raises(ResourceNotRunning):
        await agent.shutdown()


# ---- 2. shutdown after exit raises --------------------------------------


async def test_fake_runtime_shutdown_after_exit_raises():
    rt = FakeRuntime()
    async with rt.running():
        pass
    with pytest.raises(ResourceNotRunning):
        await rt.shutdown()


async def test_controller_runner_shutdown_after_exit_raises():
    store = InMemoryStore()
    store.register_kind(Workload)
    runner: ControllerRunner[Workload] = ControllerRunner(_IdleController(), store=store)
    async with runner.running():
        pass
    with pytest.raises(ResourceNotRunning):
        await runner.shutdown()


async def test_manager_shutdown_after_exit_raises():
    m = Manager()
    m.register_kind(Workload)
    m.add_controller(_IdleController())
    async with m.running():
        pass
    with pytest.raises(ResourceNotRunning):
        await m.shutdown()


# ---- 3. running() re-entry raises ---------------------------------------


async def test_fake_runtime_running_reentry_raises():
    """Round-7 H4: every other Resource impl checks
    ``if self._is_running: raise ResourceNotRunning(...)``;
    ``FakeRuntime`` was missing the guard. Concurrent
    ``async with fake.running():`` blocks would silently corrupt
    state.
    """
    rt = FakeRuntime()
    async with rt.running():
        with pytest.raises(ResourceNotRunning):
            async with rt.running():
                pass


async def test_controller_runner_running_reentry_raises():
    store = InMemoryStore()
    store.register_kind(Workload)
    runner: ControllerRunner[Workload] = ControllerRunner(_IdleController(), store=store)
    async with runner.running():
        with pytest.raises(ResourceNotRunning):
            async with runner.running():
                pass


async def test_manager_running_reentry_raises():
    m = Manager()
    m.register_kind(Workload)
    async with m.running():
        with pytest.raises(ResourceNotRunning):
            async with m.running():
                pass


async def test_agent_running_reentry_raises(tmp_path: Path):
    rt = FakeRuntime()
    store = InMemoryStore()
    store.register_kind(Workload)
    agent = Agent(rt, store=store, state_dir=tmp_path / "agent")
    async with agent.running():
        with pytest.raises(ResourceNotRunning):
            async with agent.running():
                pass


# ---- 4. stop_token set after exit ---------------------------------------


async def test_fake_runtime_stop_token_set_after_exit():
    rt = FakeRuntime()
    assert not rt.stop_token.is_set
    async with rt.running():
        assert not rt.stop_token.is_set
    assert rt.stop_token.is_set


async def test_manager_stop_token_set_after_exit():
    m = Manager()
    m.register_kind(Workload)
    m.add_controller(_IdleController())
    async with m.running():
        assert not m.stop_token.is_set
    assert m.stop_token.is_set


# ---- 5. parent_token cascade --------------------------------------------


async def test_runtime_parent_token_cascades_to_stop_token():
    """Setting the parent before constructing a child propagates the
    set state into the child's token at construction time."""
    parent = StopToken()
    parent.set()
    rt = FakeRuntime(parent_token=parent)
    assert rt.stop_token.is_set


async def test_manager_stop_token_cascades_to_runners():
    """Manager passes its stop_token as parent_token to each runner.
    Setting the manager's token at construction propagates."""
    parent = StopToken()
    m = Manager(parent_token=parent)
    m.register_kind(Workload)
    m.add_controller(_IdleController())
    parent.set()
    assert m.stop_token.is_set
    # Every runner constructed via add_controller should see the set state.
    for runner in m._runners:
        assert runner.stop_token.is_set


async def test_runtime_runtime_event_token_cascades_post_construction():
    """Setting parent after child construction also propagates
    (StopToken's child weakref-set design)."""
    parent = StopToken()
    rt = FakeRuntime(parent_token=parent)
    assert not rt.stop_token.is_set
    parent.set()
    assert rt.stop_token.is_set


# ---- 6. cancel during shutdown — supervisor-level half-cancellation -----


@final
class _UncancellableController(Controller[Workload]):
    """Reconciler that swallows the first cancel and keeps awaiting,
    so a worker task genuinely doesn't finish until ``release_done``
    is set. Used to make ``runner.shutdown()`` actually block long
    enough for an external cancel to land mid-await.
    """

    def __init__(self, *, started: asyncio.Event, release_done: asyncio.Event):
        self._started: asyncio.Event = started
        self._release_done: asyncio.Event = release_done

    @property
    @override
    def owned_kind(self) -> type[Workload]:
        return Workload

    @property
    @override
    def watches(self) -> list[WatchSpec]:
        return []

    @override
    async def reconcile(self, store: InMemoryStore, ref: ItemRef) -> Result:
        self._started.set()
        # Shield the inner await so the first cancel doesn't kill us.
        # The worker keeps awaiting until release_done is set; only
        # then does the wrapping CancelledError propagate.
        with contextlib.suppress(asyncio.CancelledError):
            _ = await asyncio.shield(self._release_done.wait())
        _ = await self._release_done.wait()
        return Result(ok=True)


async def test_controller_runner_shutdown_cancellation_completes_via_aexit():
    """Caller cancels the task running ``runner.shutdown()`` while
    the runner is awaiting in-flight worker tasks. The runner's
    surrounding ``__aexit__`` (sync) reconciles: re-cancels tasks
    (idempotent), re-closes queue (idempotent), sets stop_token.
    After the with-block exits, every spawned task is done.
    """
    store = InMemoryStore()
    store.register_kind(Workload)
    started = asyncio.Event()
    release_done = asyncio.Event()
    runner: ControllerRunner[Workload] = ControllerRunner(
        _UncancellableController(started=started, release_done=release_done), store=store
    )

    captured_tasks: list[asyncio.Task[object]] = []

    async with runner.running():
        runner.queue.add(ItemRef(namespace="default", name="x"))
        _ = await asyncio.wait_for(started.wait(), timeout=2.0)
        captured_tasks = [
            t
            for t in asyncio.all_tasks()
            if t.get_name().startswith(("watch[", "reconcile[", "reconcile-call["))
        ]
        assert captured_tasks, "expected runner-spawned tasks to exist"

        # Spawn shutdown as its own task; cancel after one yield so
        # the gather has actually started awaiting the parked worker.
        shutdown_task = asyncio.create_task(runner.shutdown(), name="t.shutdown")
        await wait_for_asyncio_idle()
        # State at this point: shutdown's sync prologue ran (token
        # set, queue closed, tasks cancelled); the worker swallowed
        # its first cancel and is back parked in release_done.wait.
        # Now cancel shutdown itself.
        _ = shutdown_task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await shutdown_task

        # Free the worker so the post-aexit reap can finish it.
        release_done.set()

    # After the with-block exits, every spawned task is done — even
    # though shutdown was cancelled, the sync __aexit__ re-cancelled
    # and the post-aexit scheduler reaped them.
    await wait_for_asyncio_idle()
    for t in captured_tasks:
        assert t.done(), f"task {t.get_name()} survived shutdown cancel + __aexit__"
    assert runner.stop_token.is_set


async def test_manager_shutdown_cancellation_still_releases_runners():
    """Cancel ``manager.shutdown()`` while it's awaiting one of the
    runners. The remaining runners are released by the manager's
    sync ``__aexit__`` via the CheckedExitStack unwind.
    """
    m = Manager()
    m.register_kind(Workload)
    started_a = asyncio.Event()
    release_done_a = asyncio.Event()
    m.add_controller(_UncancellableController(started=started_a, release_done=release_done_a))
    m.add_controller(_IdleController())

    captured_tasks: list[asyncio.Task[object]] = []
    async with m.running():
        m._runners[0].queue.add(ItemRef(namespace="default", name="x"))
        _ = await asyncio.wait_for(started_a.wait(), timeout=2.0)
        captured_tasks = [
            t
            for t in asyncio.all_tasks()
            if t.get_name().startswith(("watch[", "reconcile[", "reconcile-call["))
        ]
        shutdown_task = asyncio.create_task(m.shutdown(), name="t.shutdown")
        await wait_for_asyncio_idle()
        _ = shutdown_task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await shutdown_task
        release_done_a.set()

    # Manager's __aexit__ ran the stack unwind → both runners' sync
    # __aexit__ fired → all tasks cancelled. The post-aexit reap
    # finishes them.
    await wait_for_asyncio_idle()
    for t in captured_tasks:
        assert t.done(), f"task {t.get_name()} survived cancelled manager.shutdown"
    assert m.stop_token.is_set


async def test_agent_shutdown_cancellation_still_releases_subresources(tmp_path: Path):
    """Agent.shutdown() awaits runner → runtime → event task in
    sequence. Cancelling shutdown still releases every sub-resource
    via the agent's sync __aexit__ + the stack unwind.
    """
    rt = FakeRuntime()
    store = InMemoryStore()
    store.register_kind(Workload)
    agent = Agent(rt, store=store, state_dir=tmp_path / "agent")

    captured_tasks: list[asyncio.Task[object]] = []
    async with agent.running():
        captured_tasks = [
            t
            for t in asyncio.all_tasks()
            if t.get_name().startswith(("agent.", "watch[", "reconcile[", "reconcile-call["))
        ]
        assert captured_tasks
        shutdown_task = asyncio.create_task(agent.shutdown(), name="t.shutdown")
        await wait_for_asyncio_idle()
        _ = shutdown_task.cancel()
        # Either CancelledError propagates (cancel won the race) or
        # shutdown completed successfully before the cancel landed.
        with contextlib.suppress(asyncio.CancelledError):
            await shutdown_task

    await wait_for_asyncio_idle()
    for t in captured_tasks:
        assert t.done(), f"task {t.get_name()} survived agent shutdown + __aexit__"
    assert agent.stop_token.is_set


# ---- 7. shutdown is idempotent under multiple calls ---------------------


async def test_fake_runtime_shutdown_idempotent_inside_running():
    """Shutdown can be called multiple times inside running() without
    raising — it's a sync-signal operation (set stop_token), and
    the second call observes the same already-True state."""
    rt = FakeRuntime()
    async with rt.running():
        await rt.shutdown()
        await rt.shutdown()
        await rt.shutdown()
    assert rt.stop_token.is_set


async def test_controller_runner_shutdown_idempotent_inside_running():
    store = InMemoryStore()
    store.register_kind(Workload)
    runner: ControllerRunner[Workload] = ControllerRunner(_IdleController(), store=store)
    async with runner.running():
        await runner.shutdown()
        # Second call: stop_token already set, queue already closed,
        # tasks already cancelled. All idempotent.
        await runner.shutdown()


# ---- 8. runtime hosting agent — composed cascade ------------------------


@final
class _ProbeRuntime(Runtime):
    """Runtime that records when its lifecycle methods are called
    so cascade tests can verify Agent → Runtime delegation."""

    def __init__(self) -> None:
        self._silent: BroadcastQueue[RuntimeEvent] = BroadcastQueue("probe")
        self._stop_token: StopToken = StopToken()
        self.shutdown_called: int = 0
        self.aexit_called: int = 0

    @property
    @override
    def stop_token(self) -> StopToken:
        return self._stop_token

    @override
    async def apply(self, workload: Workload) -> WorkloadStatus:
        return WorkloadStatus()

    @override
    async def delete(self, *, namespace: str | None, name: str) -> None:
        return

    @override
    async def get(self, *, namespace: str | None, name: str) -> WorkloadStatus | None:
        return None

    @override
    async def list(self) -> list[WorkloadStatus]:
        return []

    @override
    def watch(self):
        return self._silent.subscribe()

    @override
    async def shutdown(self) -> None:
        self.shutdown_called += 1
        self._stop_token.set()

    @override
    @contextlib.asynccontextmanager
    async def running(self) -> AsyncGenerator[Self]:
        try:
            yield self
        finally:
            self.aexit_called += 1
            self._silent.close()
            self._stop_token.set()


async def test_agent_graceful_shutdown_calls_runtime_shutdown_then_aexit(tmp_path: Path):
    """When agent.shutdown() runs to completion, runtime.shutdown()
    is called once (graceful), then the runtime's __aexit__ runs
    once on the agent's stack unwind. Establishes ordering.
    """
    rt = _ProbeRuntime()
    store = InMemoryStore()
    store.register_kind(Workload)
    agent = Agent(rt, store=store, state_dir=tmp_path / "agent")

    async with agent.running():
        await agent.shutdown()
        # shutdown was called; aexit hasn't run yet (still inside with).
        assert rt.shutdown_called == 1
        assert rt.aexit_called == 0
    # After with-block exits, the runtime's __aexit__ has fired.
    assert rt.aexit_called == 1


async def test_running_with_pre_set_stop_token_raises_resource_not_running():
    """If a Resource's stop_token is already set when ``running()``
    is entered, the first CheckedExitStack push raises
    ``GracefulAbort``. The Resource's ``except`` arm should convert
    that to ``ResourceNotRunning`` (semantic) — *not* let the
    @asynccontextmanager machinery surface a confusing
    ``RuntimeError("generator didn't yield")``.

    Pre-fix (audit H4): the except did ``pass``, generator returned
    without yielding, caller saw ``RuntimeError``.
    """
    parent = StopToken()
    parent.set()
    m = Manager(parent_token=parent)
    m.register_kind(Workload)
    m.add_controller(_IdleController())
    with pytest.raises(ResourceNotRunning):
        async with m.running():
            pytest.fail("body should not run when stop_token was pre-set")


async def test_cancel_and_collect_propagates_outer_cancel():
    """Helper contract: when the calling task is itself cancel-pending,
    awaiting an inner-cancelled task must propagate the outer cancel
    rather than swallow it.

    Setup: pre-cancel the calling task before it enters
    ``cancel_and_collect``. The helper's ``await task`` then raises
    CancelledError because the calling task is already cancel-pending;
    ``current.cancelling() > 0`` triggers the re-raise.
    """
    from orchestrator.lifecycle import cancel_and_collect

    inner_started = asyncio.Event()

    async def _inner() -> None:
        inner_started.set()
        _ = await asyncio.Event().wait()

    async def _outer() -> None:
        inner = asyncio.create_task(_inner(), name="t.inner")
        _ = await inner_started.wait()
        # Self-cancel so cancelling() > 0 when the helper enters its
        # await. This simulates "an outer caller cancelled us between
        # iterations of a cancel-and-await loop."
        current = asyncio.current_task()
        assert current is not None
        _ = current.cancel()
        await cancel_and_collect(inner)

    outer = asyncio.create_task(_outer(), name="t.outer")
    with pytest.raises(asyncio.CancelledError):
        await outer


async def test_cancel_and_collect_swallows_inner_cancel():
    """Happy path: when only the inner task's cancel is in flight
    (the calling task is not itself cancelled), the helper swallows
    the inner CancelledError and returns normally.
    """
    from orchestrator.lifecycle import cancel_and_collect

    inner_started = asyncio.Event()

    async def _inner() -> None:
        inner_started.set()
        _ = await asyncio.Event().wait()

    inner = asyncio.create_task(_inner(), name="t.inner")
    _ = await asyncio.wait_for(inner_started.wait(), timeout=2.0)

    # Outer (this test task) is not cancelled. The helper should
    # cancel inner, swallow inner's CancelledError, and return.
    await cancel_and_collect(inner)
    assert inner.done()


async def test_cancel_and_collect_swallows_inner_exception():
    """If the inner task raises a regular Exception, the helper
    swallows it (we're tearing down; we don't care about its
    natural failure)."""
    from orchestrator.lifecycle import cancel_and_collect

    async def _inner() -> None:
        raise RuntimeError("inner crashed")

    inner = asyncio.create_task(_inner(), name="t.inner")
    # Let it crash before we try to cancel-and-collect.
    await wait_for_asyncio_idle()
    assert inner.done()
    # Helper handles already-done task + caught its exception.
    await cancel_and_collect(inner)


async def test_cancel_and_collect_done_task_is_noop():
    """An already-done task is a clean no-op. No raise."""
    from orchestrator.lifecycle import cancel_and_collect

    async def _quick() -> None:
        return

    inner = asyncio.create_task(_quick(), name="t.quick")
    _ = await inner
    assert inner.done()
    await cancel_and_collect(inner)


async def test_agent_running_fails_fast_on_persistent_runtime_failure(tmp_path: Path):
    """If the runtime's ``watch()`` raises every time, ``Agent.running()``
    must surface the failure rather than busy-loop forever waiting
    for ``runtime_ready``.

    Pre-fix (M2): ``_runtime_event_loop`` catches ``Exception``, sleeps
    1s, retries forever — ``runtime_ready`` (an asyncio.Event) is
    never set, ``Agent.running().__aenter__`` blocks on
    ``runtime_ready.wait()`` indefinitely. Wrapping with
    ``asyncio.timeout`` would surface ``TimeoutError``, not the
    underlying RuntimeError.

    Post-fix: setup failures (``ready`` not yet signalled) propagate
    the exception via the ready future, mirroring
    ``ControllerRunner._watch_loop``'s pattern.
    """

    @final
    class _BrokenRuntime(Runtime):
        def __init__(self) -> None:
            self._stop_token: StopToken = StopToken()

        @property
        @override
        def stop_token(self) -> StopToken:
            return self._stop_token

        @override
        async def apply(self, workload: Workload) -> WorkloadStatus:
            return WorkloadStatus()

        @override
        async def delete(self, *, namespace: str | None, name: str) -> None:
            return

        @override
        async def get(self, *, namespace: str | None, name: str) -> WorkloadStatus | None:
            return None

        @override
        async def list(self) -> list[WorkloadStatus]:
            return []

        @override
        def watch(self):
            raise RuntimeError("watch is permanently broken")

        @override
        @contextlib.asynccontextmanager
        async def running(self) -> AsyncGenerator[Self]:
            try:
                yield self
            finally:
                self._stop_token.set()

        @override
        async def shutdown(self) -> None:
            self._stop_token.set()

    rt = _BrokenRuntime()
    store = InMemoryStore()
    store.register_kind(Workload)
    agent = Agent(rt, store=store, state_dir=tmp_path / "agent")

    # The original RuntimeError must surface — not a TimeoutError, not
    # an indefinite hang.
    with pytest.raises(RuntimeError, match="broken"):
        async with asyncio.timeout(60.0):
            async with agent.running():
                pytest.fail("should not enter the body")


async def test_manager_shutdown_continues_when_one_runner_raises(
    monkeypatch: pytest.MonkeyPatch,
):
    """If one runner's ``shutdown()`` raises a non-ResourceNotRunning
    exception, ``Manager.shutdown`` must log it and proceed to drain
    the remaining runners — not bail out leaving them undrained.

    Pre-fix: the loop catches only ResourceNotRunning; other
    exceptions propagate, skipping the rest of the runners.
    """
    m = Manager()
    m.register_kind(Workload)
    m.add_controller(_IdleController())  # runner_b — added first
    m.add_controller(_IdleController())  # runner_a — drained first via reversed()

    runner_a = m._runners[1]
    runner_b = m._runners[0]
    b_shutdown_called = False

    async def _a_raises() -> None:
        raise RuntimeError("a's shutdown failed deliberately")

    original_b_shutdown = runner_b.shutdown

    async def _b_records() -> None:
        nonlocal b_shutdown_called
        b_shutdown_called = True
        await original_b_shutdown()

    monkeypatch.setattr(runner_a, "shutdown", _a_raises)
    monkeypatch.setattr(runner_b, "shutdown", _b_records)

    async with m.running():
        # Should not raise. After the fix, the RuntimeError from a's
        # shutdown gets logged-and-swallowed; b is still drained.
        await m.shutdown()

    assert b_shutdown_called, (
        "runner_b's shutdown must have been called even after runner_a's "
        "shutdown raised — Manager.shutdown should log-and-continue"
    )


async def test_agent_forceful_exit_skips_runtime_shutdown(tmp_path: Path):
    """If the agent's running() exits without shutdown(), runtime's
    __aexit__ still fires (via stack unwind) but shutdown() is
    never called. This pins the "sync signals, no async drain"
    forceful path.
    """
    rt = _ProbeRuntime()
    store = InMemoryStore()
    store.register_kind(Workload)
    agent = Agent(rt, store=store, state_dir=tmp_path / "agent")

    async with agent.running():
        pass

    assert rt.shutdown_called == 0, (
        "forceful exit should not call shutdown; only the sync __aexit__ runs"
    )
    assert rt.aexit_called == 1
