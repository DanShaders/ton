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
    """Resource-shape contract: a watch_loop's setup error
    (``store.subscription(...)`` raising, e.g. ``ValidationError``
    on an unregistered kind) propagates out of ``async with
    runner.running()`` directly — no ExceptionGroup wrap — and
    every spawned task gets cancelled by the runner's setup-failure
    cleanup path.

    Pre-fix (round-5 #3): the loop's broad ``except Exception``
    arm logged and re-looped, never setting the ``ready`` future.
    ``running()`` blocked on ``await ready.wait()`` forever.

    Post-Resource-shape: ``_watch_loop`` calls
    ``ready.set_exception`` before re-raising, so the ``await ready``
    in ``running()`` re-raises the original error. Before the
    re-raise propagates, every spawned task is cancelled.
    """
    from orchestrator import ValidationError

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

    with pytest.raises(ValidationError):
        await _try_enter()

    # The runner's setup-failure path cancels every spawned task before
    # the exception propagates. Yield once so the cancellations land.
    await asyncio.sleep(0)
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
    """Resource-shape contract: a body exception inside
    ``async with manager.running()`` propagates out unchanged
    (no TaskGroup wrap), and every sub-resource's sync ``__aexit__``
    still runs as the :class:`CheckedExitStack` unwinds.

    Pre-fix (audit round 4): ``Manager.shutdown`` flipped a
    ``_stopping=True`` flag before tear-down, leaving the manager
    permanently un-shutdownable on tear-down failure. The Resource
    Protocol eliminates that: no flag, no shutdown-before-running
    sequencing — the body exception just falls through the stack's
    LIFO unwind.

    Manager is one-shot per the Resource discipline: re-entering
    ``running()`` after exit is not supported (its ``stop_token``
    has fired).
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

    with pytest.raises(RuntimeError, match="boom"):
        async with manager.running():
            raise RuntimeError("boom")

    # After the body exception, the manager's stop_token is set and
    # _is_running is False — the runner's sync __aexit__ ran during
    # CheckedExitStack unwind, so its tasks were cancelled.
    assert manager.stop_token.is_set
    assert not manager._is_running


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
