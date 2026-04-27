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
import contextlib
from collections.abc import AsyncGenerator
from datetime import datetime, timezone
from pathlib import Path
from typing import Self, final, override

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
from orchestrator.broadcast import BroadcastQueue
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

    def __init__(self) -> None:
        self._silent: BroadcastQueue[RuntimeEvent] = BroadcastQueue("hang-runtime")

    @override
    def watch(self):
        # A real broadcast subscription that never receives anything —
        # registers immediately (so the agent's runtime-ready event
        # fires), but the iterator blocks forever in next() since we
        # never publish.
        return self._silent.subscribe()

    @override
    @contextlib.asynccontextmanager
    async def running(self) -> AsyncGenerator[Self]:
        try:
            yield self
        finally:
            self._silent.close()


def _wl(name: str) -> Workload:
    return Workload(
        metadata=Metadata(name=name, namespace="default"),
        spec=WorkloadSpec(
            containers=[Container(name="primary", image=HostBinaryImage(path="/bin/true"), env={})],
        ),
    )


async def test_burst_of_spec_changes_collapses_to_at_most_two_applies(
    agent_pair: tuple[Manager, Agent],
    fake_runtime: FakeRuntime,
):
    """Bug round-5 #8 (recast): the agent's reconcile flow should
    coalesce bursts of spec changes via WorkQueue dedup. Apply v1,
    v2, v3 in quick succession (synchronously, before the agent
    reconciles); the runtime should see at most two applies — the
    one in flight when the burst arrived (if any) plus one for the
    final spec — and the final spec should be the latest (v3).

    Pre-fix: agent's per-event _desired_loop processes every event
    serially, so the runtime sees three applies (v1, v2, v3) all in
    full. Post-fix (Controller pattern): the workqueue dedups, the
    reconciler reads current state at reconcile time, and v2 never
    becomes its own apply.
    """
    manager, _agent = agent_pair

    # Synchronously fire three applies before yielding so the agent's
    # workqueue sees all three before it processes any.
    wl_v1 = _wl("burst")
    wl_v1.spec.containers[0].env = {"VERSION": "1"}
    wl_v2 = _wl("burst")
    wl_v2.spec.containers[0].env = {"VERSION": "2"}
    wl_v3 = _wl("burst")
    wl_v3.spec.containers[0].env = {"VERSION": "3"}

    _ = await manager.store.apply(wl_v1)
    _ = await manager.store.apply(wl_v2)
    _ = await manager.store.apply(wl_v3)

    # Wait for the agent to reach the v3 reconciled state.
    _ = await wait_for_event(
        manager.store,
        Workload,
        lambda w: (
            w.metadata.name == "burst"
            and w.status.phase == "Running"
            and any(
                c.type == "Reconciled" and c.status == "True" and c.observed_generation == 3
                for c in w.status.conditions
            )
        ),
    )

    burst_applies = [w for w in fake_runtime.applies if w.metadata.name == "burst"]
    versions_applied = [w.spec.containers[0].env.get("VERSION") for w in burst_applies]
    assert "3" in versions_applied, (
        f"runtime never received the final spec; versions applied: {versions_applied}"
    )
    # Coalesced: at most 2 applies (initial pre-burst + final). Three
    # applies means we processed every event without dedup.
    assert len(burst_applies) <= 2, (
        f"agent failed to coalesce a burst of spec changes; "
        f"runtime saw {len(burst_applies)} applies (versions: {versions_applied}) "
        f"instead of at most 2"
    )


