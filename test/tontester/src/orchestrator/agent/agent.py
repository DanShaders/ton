"""In-process kubelet equivalent.

The :class:`Agent` is the only thing that talks to the :class:`Runtime`.
Its job is bidirectional reflection:

- **Desired → Runtime.** A :class:`WorkloadAgentReconciler` runs as a
  regular :class:`~orchestrator.control.ControllerRunner` against the
  Workload kind. The reconciler reads the *current* state from the
  store (not from individual events) and converges the runtime to it.
  Bursts of spec changes coalesce via the workqueue's dedup; per-key
  serialization comes from the workqueue's in-flight tracking.
- **Runtime → Status.** A separate task subscribes to
  ``runtime.watch()`` and reflects each event into the matching
  Workload's status via ``store.patch_status``.

A single agent serves the whole process. Per the
"orchestrator-crash-=-payload-crash" contract there's no agent-side
durable state to recover; startup just calls
:func:`clean_state_dir` to mop up filesystem debris from a previous
unclean exit.

For fleet deployments, multiple :class:`Agent` instances run on
separate hosts, each filtering by ``host_selector`` against its own
labels. Cross-host transport is out of scope for this prototype — the
class is the same, the transport plugs in front of the store
subscribe + patch_status calls.
"""

import asyncio
import contextlib
import logging
from collections.abc import AsyncGenerator
from datetime import datetime, timezone
from pathlib import Path
from typing import Literal, Self, final, override

from ..control import Controller, ControllerRunner, ItemRef, Result, WatchSpec
from ..errors import NotFound
from ..resources import (
    Condition,
    Metadata,
    Workload,
    WorkloadStatus,
    matches,
    upsert_condition,
)
from ..runtime import Runtime, RuntimeEvent, RuntimeEventType
from ..store import InMemoryStore
from .cleanup import clean_cgroup_root, clean_state_dir

logger = logging.getLogger(__name__)

_AGENT_FINALIZER = "orchestrator.io/agent-cleanup"

type ConditionStatus = Literal["True", "False", "Unknown"]


def _add_agent_finalizer(meta: Metadata) -> None:
    if _AGENT_FINALIZER not in meta.finalizers:
        meta.finalizers.append(_AGENT_FINALIZER)


def _drop_agent_finalizer(meta: Metadata) -> None:
    if _AGENT_FINALIZER in meta.finalizers:
        meta.finalizers.remove(_AGENT_FINALIZER)


_AGENT_RECONCILED_REASON = "ApplyOk"


def _already_reconciled(workload: Workload) -> bool:
    """Has this workload's current generation been agent-Reconciled?

    Match on (type, status, reason, observed_generation) — *not* just
    on (type, status, observed_generation). Other writers (a future
    controller, a test fixture, a CRD-style health rollup) might use
    type=Reconciled with their own reason; respecting only the agent's
    own reason keeps this guard from silently disabling the agent when
    someone else writes the condition.
    """
    gen = workload.metadata.generation
    for c in workload.status.conditions:
        if (
            c.type == "Reconciled"
            and c.status == "True"
            and c.reason == _AGENT_RECONCILED_REASON
            and c.observed_generation == gen
        ):
            return True
    return False


def _now() -> datetime:
    return datetime.now(timezone.utc)


def _cancel_all(tasks: list[asyncio.Task[None]]) -> None:
    """Sync callback usable with ``AsyncExitStack.callback`` — cancels
    every task. The surrounding TaskGroup awaits them on its own
    ``__aexit__``; we just need to request the cancellation here.
    """
    for t in tasks:
        _ = t.cancel()


