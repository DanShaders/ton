"""Reconciler protocol + the runner that drives it.

A :class:`Controller` is anything that can ``reconcile(ref)`` for a
particular *owned* kind. The :class:`ControllerRunner` does the
plumbing:

1. Subscribes to the store's watch bus(es) the controller declares.
2. Maps each watch event to ``(namespace, name)`` ref(s) of the owned
   kind, via the controller's ``map_event``.
3. Feeds those refs into a :class:`WorkQueue`.
4. Spawns N worker tasks that pull refs and call
   ``controller.reconcile(ref)``.

Reconciler return value is :class:`Result`: ``ok=True`` clears backoff,
``ok=False`` triggers exponential backoff, ``requeue_after_s`` schedules
an explicit follow-up regardless. Worker tasks treat raised exceptions
as ``ok=False`` with a stack-traced log line.

**Lifecycle.** ``ControllerRunner`` is an async context manager —
``async with runner.running():`` spawns the watch + worker tasks
inside an :class:`asyncio.TaskGroup` and tears them down on exit.
There is no ``start()`` / ``stop()`` pair; the TaskGroup *is* the
cancel-and-drain. External cancellation propagates correctly
because that's what TaskGroup does.
"""

import asyncio
import contextlib
import logging
from collections.abc import AsyncGenerator, AsyncIterator
from contextlib import asynccontextmanager
from dataclasses import dataclass
from typing import Generic, Protocol, Self, TypeVar, final

from pydantic import BaseModel

from ..errors import WatchOverflow
from ..resources import ResourceLike
from ..store import InMemoryStore, WatchEvent, WatchEventType
from .workqueue import Clock, RealClock, WorkQueue

logger = logging.getLogger(__name__)

_AnyResource = ResourceLike[BaseModel, BaseModel]
# Covariant in the owned kind: every method that mentions _TOwned uses
# it in a return-only / phantom position (``owned_kind`` returns
# ``type[_TOwned]``, ``reconcile`` doesn't consume it). That lets the
# Manager hold a heterogeneous list of ``Controller[_AnyResource]`` /
# ``ControllerRunner[_AnyResource]`` while concrete callers see the
# narrowed parameter on their specific controller.
_TOwned = TypeVar("_TOwned", bound=_AnyResource, covariant=True)


@dataclass(frozen=True)
class ItemRef:
    """Workqueue item: a per-kind (namespace, name) handle.

    Hashable for dedup; trivially comparable for logging. The kind isn't
    on the ref because each controller has its own queue typed against
    its single owned kind.
    """

    namespace: str | None
    name: str


@dataclass(frozen=True)
class Result:
    ok: bool
    requeue_after_s: float | None = None
    reason: str = ""


class EventMapper(Protocol):
    """Translate one watch event into refs of the *owned* kind.

    For the owned kind itself, :func:`identity_mapper` returns the
    event's own ref. For owner-ref'd children (e.g. WorkloadSet
    watching its Workloads), :func:`owner_mapper` finds the parent ref
    on the child's owner_refs.
    """

    def __call__(self, event: WatchEvent[_AnyResource]) -> list[ItemRef]: ...


@dataclass(frozen=True)
class WatchSpec:
    """One declaration of "subscribe to this kind, route via this mapper"."""

    resource_type: type[_AnyResource]
    map_event: EventMapper


class Controller(Protocol, Generic[_TOwned]):
    """One reconciler for one owned kind.

    ``watches`` lists every kind the controller cares about. The runner
    automatically subscribes to ``owned_kind`` with
    :func:`identity_mapper` if absent — concrete controllers usually
    only declare *additional* watches.
    """

    @property
    def owned_kind(self) -> type[_TOwned]: ...

    @property
    def watches(self) -> list[WatchSpec]: ...

    async def reconcile(self, store: InMemoryStore, ref: ItemRef) -> Result: ...


def identity_mapper(event: WatchEvent[_AnyResource]) -> list[ItemRef]:
    return [ItemRef(namespace=event.resource.metadata.namespace, name=event.resource.metadata.name)]