async def test_supersede_cancels_in_flight_apply(
    agent_pair: tuple[Manager, Agent],
    fake_runtime: FakeRuntime,
    monkeypatch: pytest.MonkeyPatch,
):
    """Wires the supervisor pattern's abort-on-cancel capability into
    the agent's reconcile path: when v2 arrives while v1's
    ``runtime.apply`` is mid-flight, the workqueue cancels the
    worker's reconcile, which propagates ``CancelledError`` into
    ``runtime.apply`` and stops the in-progress v1 spawn — instead
    of running v1 to completion before kicking off v2.

    Without this wiring the workqueue would just dirty-flag the ref
    and v2 would have to wait for v1 to finish naturally.
    """
    manager, _agent = agent_pair

    apply_in_flight = asyncio.Event()
    let_apply_finish = asyncio.Event()
    apply_records: list[tuple[str, str]] = []
    real_apply = fake_runtime.apply

    async def _slow_apply(workload: Workload) -> WorkloadStatus:
        version = workload.spec.containers[0].env.get("VERSION", "?")
        apply_records.append((version, "started"))
        apply_in_flight.set()
        try:
            _ = await let_apply_finish.wait()
        except asyncio.CancelledError:
            apply_records.append((version, "cancelled"))
            raise
        apply_records.append((version, "finished"))
        return await real_apply(workload)

    monkeypatch.setattr(fake_runtime, "apply", _slow_apply)

    wl_v1 = _wl("super")
    wl_v1.spec.containers[0].env = {"VERSION": "1"}
    _ = await manager.store.apply(wl_v1)

    # Wait for v1's apply to be parked in slow_apply's await.
    _ = await asyncio.wait_for(apply_in_flight.wait(), timeout=2.0)
    apply_in_flight.clear()

    # Apply v2 — supersede must cancel v1's in-flight apply so v2
    # can start without waiting for v1 to complete.
    wl_v2 = _wl("super")
    wl_v2.spec.containers[0].env = {"VERSION": "2"}
    _ = await manager.store.apply(wl_v2)

    # v2's apply must start before we let anything finish — proving
    # v1 was cancelled mid-flight rather than allowed to complete.
    _ = await asyncio.wait_for(apply_in_flight.wait(), timeout=2.0)

    # Now v2 is parked. Let it finish.
    let_apply_finish.set()
    _ = await wait_for_event(
        manager.store,
        Workload,
        lambda w: (
            w.metadata.name == "super"
            and w.status.phase == "Running"
            and any(
                c.type == "Reconciled" and c.status == "True" and c.observed_generation == 2
                for c in w.status.conditions
            )
        ),
    )

    assert ("1", "cancelled") in apply_records, (
        f"v1's apply was not cancelled mid-flight; records: {apply_records}"
    )
    assert ("2", "finished") in apply_records, (
        f"v2's apply never finished; records: {apply_records}"
    )
    # v1 must NOT have finished (we cancelled it before letting it through).
    assert ("1", "finished") not in apply_records, (
        f"v1 was allowed to complete despite supersede; records: {apply_records}"
    )


async def test_cascading_supersede_during_unwind(
    agent_pair: tuple[Manager, Agent],
    fake_runtime: FakeRuntime,
    monkeypatch: pytest.MonkeyPatch,
):
    """Cascading supersede: v3 arrives while v2 is still unwinding
    v1's cancellation. With correct asyncio primitives (TaskGroup +
    proper Cancel propagation through the supervisor's RAII chain)
    this should converge cleanly to v3 with no orphan processes,
    no stuck tasks, no lost cancellation requests.

    Specifically:
    - v1 applied; mid-spawn (apply blocks).
    - v2 applied → v1 cancel issued; v1 apply unwinds (raises
      CancelledError); v2 apply starts; mid-spawn.
    - v3 applied immediately after, before v2's cancel-of-v1 has
      finished propagating through the runtime → v2 cancel issued;
      v2 unwinds; v3 starts.
    - We let v3 finish.

    Asserts: only v3 finished; v1 and v2 both cancelled; the agent
    converged to v3's reconciled state without hanging.
    """
    manager, _agent = agent_pair

    apply_in_flight = asyncio.Event()
    let_apply_finish = asyncio.Event()
    apply_records: list[tuple[str, str]] = []
    real_apply = fake_runtime.apply

    async def _slow_apply(workload: Workload) -> WorkloadStatus:
        version = workload.spec.containers[0].env.get("VERSION", "?")
        apply_records.append((version, "started"))
        apply_in_flight.set()
        try:
            _ = await let_apply_finish.wait()
        except asyncio.CancelledError:
            apply_records.append((version, "cancelled"))
            raise
        apply_records.append((version, "finished"))
        return await real_apply(workload)

    monkeypatch.setattr(fake_runtime, "apply", _slow_apply)

    def _versioned(v: str) -> Workload:
        wl = _wl("cascade")
        wl.spec.containers[0].env = {"VERSION": v}
        return wl

    # v1: apply, wait for it to be parked.
    _ = await manager.store.apply(_versioned("1"))
    _ = await asyncio.wait_for(apply_in_flight.wait(), timeout=2.0)
    apply_in_flight.clear()

    # v2: apply, wait for it to take over (v1 must have been cancelled).
    _ = await manager.store.apply(_versioned("2"))
    _ = await asyncio.wait_for(apply_in_flight.wait(), timeout=2.0)
    apply_in_flight.clear()

    # v3: apply, wait for it to take over. This is the cascade — v3
    # supersedes v2 which is itself still unwinding/just-started.
    _ = await manager.store.apply(_versioned("3"))
    _ = await asyncio.wait_for(apply_in_flight.wait(), timeout=2.0)

    # Let v3 finish.
    let_apply_finish.set()
    _ = await wait_for_event(
        manager.store,
        Workload,
        lambda w: (
            w.metadata.name == "cascade"
            and w.status.phase == "Running"
            and any(
                c.type == "Reconciled" and c.status == "True" and c.observed_generation == 3
                for c in w.status.conditions
            )
        ),
    )

    assert ("1", "cancelled") in apply_records, f"v1 not cancelled; {apply_records}"
    assert ("2", "cancelled") in apply_records, f"v2 not cancelled; {apply_records}"
    assert ("3", "finished") in apply_records, f"v3 not finished; {apply_records}"
    finished = [v for v, e in apply_records if e == "finished"]
    assert finished == ["3"], f"only v3 should have finished; finished applies: {finished}"


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