@final
class Agent:
    """Owns the runtime; reflects spec ↔ status against the store.

    Construction is inert. The :meth:`running` async context manager
    cleans the state dir, opens the runtime, spawns the runtime
    event loop, and runs a :class:`WorkloadAgentReconciler` via
    :class:`ControllerRunner`. On exit (any path, including
    ``CancelledError``), the AsyncExitStack tears everything down in
    LIFO order: reconciler stops, event loop is cancelled, runtime
    closes.

    ``host_labels`` must include whatever a workload's ``host_selector``
    can match — this is how we partition workloads across hosts in the
    fleet case. For laptop runs, ``{"host": "local"}`` is enough.
    """

    def __init__(
        self,
        runtime: Runtime,
        *,
        store: InMemoryStore,
        state_dir: Path,
        cgroup_root: Path | None = None,
        host_labels: dict[str, str] | None = None,
    ):
        self._runtime: Runtime = runtime
        self._store: InMemoryStore = store
        self._state_dir: Path = state_dir
        self._cgroup_root: Path | None = cgroup_root
        self._host_labels: dict[str, str] = host_labels or {"host": "local"}

    @contextlib.asynccontextmanager
    async def running(self) -> AsyncGenerator[Self]:
        """Open the runtime, spawn the reconciler + runtime event loop,
        and wait for the runtime subscription to register before yielding.

        Cleanup ordering on exit (LIFO of AsyncExitStack):

        1. Cancel the runtime event loop task (TaskGroup awaits it).
        2. ControllerRunner exits — its TaskGroup cancels watch +
           worker tasks, drains the workqueue.
        3. Runtime ``running()`` exits — drains workloads.

        External cancellation of the surrounding ``async with``
        propagates through every level because that's the contract
        of TaskGroup and AsyncExitStack.
        """
        clean_state_dir(self._state_dir)
        clean_cgroup_root(self._cgroup_root)

        runtime_ready = asyncio.Event()
        reconciler = WorkloadAgentReconciler(
            runtime=self._runtime,
            host_labels=self._host_labels,
        )
        runner: ControllerRunner[Workload] = ControllerRunner(reconciler, store=self._store)

        async with contextlib.AsyncExitStack() as stack:
            _ = await stack.enter_async_context(self._runtime.running())
            tg = await stack.enter_async_context(asyncio.TaskGroup())
            runtime_task = tg.create_task(
                self._runtime_event_loop(runtime_ready),
                name="agent.runtime",
            )
            _ = stack.callback(_cancel_all, [runtime_task])
            _ = await stack.enter_async_context(runner.running())
            _ = await runtime_ready.wait()
            yield self

    # ---- runtime → status loop ----------------------------------------

    async def _runtime_event_loop(self, ready: asyncio.Event) -> None:
        """Mirror runtime events into Workload status.

        ``runtime.watch()`` is an async context manager — synchronous
        registration on entry, automatic cleanup on exit. We set
        ``ready`` immediately after entering: by then the subscriber
        is on the runtime's broadcast set, so any apply() the agent
        issues will be observed. No registration sentinel needed.

        Wrapped in a restart loop because a transient exception in
        ``_reflect_runtime_event`` would otherwise kill this task
        silently while the reconciler kept writing specs — leaving
        status permanently desynced from the runtime.
        """
        while True:
            try:
                async with self._runtime.watch() as events:
                    ready.set()
                    async for event in events:
                        await self._reflect_runtime_event(event)
            except Exception:
                logger.exception("runtime event loop crashed; restarting after pause")
                await asyncio.sleep(1.0)

    async def _reflect_runtime_event(self, event: RuntimeEvent) -> None:
        try:
            current = self._store.get(
                Workload,
                namespace=event.workload_namespace,
                name=event.workload_name,
            )
        except NotFound:
            # Runtime has it; store doesn't. Could be a delete-in-flight;
            # drop the event.
            return

        cond_status: ConditionStatus
        cond_reason: str
        match event.type:
            case RuntimeEventType.STARTED:
                cond_status = "True"
                cond_reason = "Started"
            case RuntimeEventType.EXITED:
                cond_status = "False"
                cond_reason = "Exited"
            case RuntimeEventType.STATUS_CHANGED:
                cond_status = "True" if event.status.phase == "Running" else "Unknown"
                cond_reason = "StatusChanged"

        # ``reconciled=False`` because this loop reflects RUNTIME state,
        # not RECONCILE state. The reconciler is the only writer of the
        # Reconciled condition — it knows what generation it actually
        # applied. If the runtime event loop also wrote Reconciled, a
        # stale STARTED for an old generation would write Reconciled
        # with the *current* generation (because we read ``current``
        # from the store at event time), falsely marking a newer
        # generation as already-reconciled. Then subsequent spec
        # changes would be silently skipped by ``_already_reconciled``.
        await _write_status(
            self._store,
            current,
            event.status,
            reconciled=False,
            extra=(
                Condition(
                    type="Available",
                    status=cond_status,
                    reason=cond_reason,
                    message=f"runtime: phase={event.status.phase}",
                    last_transition=event.timestamp,
                    observed_generation=current.metadata.generation,
                ),
            ),
        )


