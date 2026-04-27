# pyright: reportPrivateUsage=false
"""ControllerRunner lifecycle invariants — focused on partial-start
cleanup, which round-2 audit found leaked tasks if start was cancelled
mid-await."""

import asyncio
from typing import final, override

import pytest
from orchestrator import (
    Controller,
    ControllerRunner,
    InMemoryStore,
    ItemRef,
    Namespace,
    Result,
    WatchSpec,
    Workload,
)
from orchestrator.testing import wait_for_asyncio_idle

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


@final
class _BlockingController(Controller[Workload]):
    """Controller whose watches reference an unregistered kind so
    ControllerRunner.start blocks forever on ready.wait — perfect for
    cancellation tests."""

    @property
    @override
    def owned_kind(self) -> type[Workload]:
        return Workload

    @property
    @override
    def watches(self) -> list[WatchSpec]:
        # Namespace is intentionally unregistered in the store the
        # tests build below, so the watch_loop's bus.open raises and
        # ready.set() is never reached. The mapper is never invoked
        # because subscribe fails first.
        return [WatchSpec(resource_type=Namespace, map_event=lambda event: [])]

    @override
    async def reconcile(self, store: InMemoryStore, ref: ItemRef) -> Result:
        return Result(ok=True)


async def test_runner_running_fails_fast_on_watch_setup_error():
    """Bug round-5 #3: ``ControllerRunner._watch_loop``'s setup
    (``store.subscription(...)``) can raise — e.g. ``ValidationError``
    if the watched kind isn't registered. The loop's broad
    ``except Exception`` arm logs and re-loops, never setting the
    ``ready`` event. ``running()`` blocks on ``await ready.wait()``
    forever, instead of failing fast with the configuration error.

    Pin the contract: a setup failure surfaces to the caller of
    ``async with runner.running():`` and every spawned task is
    cleaned up by TaskGroup unwind.
    """
    store = InMemoryStore()
    store.register_kind(Workload)
    # Deliberately don't register Namespace — _BlockingController watches
    # it; the watch_loop's first subscription call will fail.

    runner: ControllerRunner[Workload] = ControllerRunner(_BlockingController(), store=store)

    spawned_before_failure: list[asyncio.Task[object]] = []

    async def _try_enter() -> None:
        nonlocal spawned_before_failure
        async with runner.running():
            spawned_before_failure = [
                t for t in asyncio.all_tasks() if t.get_name().startswith(("watch[", "reconcile["))
            ]
            pytest.fail("running() should not have yielded; setup must have failed")

    with pytest.raises(BaseException) as exc_info:
        await _try_enter()

    # The original ValidationError must be reachable in the exception chain
    # (TaskGroup wraps body exceptions in ExceptionGroup).
    flat: list[BaseException] = []

    def _flatten(e: BaseException) -> None:
        if isinstance(e, BaseExceptionGroup):
            for sub in e.exceptions:
                _flatten(sub)
        else:
            flat.append(e)

    _flatten(exc_info.value)
    from orchestrator import ValidationError

    assert any(isinstance(e, ValidationError) for e in flat), (
        f"expected ValidationError in exception chain, got {[type(e).__name__ for e in flat]}"
    )
    # And every task spawned before the failure must be done — TaskGroup
    # unwind doesn't leak.
    for t in spawned_before_failure:
        assert t.done(), f"task {t.get_name()} leaked through fail-fast unwind"


async def test_manager_running_cancellation_stops_already_started_runners():
    """Round-3 finding (recast): cancellation during ``async with
    manager.running()`` — while inside the body, after entry — must
    tear down every runner's tasks. AsyncExitStack guarantees LIFO
    cleanup; verify nothing leaks.
    """
    from orchestrator import Manager

    @final
    class _OkController(Controller[Workload]):
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

    manager = Manager()
    manager.register_kind(Workload)
    manager.add_controller(_OkController())
    manager.add_controller(_OkController())

    spawned: list[asyncio.Task[object]] = []

    async def _enter_then_yield() -> None:
        nonlocal spawned
        async with manager.running():
            spawned = [
                t for t in asyncio.all_tasks() if t.get_name().startswith(("watch[", "reconcile["))
            ]
            assert spawned, "expected manager to have spawned runner tasks"
            _ = await asyncio.Event().wait()

    enter_task = asyncio.create_task(_enter_then_yield(), name="t.manager.enter")
    await wait_for_asyncio_idle()

    _ = enter_task.cancel()
    with pytest.raises((asyncio.CancelledError, Exception)):
        await enter_task

    for t in spawned:
        assert t.done(), f"manager-spawned task {t.get_name()} leaked through cancellation"


async def test_manager_running_propagates_internal_failures():
    """Bug 7 (audit round 4) — original shape: ``Manager.shutdown``
    set ``_stopping=True`` before ``aclose``, leaving the manager
    permanently un-shutdownable on aclose failure.

    The fix is structural: there is no longer a ``shutdown()`` method
    or ``_stopping`` flag. ``Manager.running()`` is an
    ``AsyncExitStack``-based async context manager. Failure during
    entry rolls back via the stack's own ``__aexit__``; failure on
    exit propagates the exception to the caller's ``async with``.
    There is no in-between state to corrupt.

    This test pins the new contract: an exception raised by code
    inside ``async with manager.running()`` propagates out, and
    every cleanup callback the stack accumulated still runs.
    """
    from orchestrator import Manager

    @final
    class _OkController(Controller[Workload]):
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

    manager = Manager()
    manager.register_kind(Workload)
    manager.add_controller(_OkController())

    # ``asyncio.TaskGroup`` (used inside each runner's running())
    # wraps body exceptions in ``BaseExceptionGroup`` per its
    # documented contract; the original is reachable via ``.split``.
    with pytest.raises(BaseExceptionGroup) as exc_info:
        async with manager.running():
            raise RuntimeError("boom")
    runtime_errors, _ = exc_info.value.split(RuntimeError)
    assert runtime_errors is not None
    assert any("boom" in str(e) for e in runtime_errors.exceptions)

    # After the failed running() exit, everything is back to the
    # pre-running() state — re-entering must work, not no-op.
    async with manager.running():
        pass


async def test_runner_running_propagates_external_cancellation():
    """Bug 6 (audit round 4) — original shape: ``ControllerRunner.stop``
    awaited tasks under ``except CancelledError, Exception: pass``,
    swallowing external cancellation of stop() itself.

    The fix is structural: there is no ``stop()`` method to swallow
    anything. ``ControllerRunner.running()`` uses ``asyncio.TaskGroup``;
    cancelling a task that's awaiting the ``async with`` propagates
    correctly because that's TaskGroup's own contract. Real loops
    (watch + worker) honor cancel — the worker exits via
    ``QueueClosed`` and the watch exits via ``CancelledError`` — so
    the cancel completes promptly.
    """
    store = InMemoryStore()
    store.register_kind(Workload)

    @final
    class _IdleController(Controller[Workload]):
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

    runner: ControllerRunner[Workload] = ControllerRunner(_IdleController(), store=store)

    async def _hold() -> None:
        async with runner.running():
            _ = await asyncio.Event().wait()

    hold_task = asyncio.create_task(_hold(), name="t.hold")
    # Let running() finish entry (subscriptions registered, ready set).
    await wait_for_asyncio_idle()

    _ = hold_task.cancel()
    # Healthy: cancel propagates promptly through TaskGroup → __aexit__.
    # Buggy: would hang indefinitely if running() swallowed CancelledError.
    with pytest.raises(asyncio.CancelledError):
        await hold_task
