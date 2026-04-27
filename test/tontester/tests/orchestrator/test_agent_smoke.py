"""End-to-end smoke: Manager + Agent + FakeRuntime.

Apply a Workload via the store; the agent reflects desired into the
runtime, then writes status back. Tests the full reconcile loop with
no real subprocess.

Waits use the store's watch primitive (see
:func:`orchestrator.testing.wait_for_event` /
:func:`orchestrator.testing.wait_for_state`) — never poll-and-sleep,
which would race the agent's async patch_status path and only pretend
to know when the agent is done.
"""

import asyncio
from collections.abc import AsyncIterator
from datetime import datetime, timezone
from pathlib import Path
from typing import final, override

import pytest
from orchestrator import (
    Agent,
    Condition,
    Container,
    FakeRuntime,
    HostBinaryImage,
    InMemoryStore,
    Manager,
    Metadata,
    Runtime,
    RuntimeEvent,
    WatchEventType,
    Workload,
    WorkloadSpec,
    WorkloadStatus,
)
from orchestrator.resources import upsert_condition
from orchestrator.testing import wait_for_asyncio_idle, wait_for_event, wait_for_state

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


@final
class _HangRuntime(Runtime):
    """Minimal runtime whose ``watch()`` registers (yields the
    sentinel) but never produces a real event. Used to make
    ``Agent.start`` block on ``_runtime_ready.wait`` so we can test
    cancellation cleanup."""

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
    def watch(self) -> AsyncIterator[RuntimeEvent | None]:
        async def _hang() -> AsyncIterator[RuntimeEvent | None]:
            # Hang forever — never yields the registration sentinel,
            # so ``Agent._runtime_ready`` stays unset and ``start()``
            # blocks on it. Perfect setup for a cancellation test.
            _ = await asyncio.Event().wait()
            yield None  # unreachable

        return _hang()

    @override
    async def close(self) -> None:
        return


def _wl(name: str) -> Workload:
    return Workload(
        metadata=Metadata(name=name, namespace="default"),
        spec=WorkloadSpec(
            containers=[Container(name="primary", image=HostBinaryImage(path="/bin/true"), env={})],
        ),
    )


async def test_apply_drives_runtime_and_status(
    agent_pair: tuple[Manager, Agent],
    fake_runtime: FakeRuntime,
):
    manager, _agent = agent_pair
    _ = await manager.store.apply(_wl("alpha"))

    # Wait for the agent to apply the workload and write status back.
    # ``patch_status`` fires a MODIFIED event we observe via the bus.
    _ = await wait_for_event(
        manager.store,
        Workload,
        lambda w: w.metadata.name == "alpha" and w.status.phase == "Running",
    )

    # The runtime saw the apply.
    assert any(w.metadata.name == "alpha" for w in fake_runtime.applies)


async def test_delete_propagates_through_finalizer(
    agent_pair: tuple[Manager, Agent],
    fake_runtime: FakeRuntime,
):
    manager, _agent = agent_pair
    _ = await manager.store.apply(_wl("bravo"))

    # Wait until the agent installs its finalizer (a MODIFIED event
    # from the agent's patch_metadata).
    _ = await wait_for_event(
        manager.store,
        Workload,
        lambda w: w.metadata.name == "bravo" and bool(w.metadata.finalizers),
    )

    # Open the DELETED subscription BEFORE issuing delete: the bus
    # registers synchronously, so the DELETED event published when the
    # agent drops its finalizer (which only happens after runtime.stop
    # completes) is observed inside this with-block.
    async with manager.store.subscription(Workload) as sub:
        await manager.store.delete(Workload, namespace="default", name="bravo")

        async def _await_hard_delete() -> None:
            async for event in sub:
                if event.type == WatchEventType.DELETED and event.resource.metadata.name == "bravo":
                    return
            raise RuntimeError("Workload subscription closed before bravo was hard-deleted")

        await asyncio.wait_for(_await_hard_delete(), timeout=2.0)

    assert ("default", "bravo") in fake_runtime.deletes


async def test_external_reconciled_condition_does_not_skip_agent_apply(
    agent_pair: tuple[Manager, Agent],
    fake_runtime: FakeRuntime,
):
    """Round-2 finding 5.1: ``_already_reconciled`` checks for any
    condition with type=Reconciled / status=True / matching generation.
    A non-agent writer of that condition (e.g. a future controller, or
    a test fixture) would silently disable the agent. Guard by reason.
    """
    manager, _agent = agent_pair
    _ = await manager.store.apply(_wl("charlie"))

    # Wait for the agent's first reconcile to complete (Running phase).
    _ = await wait_for_event(
        manager.store,
        Workload,
        lambda w: w.metadata.name == "charlie" and w.status.phase == "Running",
    )

    # Bump the spec to force a new generation, then *before* the agent
    # picks up the change, externally write a Reconciled=True condition
    # for the new generation but with a foreign reason. The agent must
    # still re-apply (because the reason isn't its own).
    fake_runtime.applies.clear()
    new_wl = _wl("charlie")
    new_wl.spec.containers[0].env = {"FOO": "bar"}  # spec change
    applied = await manager.store.apply(new_wl)
    new_gen = applied.metadata.generation

    def _spoof(w: Workload) -> None:
        w.status.conditions = upsert_condition(
            w.status.conditions,
            Condition(
                type="Reconciled",
                status="True",
                reason="OtherSource",
                message="from a non-agent writer",
                last_transition=datetime.now(timezone.utc),
                observed_generation=new_gen,
            ),
        )

    _ = await manager.store.patch_status(
        Workload, namespace="default", name="charlie", mutator=_spoof
    )

    # Agent should still re-apply because the foreign Reconciled
    # condition doesn't match the agent's reason. Wait for the apply
    # to land in the FakeRuntime — observed indirectly via the agent's
    # follow-up patch_status with the new env reflected in the live
    # spec.
    await wait_for_state(
        manager.store,
        Workload,
        lambda: any(
            w.metadata.name == "charlie" and w.spec.containers[0].env.get("FOO") == "bar"
            for w in fake_runtime.applies
        ),
    )