def owner_mapper(controller_kind: str) -> EventMapper:
    """Map child events to parent refs via owner_refs.

    Use when the owned kind is the *parent*: e.g. WorkloadSet watching
    Workloads. Returns the parent refs of the event's resource that
    name ``controller_kind``.
    """

    def _map(event: WatchEvent[_AnyResource]) -> list[ItemRef]:
        out: list[ItemRef] = []
        for ref in event.resource.metadata.owner_refs:
            if ref.kind != controller_kind:
                continue
            out.append(ItemRef(namespace=event.resource.metadata.namespace, name=ref.name))
        return out

    return _map


@final
class ControllerRunner(Generic[_TOwned]):
    """Wires one Controller to the store + a workqueue + worker tasks.

    Lifecycle is the :meth:`running` async context manager:

        async with runner.running():
            ...

    Inside, an :class:`asyncio.TaskGroup` owns every spawned task —
    watch loops and worker loops. On context exit (normal *or*
    exception, including ``CancelledError``), the TaskGroup cancels
    every child and waits; external cancellation propagates without
    a hand-rolled drain. The workqueue is closed *first* so worker
    tasks observe :exc:`QueueClosed` and exit cleanly before the
    TaskGroup escalates to ``Task.cancel()``.
    """

    def __init__(
        self,
        controller: Controller[_TOwned],
        *,
        store: InMemoryStore,
        worker_count: int = 1,
        clock: Clock | None = None,
    ):
        self._controller: Controller[_TOwned] = controller
        self._store: InMemoryStore = store
        self._worker_count: int = worker_count
        self._queue: WorkQueue[ItemRef] = WorkQueue(clock=clock or RealClock())

    @property
    def queue(self) -> WorkQueue[ItemRef]:
        return self._queue

    @asynccontextmanager
    async def running(self) -> AsyncGenerator[Self]:
        """Spawn the watch + worker tasks; yield once they're ready.

        Cleanup ordering on exit, top-to-bottom in the stack (LIFO
        execution from the ``contextlib.ExitStack`` perspective):

        1. Cancel every spawned background task (they're forever-loops
           that wouldn't terminate on their own).
        2. ``TaskGroup.__aexit__`` waits for all those tasks to finish.
        3. ``WorkQueue.close`` — by this point the worker tasks are
           already gone, so this is just final tidiness.

        External cancellation of the surrounding ``async with``
        propagates correctly because that's TaskGroup's own contract.
        """
        watches = list(self._controller.watches)
        owned_kind = self._controller.owned_kind
        if not any(w.resource_type is owned_kind for w in watches):
            watches.insert(0, WatchSpec(resource_type=owned_kind, map_event=identity_mapper))

        try:
            async with contextlib.AsyncExitStack() as stack:
                # Push queue.close as the last thing to run on LIFO exit.
                _ = stack.callback(self._queue.close)
                tg = await stack.enter_async_context(asyncio.TaskGroup())
                tasks: list[asyncio.Task[None]] = []
                ready_futures: list[asyncio.Future[None]] = []
                loop = asyncio.get_running_loop()
                for spec in watches:
                    ready: asyncio.Future[None] = loop.create_future()
                    ready_futures.append(ready)
                    tasks.append(
                        tg.create_task(
                            self._watch_loop(spec, ready),
                            name=f"watch[{owned_kind.__name__}<-{spec.resource_type.__name__}]",
                        )
                    )
                for i in range(self._worker_count):
                    tasks.append(
                        tg.create_task(
                            self._worker_loop(),
                            name=f"reconcile[{owned_kind.__name__}#{i}]",
                        )
                    )
                # Cancel-on-exit pushed *after* TaskGroup so it runs
                # *before* TaskGroup's __aexit__ in LIFO order — the
                # group then awaits the cancelled tasks.
                _ = stack.callback(_cancel_all, tasks)
                # Block until each watch loop has registered its bus
                # subscription. Future-based (not Event-based) so a
                # setup failure inside the loop sets the exception on
                # the future and surfaces here — instead of leaving
                # ``running()`` blocked forever on a never-set Event.
                for ready in ready_futures:
                    _ = await ready
                yield self
        except* WatchOverflow:
            # WatchOverflow is the watch loop's own exit-on-restart
            # signal, but if it ever escapes the loop it's not fatal
            # to the runner's caller.
            pass

    async def _watch_loop(self, spec: WatchSpec, ready: asyncio.Future[None]) -> None:
        """Subscribe to one kind; route events via spec.map_event into the queue.

        ``ready`` is resolved once the bus subscription is registered.
        :meth:`running` awaits it so the caller can publish events
        immediately after the context yields.

        Failure handling has two regimes, distinguished by whether
        ``ready`` has been set:

        - **Setup failure (ready not yet set):** the exception is set
          on the ``ready`` future and re-raised, propagating through
          the surrounding TaskGroup as a fail-fast configuration error.
          This catches bugs like "watched a kind that wasn't registered."
        - **Steady-state failure (ready already set):** the exception
          is logged and the loop pauses before re-subscribing — same
          shape as a k8s informer recovery on transient store errors.

        On WatchOverflow we re-list and re-subscribe regardless of
        regime. Exits when the surrounding TaskGroup cancels this task.
        """
        while True:
            try:
                # Synchronous registration via async-with: enforces
                # cleanup on every exit path (WatchOverflow, exception,
                # cancellation). The previous "open then stream" split
                # leaked subscriptions if a list/snapshot raise jumped
                # past the stream call.
                async with self._store.subscription(spec.resource_type) as sub:
                    if not ready.done():
                        ready.set_result(None)
                    # Re-list current rows: covers both "reconciler
                    # started after data exists" and recovery after
                    # WatchOverflow.
                    for row in self._store.list(spec.resource_type):
                        for ref in spec.map_event(
                            WatchEvent(
                                type=WatchEventType.ADDED,
                                resource=row,
                                resource_version=row.metadata.resource_version,
                            )
                        ):
                            self._queue.add(ref)
                    async for event in sub:
                        for ref in spec.map_event(event):
                            self._queue.add(ref)
            except WatchOverflow:
                logger.warning(f"watch overflow on {spec.resource_type.__name__}; relisting")
                # Loop re-enters the with block and re-lists.
                continue
            except Exception as e:
                if not ready.done():
                    # Setup failure: surface to running() instead of
                    # silently retrying forever.
                    ready.set_exception(e)
                    raise
                logger.exception(f"watch loop on {spec.resource_type.__name__} crashed; restarting")
                # Brief pause before reconnecting to avoid a tight
                # loop if the store is in a degraded state.
                await asyncio.sleep(1.0)

    async def _worker_loop(self) -> None:
        async for ref in self._queue.items():
            # Spawn the reconcile as a subtask so the workqueue can
            # cancel *just this reconcile* (supersede) without killing
            # the worker loop. Without the subtask, the only way to
            # cancel a stale reconcile would be to cancel the worker
            # itself — and that would tear down the whole runner.
            sub: asyncio.Task[Result] = asyncio.create_task(
                self._controller.reconcile(self._store, ref),
                name=f"reconcile-call[{self._controller.owned_kind.__name__}/{ref}]",
            )
            self._queue.register_in_flight_task(ref, sub)
            try:
                result = await sub
            except asyncio.CancelledError:
                self._queue.done(ref, success=False)
                # Distinguish: if the worker itself was cancelled
                # (TaskGroup shutdown), propagate. Otherwise the
                # cancel hit only the subtask — the workqueue
                # superseded this reconcile — and we loop to pick
                # up the requeued ref.
                current = asyncio.current_task()
                if current is not None and current.cancelling() > 0:
                    raise
                continue
            except Exception:
                logger.exception(f"reconcile {self._controller.owned_kind.__name__}/{ref} raised")
                self._queue.done(ref, success=False)
                continue
            self._queue.done(ref, success=result.ok)
            if result.requeue_after_s is not None:
                self._queue.add_after(ref, result.requeue_after_s)


def _cancel_all(tasks: list[asyncio.Task[None]]) -> None:
    for t in tasks:
        _ = t.cancel()


def collect_refs(events: AsyncIterator[WatchEvent[_AnyResource]]) -> AsyncIterator[ItemRef]:
    """Test helper: identity-map an event stream to refs."""

    async def _gen() -> AsyncIterator[ItemRef]:
        async for event in events:
            for ref in identity_mapper(event):
                yield ref

    return _gen()
