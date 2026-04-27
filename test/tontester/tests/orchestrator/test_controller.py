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


async def test_manager_start_cancellation_stops_already_started_runners():
    """Round-3 finding: ``Manager.start`` only catches ``Exception``,
    not ``BaseException`` / ``CancelledError``. Cancellation between
    two ``runner.start()`` calls would leave the first runner's tasks
    running because its ``_safe_stop`` callback was registered on the
    stack but the stack's ``aclose`` wouldn't run on cancellation.

    Trigger: a runner whose start hangs (Namespace not registered → its
    watch_loop fails → ready never sets). The earlier runner started
    successfully and registered _safe_stop. Cancel the manager; verify
    the earlier runner's tasks are also cancelled.
    """
    from orchestrator import Manager

    # First controller: trivial, starts successfully.
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
    # Don't register Namespace — second controller's watch_loop will fail.
    manager.add_controller(_OkController())
    manager.add_controller(_BlockingController())

    start_task = asyncio.create_task(manager.start(), name="t.manager.start")
    await wait_for_asyncio_idle()
    # By now, the first runner has fully started (its _safe_stop was
    # pushed) and the second runner is hanging on ready.wait. Snapshot
    # all currently-running runner tasks across both runners.
    all_tasks: list[asyncio.Task[None]] = []
    for runner in manager._runners:
        all_tasks.extend(runner._tasks)
    assert all_tasks, "expected manager to have spawned runner tasks"

    _ = start_task.cancel()
    with pytest.raises((asyncio.CancelledError, Exception)):
        _ = await start_task

    # Both runners' tasks must be cleaned up — including the first
    # runner that successfully started and registered _safe_stop.
    for t in all_tasks:
        assert t.done(), f"manager-spawned task {t.get_name()} leaked through cancellation"


async def test_manager_shutdown_is_not_permanently_disabled_by_failure():
    """Bug (audit round 4): ``Manager.shutdown`` sets ``self._stopping = True``
    before ``await self._stack.aclose()``. If aclose raises (or is
    cancelled), the next ``shutdown()`` sees ``_stopping`` is True and
    returns immediately as a no-op — even though there are still
    callbacks the stack didn't get to run. A daemon that catches the
    first failure and retries shutdown silently does nothing on the
    retry, leaking whatever cleanup remained.
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
    await manager.start()

    # Inject a callback that raises BaseException — _safe_stop only
    # catches Exception, so this propagates out of aclose, mimicking
    # the cancellation/SystemExit class of failure the bug worries about.
    async def _raising_callback() -> None:
        raise SystemExit("simulated cleanup failure")

    _ = manager._stack.push_async_callback(_raising_callback)

    # Inject a sentinel that, when fired, proves a shutdown actually
    # ran the stack. Pushed after the raising callback so it runs FIRST
    # (LIFO) on a healthy retry.
    sentinel_fired = asyncio.Event()

    async def _sentinel() -> None:
        sentinel_fired.set()

    _ = manager._stack.push_async_callback(_sentinel)

    # First shutdown: sentinel fires (LIFO: pushed last → runs first),
    # then the raising callback propagates SystemExit.
    with pytest.raises(SystemExit):
        await manager.shutdown()
    assert sentinel_fired.is_set()
    sentinel_fired.clear()

    # Push a fresh sentinel and try shutdown again. Healthy: stack
    # still has callbacks; second shutdown drains what it can. Buggy:
    # _stopping=True → immediate no-op, sentinel never fires.
    _ = manager._stack.push_async_callback(_sentinel)
    await manager.shutdown()

    assert sentinel_fired.is_set(), (
        "Manager.shutdown after a failed first attempt is a permanent no-op; "
        "_stopping stays True forever, retries silently skip pending cleanup"
    )


async def test_runner_stop_propagates_external_cancellation():
    """Bug (audit round 4): ``ControllerRunner.stop`` awaits each task
    under ``except asyncio.CancelledError, Exception: pass``. When the
    *caller* cancels stop() mid-await, that cancellation manifests as
    CancelledError on the awaited task — indistinguishable from the
    task's own cancellation — and the ``pass`` swallows it. Callers
    can't time-bound shutdown via ``wait_for(stop(), timeout=...)`` or
    interrupt it with cancel().

    Verified observable behavior: cancel stop mid-flight, expect
    ``stop()`` itself to surface CancelledError instead of completing
    successfully. Under the bug, stop completes "normally" (the
    swallow turns external cancellation into a no-op) and the cancel
    request is lost.
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
    await runner.start()

    # Drop the runner's own short-lived tasks so they don't race
    # cancellation with our injected hang task. Then put a single
    # uncancellable task in their place so stop()'s drain loop is
    # parked on awaiting just it.
    for t in runner._tasks:
        _ = t.cancel()
        try:
            await t
        except asyncio.CancelledError, Exception:
            pass
    runner._tasks.clear()

    release = asyncio.Event()

    async def _hang_until_released() -> None:
        while not release.is_set():
            try:
                _ = await release.wait()
            except asyncio.CancelledError:
                continue

    hang_task = asyncio.create_task(_hang_until_released(), name="t.hang")
    runner._tasks.append(hang_task)

    stop_task = asyncio.create_task(runner.stop(), name="t.stop")
    # Let stop reach the `await task` parked on hang_task.
    for _ in range(5):
        await asyncio.sleep(0)
    _ = stop_task.cancel()
    # Give stop several scheduling opportunities to either propagate
    # the cancel or swallow it.
    for _ in range(20):
        await asyncio.sleep(0)

    try:
        # Healthy: stop_task is done after cancel propagates.
        # Buggy: stop_task is still running (swallowed the cancel and
        # remains stuck on `await hang_task`).
        assert stop_task.done(), (
            "ControllerRunner.stop did not honor external cancel — "
            "except CancelledError swallows the cancellation, leaving "
            "the stop coroutine permanently parked on its drain await"
        )
        # And the propagated exception must be CancelledError.
        with pytest.raises(asyncio.CancelledError):
            _ = stop_task.result()
    finally:
        release.set()
        try:
            await hang_task
        except asyncio.CancelledError, Exception:
            pass
        if not stop_task.done():
            _ = stop_task.cancel()
            try:
                await stop_task
            except asyncio.CancelledError, Exception:
                pass


async def test_runner_cleans_up_tasks_on_start_cancellation():
    """Round-2 finding 1.3: if start() raises after creating tasks, the
    runner must self-clean before propagating. Otherwise Manager's
    rollback can't reach the tasks (they were never registered with
    its stack), and they leak.
    """
    store = InMemoryStore()
    store.register_kind(Workload)
    # Deliberately don't register Namespace — _BlockingController watches
    # it, so the watch_loop's subscribe will fail and ready never sets.

    runner: ControllerRunner[Workload] = ControllerRunner(_BlockingController(), store=store)
    start_task = asyncio.create_task(runner.start(), name="t.runner.start")

    # Let start() create its tasks and block on ready.wait. With
    # VirtualClock + the idle helper this is instant rather than the
    # arbitrary asyncio.sleep(0.05) it used to be.
    await wait_for_asyncio_idle()
    created = list(runner._tasks)
    assert created, "expected start to have created tasks before cancellation"
    assert all(not t.done() for t in created), "tasks should be running"

    _ = start_task.cancel()
    with pytest.raises((asyncio.CancelledError, Exception)):
        _ = await start_task

    # After cancellation propagates, every task the runner created
    # must be done (cancelled or finished). Otherwise they're orphans.
    for t in created:
        assert t.done(), f"runner-created task {t.get_name()} leaked through cancellation"