@final
class WorkloadAgentReconciler(Controller[Workload]):
    """Drives ``runtime.apply`` / ``runtime.delete`` from store state.

    Uses the standard :class:`Controller` shape so the agent gets
    WorkQueue dedup (collapse bursts) and per-key serialization
    (only one reconcile per workload at a time) for free. The
    reconciler reads *current* store state on each invocation, not
    the event payload, so v1→v2→v3 spec churn collapses to a single
    reconcile against v3.
    """

    def __init__(
        self,
        *,
        runtime: Runtime,
        host_labels: dict[str, str],
    ):
        self._runtime: Runtime = runtime
        self._host_labels: dict[str, str] = host_labels

    @property
    @override
    def owned_kind(self) -> type[Workload]:
        return Workload

    @property
    @override
    def watches(self) -> list[WatchSpec]:
        return []  # ControllerRunner auto-adds owned_kind watch

    @override
    async def reconcile(self, store: InMemoryStore, ref: ItemRef) -> Result:
        wl = store.get_or_none(Workload, namespace=ref.namespace, name=ref.name)
        if wl is None:
            # Hard-deleted in the store. Idempotently ensure the
            # runtime is gone too.
            await self._runtime.delete(namespace=ref.namespace, name=ref.name)
            return Result(ok=True, reason="store row gone")

        if not self._matches_host(wl):
            return Result(ok=True, reason="not our host")

        if wl.metadata.deletion_timestamp is not None:
            await self._runtime.delete(namespace=wl.metadata.namespace, name=wl.metadata.name)
            await self._drop_finalizer(store, wl)
            return Result(ok=True, reason="terminating")

        # Add the agent finalizer if it isn't already there. This makes
        # the store wait for our delete-and-cleanup before hard-removing
        # the row, so the runtime never lags behind the resource view.
        if _AGENT_FINALIZER not in wl.metadata.finalizers:
            try:
                _ = await store.patch_metadata(
                    Workload,
                    namespace=wl.metadata.namespace,
                    name=wl.metadata.name,
                    mutator=_add_agent_finalizer,
                )
            except NotFound:
                return Result(ok=True, reason="raced delete")
            return Result(ok=True, reason="finalizer added")

        # Skip if we've already reconciled this generation. The status
        # write triggers another MODIFIED event on the watch bus; the
        # workqueue dedups so we only requeue once, but we'd still spend
        # a reconcile pass. The check short-circuits cheaply.
        if _already_reconciled(wl):
            return Result(ok=True, reason="already reconciled")

        try:
            status = await self._runtime.apply(wl)
        except Exception as e:
            logger.exception(f"runtime.apply failed for {wl.metadata.namespace}/{wl.metadata.name}")
            await _mark_condition(
                store,
                wl,
                type_="Reconciled",
                status="False",
                reason="ApplyFailed",
                message=str(e),
            )
            return Result(ok=False, reason=str(e))

        await _write_status(store, wl, status, reconciled=True)
        return Result(ok=True, reason="reconciled")

    def _matches_host(self, workload: Workload) -> bool:
        sel = workload.spec.host_selector
        if sel.is_empty():
            return True
        return matches(sel, self._host_labels)

    async def _drop_finalizer(self, store: InMemoryStore, workload: Workload) -> None:
        try:
            _ = await store.patch_metadata(
                Workload,
                namespace=workload.metadata.namespace,
                name=workload.metadata.name,
                mutator=_drop_agent_finalizer,
            )
        except NotFound:
            return


# ---- status writers (module-level, shared by Agent + Reconciler) -------


async def _write_status(
    store: InMemoryStore,
    workload: Workload,
    runtime_status: WorkloadStatus,
    *,
    reconciled: bool,
    extra: tuple[Condition, ...] = (),
) -> None:
    ns = workload.metadata.namespace
    name = workload.metadata.name
    gen = workload.metadata.generation

    def _apply(w: Workload) -> None:
        w.status.phase = runtime_status.phase
        w.status.host = runtime_status.host
        w.status.network_address = runtime_status.network_address
        w.status.container_statuses = list(runtime_status.container_statuses)
        new_conds = list(w.status.conditions)
        if reconciled:
            new_conds = upsert_condition(
                new_conds,
                Condition(
                    type="Reconciled",
                    status="True",
                    reason=_AGENT_RECONCILED_REASON,
                    message="agent applied current spec to runtime",
                    last_transition=_now(),
                    observed_generation=gen,
                ),
            )
        for c in extra:
            new_conds = upsert_condition(new_conds, c)
        w.status.conditions = new_conds

    try:
        _ = await store.patch_status(Workload, namespace=ns, name=name, mutator=_apply)
    except NotFound:
        return
    except Exception:
        # patch_status can raise Conflict (concurrent writer),
        # ValidationError (mutator misuse), or pydantic errors.
        # Log and absorb — letting it propagate would crash the
        # reconciler / runtime event loop.
        logger.exception(f"AGENT _write_status({ns}/{name}) raised; swallowing")


async def _mark_condition(
    store: InMemoryStore,
    workload: Workload,
    *,
    type_: str,
    status: ConditionStatus,
    reason: str,
    message: str,
) -> None:
    gen = workload.metadata.generation

    def _apply(w: Workload) -> None:
        w.status.conditions = upsert_condition(
            w.status.conditions,
            Condition(
                type=type_,
                status=status,
                reason=reason,
                message=message,
                last_transition=_now(),
                observed_generation=gen,
            ),
        )

    try:
        _ = await store.patch_status(
            Workload,
            namespace=workload.metadata.namespace,
            name=workload.metadata.name,
            mutator=_apply,
        )
    except NotFound:
        return
    except Exception:
        logger.exception(
            f"AGENT _mark_condition({workload.metadata.name}, {type_}) raised; swallowing"
        )