async def test_agent_running_cancels_loops_before_closing_runtime(tmp_path: Path):
    """Bug 5 (audit round 4) — original shape: ``Agent.start`` pushed
    ``_cancel_tasks`` and ``_safe_close_runtime`` as free-floating
    callbacks; the wrong push order made LIFO unwind close the
    runtime *before* cancelling the loops. Spurious "ApplyFailed"
    conditions then appeared during graceful shutdown.

    The fix is structural: ``Agent.running()`` enters the runtime via
    AsyncExitStack first, then enters a ``TaskGroup`` for the loops.
    LIFO of ``async with`` exits guarantees the TaskGroup cancels
    the loops first, then the runtime closes. Push order can no
    longer be wrong because there is no push order — the cleanups
    are nested context managers, not separately-registered callbacks.

    Pin the order behaviorally: when the runtime's close branch
    runs, the loop tasks must already be done.
    """
    close_observed_loops_done: list[bool] = []
    captured_loop_tasks: list[asyncio.Task[object]] = []

    @final
    class _OrderProbingRuntime(Runtime):
        def __init__(self) -> None:
            self._silent: BroadcastQueue[RuntimeEvent] = BroadcastQueue("order-probe")

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
        @contextlib.asynccontextmanager
        async def running(self) -> AsyncGenerator[Self]:
            try:
                yield self
            finally:
                close_observed_loops_done.append(all(t.done() for t in captured_loop_tasks))
                self._silent.close()

    rt = _OrderProbingRuntime()
    store = InMemoryStore()
    store.register_kind(Workload)
    agent = Agent(rt, store=store, state_dir=tmp_path / "agent")
    async with agent.running():
        captured_loop_tasks.extend(
            t for t in asyncio.all_tasks() if t.get_name().startswith("agent.")
        )
        assert captured_loop_tasks, "agent should have spawned its loop tasks"

    assert close_observed_loops_done == [True], (
        f"runtime.close branch saw active loop tasks ({close_observed_loops_done=}); "
        "TaskGroup must cancel loops before runtime exits its context"
    )


async def test_agent_running_cancellation_cleans_up_tasks(tmp_path: Path):
    """Round-3 finding (recast for the new contract): a cancellation
    during ``async with agent.running():`` (e.g. while awaiting the
    ready acks) must leave no orphan tasks. With the AsyncExitStack
    + TaskGroup design the rollback is automatic — TaskGroup is
    inside the stack and its ``__aexit__`` cancels every spawned
    task — but pin it as a regression test against future
    refactors.
    """
    runtime = _HangRuntime()
    store = InMemoryStore()
    store.register_kind(Workload)
    agent = Agent(runtime, store=store, state_dir=tmp_path / "agent")

    async def _enter_then_yield() -> None:
        async with agent.running():
            _ = await asyncio.Event().wait()

    enter_task = asyncio.create_task(_enter_then_yield(), name="t.agent.enter")
    # Let running() spawn its TaskGroup children and reach the ready.wait.
    await wait_for_asyncio_idle()
    spawned_tasks = [t for t in asyncio.all_tasks() if t.get_name().startswith("agent.")]
    assert spawned_tasks, "agent should have spawned its loop tasks"

    _ = enter_task.cancel()
    with pytest.raises((asyncio.CancelledError, Exception)):
        await enter_task

    for t in spawned_tasks:
        assert t.done(), f"agent task {t.get_name()} leaked through cancellation"
