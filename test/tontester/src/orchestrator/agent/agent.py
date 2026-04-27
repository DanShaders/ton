"""In-process kubelet equivalent.

The :class:`Agent` is the only thing that talks to the :class:`Runtime`.
Its job is bidirectional reflection:

- **Desired → Runtime.** Subscribe to Workload events from the store;
  for each event, call ``runtime.apply`` (or ``delete``) to converge
  the host on the spec. This is the agent's reconcile loop — distinct
  from controller reconcilers because it owns the actual processes
  rather than declarative resource lifecycle.
- **Runtime → Status.** Subscribe to ``runtime.watch()``; for each
  push event, ``store.patch_status`` the matching Workload with the
  observed status.

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
from datetime import datetime, timezone
from pathlib import Path
from typing import Literal, final

from ..errors import NotFound, WatchOverflow
from ..resources import (
    Condition,
    Metadata,
    Workload,
    WorkloadStatus,
    matches,
    upsert_condition,
)
from ..runtime import Runtime, RuntimeEvent, RuntimeEventType
from ..store import InMemoryStore, WatchEventType
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
class Agent:
    """Owns the runtime; reflects spec ↔ status against the store.

    Construction is inert. :meth:`start` does the cleanup pass and
    spawns the two reconcile loops; :meth:`stop` cancels them and
    closes the runtime. Both are idempotent.

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
        self._stack: contextlib.AsyncExitStack = contextlib.AsyncExitStack()
        self._tasks: list[asyncio.Task[None]] = []
        # Set by ``_desired_loop`` once it's inside the
        # ``async with store.subscription(...)`` block — i.e. once the
        # bus subscription is registered. ``start()`` awaits this so
        # the caller's first apply is guaranteed to be observed.
        self._desired_ready: asyncio.Event = asyncio.Event()
        # Same shape for the runtime side: set by ``_runtime_event_loop``
        # once it has subscribed to ``runtime.watch()``. Without this,
        # the very first ``runtime.apply()`` after ``agent.start()``
        # returned could publish a STARTED event before the runtime
        # event loop had registered its subscription, so the loop would
        # never observe it and the workload's Available condition would
        # be missing until the next runtime event.
        self._runtime_ready: asyncio.Event = asyncio.Event()
        self._started: bool = False
        self._stopping: bool = False

    async def start(self) -> None:
        """Spawn the desired and runtime loops; return once both are
        ready to observe events.

        Cleanup-on-failure is handled by AsyncExitStack's ``pop_all``
        idiom: callbacks are pushed onto a *bootstrap* stack as
        resources come up, and on success the bootstrap's ownership is
        transferred (``pop_all``) to the long-lived ``self._stack`` for
        teardown on ``stop()``. On any exception escaping the
        ``async with bootstrap`` (including ``CancelledError`` —
        ``async with`` honors it the same as any other exception),
        bootstrap's ``__aexit__`` runs and the partial state is
        unwound. No ``except BaseException`` needed.
        """
        if self._started:
            return
        self._started = True
        fully_started = False
        bootstrap = contextlib.AsyncExitStack()
        try:
            async with bootstrap:
                clean_state_dir(self._state_dir)
                clean_cgroup_root(self._cgroup_root)

                self._tasks.append(
                    asyncio.create_task(self._desired_loop(), name="agent.desired")
                )
                self._tasks.append(
                    asyncio.create_task(self._runtime_event_loop(), name="agent.runtime")
                )
                _ = bootstrap.push_async_callback(self._cancel_tasks)
                _ = bootstrap.push_async_callback(_safe_close_runtime, self._runtime)

                # Block until both loops register their subscriptions
                # (store bus + runtime.watch). Without these acks, an
                # apply right after start could fire into a bus with no
                # subscriber yet and the event would be lost.
                _ = await self._desired_ready.wait()
                _ = await self._runtime_ready.wait()

                # Success: transfer cleanups from the bootstrap stack
                # to the long-lived stack. The async-with's __aexit__
                # then runs against an empty bootstrap (no-op).
                self._stack = bootstrap.pop_all()
                fully_started = True
        finally:
            if not fully_started:
                self._started = False

    async def stop(self) -> None:
        if not self._started or self._stopping:
            return
        self._stopping = True
        await self._stack.aclose()

    async def __aenter__(self) -> "Agent":
        await self.start()
        return self

    async def __aexit__(
        self,
        _exc_type: object,
        _exc: object,
        _tb: object,
    ) -> None:
        await self.stop()

    # ---- desired-state loop -------------------------------------------

    async def _desired_loop(self) -> None:
        """Subscribe to Workload events and apply/delete via the runtime.

        The agent filters by ``host_selector``: workloads whose selector
        doesn't match this host's labels are ignored. (In practice for
        the prototype, every workload selects every host because we
        only have one — this is the seam fleet deployments will use.)

        The subscription's lifetime is bound to the ``async with``
        block; if this task is cancelled, the bus unregisters us
        automatically. On WatchOverflow we ``continue`` the outer loop,
        which exits the with block (closing the old sub) and re-enters
        with a fresh one — same shape as k8s informer recovery.
        """
        while not self._stopping:
            try:
                async with self._store.subscription(Workload) as sub:
                    # Subscription is registered with the bus right
                    # now. Signal start() and proceed with catch-up.
                    self._desired_ready.set()
                    for wl in self._store.list(Workload):
                        if not self._matches_host(wl):
                            continue
                        await self._handle_workload_event(wl, WatchEventType.ADDED)
                    async for event in sub:
                        if not self._matches_host(event.resource):
                            continue
                        await self._handle_workload_event(event.resource, event.type)
            except WatchOverflow:
                logger.warning("workload watch overflowed; relisting")
                continue
            except asyncio.CancelledError:
                return
            except Exception:
                logger.exception("desired loop crashed; restarting after pause")
                try:
                    await asyncio.sleep(1.0)
                except asyncio.CancelledError:
                    return

    def _matches_host(self, workload: Workload) -> bool:
        sel = workload.spec.host_selector
        if sel.is_empty():
            return True
        return matches(sel, self._host_labels)

    async def _handle_workload_event(
        self,
        workload: Workload,
        event_type: WatchEventType,
    ) -> None:
        ns = workload.metadata.namespace
        name = workload.metadata.name

        if event_type == WatchEventType.DELETED:
            await self._runtime.delete(namespace=ns, name=name)
            return

        if workload.metadata.deletion_timestamp is not None:
            # Graceful delete in progress: stop the workload, drain the
            # finalizer. The store hard-deletes once the finalizer set
            # is empty — see InMemoryStore.patch_metadata.
            await self._runtime.delete(namespace=ns, name=name)
            await self._drop_finalizer(workload)
            return

        # Add the agent finalizer if it isn't already there. This makes
        # the store wait for our delete-and-cleanup before hard-removing
        # the row, so the runtime never lags behind the resource view.
        if _AGENT_FINALIZER not in workload.metadata.finalizers:
            try:
                _ = await self._store.patch_metadata(
                    Workload, namespace=ns, name=name, mutator=_add_agent_finalizer
                )
            except NotFound:
                # Raced with a delete; nothing to do.
                return
            # The patch produces another MODIFIED event we'll see in the
            # next iteration; don't double-apply now.
            return

        # Skip if we've already reconciled this generation. Each apply
        # bumps observed_generation via the Reconciled condition; the
        # status write triggers another MODIFIED event back into the
        # watch bus, so without this guard we'd loop forever pushing
        # the same spec into the runtime.
        if _already_reconciled(workload):
            return

        try:
            status = await self._runtime.apply(workload)
        except Exception as e:
            logger.exception(f"runtime.apply failed for {ns}/{name}")
            await self._mark_condition(
                workload,
                type_="Reconciled",
                status="False",
                reason="ApplyFailed",
                message=str(e),
            )
            return

        await self._write_status(workload, status, reconciled=True)

    async def _drop_finalizer(self, workload: Workload) -> None:
        ns = workload.metadata.namespace
        name = workload.metadata.name
        try:
            _ = await self._store.patch_metadata(
                Workload, namespace=ns, name=name, mutator=_drop_agent_finalizer
            )
        except NotFound:
            return

    # ---- runtime → status loop ----------------------------------------

    async def _runtime_event_loop(self) -> None:
        """Mirror runtime events into Workload status.

        Wrapped in the same restart-on-error pattern as ``_desired_loop``:
        a single transient exception inside ``runtime.watch()`` (or
        downstream in ``_reflect_runtime_event``) would otherwise kill
        the task silently while the desired loop kept writing specs —
        leaving status permanently desynced from the runtime.

        Sets ``self._runtime_ready`` once the first event lands or the
        generator yields its registration sentinel — see
        :class:`Runtime.watch`'s contract: the first yielded value is
        ``None`` to signal "registered, awaiting events." This lets
        ``start()`` block until the runtime subscription is actually
        live; without it, the very first ``runtime.apply()`` after
        start could race with this loop's coroutine being scheduled
        and the corresponding STARTED would be lost.
        """
        while not self._stopping:
            try:
                async for event in self._runtime.watch():
                    if event is None:
                        # Registration sentinel — runtime has added our
                        # subscriber to its set; subsequent publishes
                        # will reach us.
                        self._runtime_ready.set()
                        continue
                    await self._reflect_runtime_event(event)
            except asyncio.CancelledError:
                return
            except Exception:
                logger.exception("runtime event loop crashed; restarting after pause")
                try:
                    await asyncio.sleep(1.0)
                except asyncio.CancelledError:
                    return

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
        # not RECONCILE state. The desired loop is the only writer of
        # the Reconciled condition — it knows what generation it
        # actually applied. If the runtime event loop also wrote
        # Reconciled, a stale STARTED for an old generation would write
        # Reconciled with the *current* generation (because we read
        # ``current`` from the store at event time), falsely marking a
        # newer generation as already-reconciled. Then subsequent spec
        # changes would be silently skipped by ``_already_reconciled``.
        await self._write_status(
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

    # ---- status writers -----------------------------------------------

    async def _write_status(
        self,
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
            _ = await self._store.patch_status(Workload, namespace=ns, name=name, mutator=_apply)
        except NotFound:
            return
        except Exception:
            # patch_status can raise Conflict (concurrent writer),
            # ValidationError (mutator misuse), or pydantic errors.
            # Log and absorb — letting it propagate would crash the
            # desired loop's iteration and force a restart that
            # re-tries the same dead subscription.
            logger.exception(f"AGENT _write_status({ns}/{name}) raised; swallowing")

    async def _mark_condition(
        self,
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
            _ = await self._store.patch_status(
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

    # ---- task lifecycle -----------------------------------------------

    async def _cancel_tasks(self) -> None:
        for task in self._tasks:
            _ = task.cancel()
        for task in self._tasks:
            try:
                await task
            except asyncio.CancelledError, Exception:
                pass
        self._tasks.clear()


# --- helpers --------------------------------------------------------------


async def _safe_close_runtime(runtime: Runtime) -> None:
    try:
        await runtime.close()
    except Exception:
        logger.exception("runtime close raised; swallowing")