async def test_runtime_event_loop_does_not_falsely_mark_reconciled(
    agent_pair: tuple[Manager, Agent],
):
    """Caught while debugging the test above: the runtime event loop
    used to call ``_write_status(reconciled=True)``. Combined with
    asyncio scheduling letting the runtime event loop process a stale
    STARTED *after* a spec bump, that wrote ``Reconciled=True`` with
    ``observed_generation = current.metadata.generation`` (the *new*
    generation) — claiming the new spec was reconciled before the
    desired loop had even seen it. ``_already_reconciled`` would then
    return True for the new generation and skip applying it.

    Only the desired loop should write Reconciled, since only it
    knows what generation it actually applied.
    """
    manager, _agent = agent_pair
    _ = await manager.store.apply(_wl("delta"))
    # Wait for first reconcile to complete fully (Reconciled+ApplyOk
    # written for gen=1 and Available written by runtime event loop).
    _ = await wait_for_event(
        manager.store,
        Workload,
        lambda w: (
            w.metadata.name == "delta"
            and any(
                c.type == "Reconciled" and c.status == "True" and c.observed_generation == 1
                for c in w.status.conditions
            )
            and any(c.type == "Available" for c in w.status.conditions)
        ),
    )

    # Now any Available condition the runtime event loop writes from a
    # stale STARTED must NOT carry a Reconciled write. We assert the
    # negative directly: the desired loop is the only Reconciled
    # writer, identified by reason="ApplyOk".
    final = manager.store.get(Workload, namespace="default", name="delta")
    reconciled = [c for c in final.status.conditions if c.type == "Reconciled"]
    assert len(reconciled) == 1
    assert reconciled[0].reason == "ApplyOk"


async def test_agent_stop_cancels_loops_before_closing_runtime(tmp_path: Path):
    """Bug (audit round 4): ``Agent.start`` pushes ``_cancel_tasks``
    first and ``_safe_close_runtime`` second onto the bootstrap stack.
    LIFO unwind on stop() therefore runs ``close()`` BEFORE cancelling
    the desired/runtime loops — the opposite of what the README
    documents ("stop runners first, then close the store/runtime").

    Pin the order behaviorally: when ``stop()`` calls ``close()``, the
    desired/runtime loop tasks must already be done (cancelled). If
    close runs first, those tasks will still be active when close
    starts, surfacing as spurious apply-on-closed-runtime errors and
    "ApplyFailed" status writes in real workloads.
    """
    close_started = asyncio.Event()
    close_can_finish = asyncio.Event()
    tasks_done_at_close: list[bool] = []
    captured_loop_tasks: list[asyncio.Task[None]] = []

    @final
    class _GatedCloseRuntime(Runtime):
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
        def watch(self) -> AsyncIterator[RuntimeEvent | None]:
            async def _gen() -> AsyncIterator[RuntimeEvent | None]:
                yield None
                _ = await asyncio.Event().wait()

            return _gen()

        @override
        async def close(self) -> None:
            close_started.set()
            tasks_done_at_close.append(all(t.done() for t in captured_loop_tasks))
            _ = await close_can_finish.wait()

    rt = _GatedCloseRuntime()
    store = InMemoryStore()
    store.register_kind(Workload)
    agent = Agent(rt, store=store, state_dir=tmp_path / "agent")
    await agent.start()
    captured_loop_tasks.extend(agent._tasks)  # pyright: ignore[reportPrivateUsage]
    assert captured_loop_tasks, "agent should have spawned its loop tasks"

    stop_task = asyncio.create_task(agent.stop(), name="t.agent.stop")
    _ = await asyncio.wait_for(close_started.wait(), timeout=2.0)
    close_can_finish.set()
    await stop_task

    assert tasks_done_at_close == [True], (
        f"runtime.close() ran while loop tasks were still active ({tasks_done_at_close=}); "
        "Agent.start pushed cleanups in wrong order — fix is to push "
        "_safe_close_runtime first, _cancel_tasks second so LIFO yields "
        "cancel-then-close per README"
    )


async def test_agent_start_cancellation_cleans_up_tasks(tmp_path: Path):
    """Round-3 finding: ``Agent.start`` only catches ``Exception``,
    not ``BaseException`` / ``CancelledError``. A cancellation while
    awaiting the ready ack would leave the desired loop and runtime
    event loop tasks running, with the runtime unclosed — Manager's
    stack-rollback can't reach them because Agent.start hadn't
    completed and ``_safe_stop`` wasn't pushed.

    Trigger by giving the agent a runtime whose ``watch()`` never
    yields, so the ready ack waits forever; cancel start mid-flight.
    """
    runtime = _HangRuntime()
    store = InMemoryStore()
    store.register_kind(Workload)
    agent = Agent(runtime, store=store, state_dir=tmp_path / "agent")

    start_task = asyncio.create_task(agent.start(), name="t.agent.start")
    # Let start spawn its bg tasks and reach the ready.wait.
    await wait_for_asyncio_idle()
    created = list(agent._tasks)  # pyright: ignore[reportPrivateUsage]
    assert created, "expected start to have spawned bg tasks"

    _ = start_task.cancel()
    with pytest.raises((asyncio.CancelledError, Exception)):
        _ = await start_task

    # Every task spawned during start must be done — otherwise it's
    # an orphan that nobody can stop.
    for t in created:
        assert t.done(), f"agent task {t.get_name()} leaked through cancellation"
