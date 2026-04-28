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

**Resource shape.** Agent implements the
:class:`~orchestrator.lifecycle.Resource` Protocol:
``running()`` is the lifecycle context manager (sync ``__aexit__``
sets stop_token and cancels owned tasks); ``shutdown()`` is the
explicit graceful drain that awaits the runner, the runtime, and
the runtime-event task. All sub-resources (runtime, runner) follow
the same Resource shape, so the Agent's ``__aexit__`` is fully
sync — no papered seam.
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
from ..lifecycle import (
    CheckedExitStack,
    GracefulAbort,
    Resource,
    ResourceNotRunning,
    StopToken,
    cancel_and_collect,
)
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


@final
class Agent(Resource):
    """Owns the runtime; reflects spec ↔ status against the store.

    Construction is inert. The :meth:`running` async context manager
    cleans the state dir, opens the runtime, spawns the runtime
    event loop, and runs a :class:`WorkloadAgentReconciler` via
    :class:`ControllerRunner`.

    Implements :class:`~orchestrator.lifecycle.Resource`: sync
    ``__aexit__`` only signals stop (no awaits); ``shutdown()`` is
    the explicit graceful drain that awaits the runtime's shutdown,
    the runner's shutdown, and the runtime-event task.

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
        parent_token: StopToken | None = None,
    ):
        self._runtime: Runtime = runtime
        self._store: InMemoryStore = store
        self._state_dir: Path = state_dir
        self._cgroup_root: Path | None = cgroup_root
        self._host_labels: dict[str, str] = host_labels or {"host": "local"}
        self._stop_token: StopToken = (
            parent_token.child() if parent_token is not None else StopToken()
        )
        self._is_running: bool = False
        # Body task and sub-resources held during running() so
        # shutdown() can drain them.
        self._runtime_event_task: asyncio.Task[None] | None = None
        self._runner: ControllerRunner[Workload] | None = None

    @property
    @override
    def stop_token(self) -> StopToken:
        return self._stop_token

    @override
    @contextlib.asynccontextmanager
    async def running(self) -> AsyncGenerator[Self]:
        """Open runtime, spawn reflection loops, run reconciler.

        Setup uses :class:`CheckedExitStack` so a stop_token fired
        mid-init aborts gracefully via :exc:`GracefulAbort`. Sub-
        resources (runtime, runner) are entered via the stack so
        their sync ``__aexit__``s run in LIFO on body exit.

        ``__aexit__`` (sync) sets stop_token, marks not running, and
        requests cancellation of the runtime-event task. No awaits.
        Caller should ``await agent.shutdown()`` inside the with-
        block for graceful drain (wrap with
        ``asyncio.wait_for(...)`` for a budget).
        """
        if self._is_running:
            raise ResourceNotRunning("Agent.running re-entered while already running")
        clean_state_dir(self._state_dir)
        clean_cgroup_root(self._cgroup_root)

        loop = asyncio.get_running_loop()
        # Future, not Event: ``_runtime_event_loop`` can ``set_exception``
        # on the first-time setup failure, which surfaces here as a
        # raise from ``await runtime_ready`` instead of an indefinite
        # block. Mirrors ControllerRunner._watch_loop's pattern.
        runtime_ready: asyncio.Future[None] = loop.create_future()
        reconciler = WorkloadAgentReconciler(
            runtime=self._runtime,
            host_labels=self._host_labels,
        )
        self._runner = ControllerRunner(
            reconciler, store=self._store, parent_token=self._stop_token
        )

        try:
            async with CheckedExitStack(self._stop_token) as stack:
                _ = await stack.enter_async_context(self._runtime.running())
                _ = await stack.enter_async_context(self._runner.running())
                self._runtime_event_task = asyncio.create_task(
                    self._runtime_event_loop(runtime_ready),
                    name="agent.runtime",
                )
                try:
                    _ = await runtime_ready
                    self._is_running = True
                    yield self
                finally:
                    # SYNC: signal stop, mark not running, request
                    # cancel on the runtime event task. Sub-resource
                    # ``__aexit__``s (sync) fire as the stack
                    # unwinds. Resource discipline.
                    self._is_running = False
                    self._stop_token.set()
                    if not self._runtime_event_task.done():
                        _ = self._runtime_event_task.cancel()
        except GracefulAbort:
            # stop_token fired mid-init via CheckedExitStack safe-point.
            # Convert to ResourceNotRunning so the caller sees a clear
            # semantic error instead of @asynccontextmanager's
            # RuntimeError("generator didn't yield").
            raise ResourceNotRunning("Agent.running entered with stop_token already set") from None

    @override
    async def shutdown(self) -> None:
        """Graceful drain: signal stop, await sub-resources + body task.

        Must be called inside ``running()``; raises
        :exc:`ResourceNotRunning` otherwise.

        Caller controls the budget by wrapping with
        :func:`asyncio.wait_for`. Caller-cancel propagates;
        ``__aexit__`` reconciles any half-state.
        """
        if not self._is_running:
            raise ResourceNotRunning("Agent.shutdown called outside running()")
        self._stop_token.set()
        # Drain the runner first: stop reconciling against the
        # runtime, so the runtime sees no new applies before we ask
        # it to drain in-flight work.
        if self._runner is not None:
            try:
                await self._runner.shutdown()
            except ResourceNotRunning:
                pass
        try:
            await self._runtime.shutdown()
        except ResourceNotRunning:
            pass
        # Cancel + collect the runtime event loop task. It's not a
        # Resource of its own; just a long-lived task we own. The
        # helper distinguishes our own cancel from an outer cancel
        # of agent.shutdown — the latter must propagate.
        if self._runtime_event_task is not None:
            await cancel_and_collect(self._runtime_event_task)

    # ---- runtime → status loop ----------------------------------------

    async def _runtime_event_loop(self, ready: asyncio.Future[None]) -> None:
        """Mirror runtime events into Workload status.

        ``runtime.watch()`` is an async context manager — synchronous
        registration on entry, automatic cleanup on exit. We resolve
        ``ready`` immediately after entering: by then the subscriber
        is on the runtime's broadcast set, so any apply() the agent
        issues will be observed.

        Failure handling has two regimes, distinguished by whether
        ``ready`` has been resolved:

        - **Setup failure (ready not yet done):** the exception is
          set on the ``ready`` future and re-raised. ``Agent.running``
          sees it via ``await runtime_ready`` and propagates the
          original failure to the caller. Catches bugs like "runtime
          backend permanently broken."
        - **Steady-state failure (ready already done):** the exception
          is logged and the loop pauses before re-subscribing — same
          shape as a k8s informer recovery on transient store errors.
        """
        while True:
            try:
                async with self._runtime.watch() as events:
                    if not ready.done():
                        ready.set_result(None)
                    async for event in events:
                        await self._reflect_runtime_event(event)
            except Exception as e:
                if not ready.done():
                    # Setup failure: surface via the ready future and
                    # exit. Returning (not re-raising) means the task
                    # ends with no exception of its own — the error
                    # lives on the future, where ``await runtime_ready``
                    # in ``Agent.running()`` consumes it. No need for
                    # a drain-callback dance to retrieve a duplicate
                    # task-level exception.
                    ready.set_exception(e)
                    return
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
