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

**Cancel safety.** Worker tasks are allowed to be cancelled mid-
reconcile. The reconciler's own resources must be either trivial
(usually true: a controller calls ``store.apply``/``store.patch_status``
which are themselves atomic and cancel-safe at the await boundary) or
managed by the controller's own ``async with`` blocks. The runner does
not introduce a cancellation budget — :class:`Manager.shutdown`
gathers the workers with a deadline and force-cancels stragglers.
"""

import asyncio
import logging
from collections.abc import AsyncIterator
from dataclasses import dataclass
from typing import Generic, Protocol, TypeVar, final

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

    Lifecycle is :meth:`start` / :meth:`stop`; both are idempotent. The
    Manager calls them under its own AsyncExitStack, so a partial
    initialization is rolled back cleanly on construction failure.
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
        self._tasks: list[asyncio.Task[None]] = []
        self._started: bool = False
        self._stopping: bool = False

    @property
    def queue(self) -> WorkQueue[ItemRef]:
        return self._queue

    async def start(self) -> None:
        if self._started:
            return
        self._started = True
        # Auto-subscribe to the owned kind so reconciles happen on direct edits.
        watches = list(self._controller.watches)
        owned_kind = self._controller.owned_kind
        if not any(w.resource_type is owned_kind for w in watches):
            watches.insert(0, WatchSpec(resource_type=owned_kind, map_event=identity_mapper))

        # Each watch loop signals via its ready event once it's registered
        # with the store's bus; start() waits on all of them so that any
        # apply() the caller does after start() returns is guaranteed to
        # be observed. Without this, watch loop tasks scheduled by
        # create_task may not actually run before the caller's first
        # apply, and the corresponding event would be published into a
        # bus with zero subscribers and lost.
        #
        # The whole task-creation + ready.wait phase is wrapped in a
        # try/except BaseException so a cancellation (or any other
        # unexpected exception) during start tears down the tasks we
        # already created. Without this, Manager's stack-based rollback
        # can't reach our tasks because Manager.start only registers
        # _safe_stop after runner.start() returns successfully.
        try:
            ready_events: list[asyncio.Event] = []
            for spec in watches:
                ready = asyncio.Event()
                ready_events.append(ready)
                self._tasks.append(
                    asyncio.create_task(
                        self._watch_loop(spec, ready),
                        name=f"watch[{owned_kind.__name__}<-{spec.resource_type.__name__}]",
                    )
                )
            for i in range(self._worker_count):
                self._tasks.append(
                    asyncio.create_task(
                        self._worker_loop(),
                        name=f"reconcile[{owned_kind.__name__}#{i}]",
                    )
                )
            for ready in ready_events:
                _ = await ready.wait()
        except BaseException:
            await self.stop()
            raise

    async def stop(self) -> None:
        if self._stopping:
            return
        self._stopping = True
        self._queue.close()
        for task in self._tasks:
            _ = task.cancel()
        for task in self._tasks:
            try:
                await task
            except asyncio.CancelledError, Exception:
                # Each worker logs its own errors; we just want to drain.
                pass
        self._tasks.clear()

    async def _watch_loop(self, spec: WatchSpec, ready: asyncio.Event) -> None:
        """Subscribe to one kind; route events via spec.map_event into the queue.

        ``ready`` signals to :meth:`start` that the bus subscription
        has been registered — start() awaits this so the caller can
        publish events immediately after start returns.

        On WatchOverflow we re-list and re-subscribe — same as k8s
        informers. The list path also drains all current rows into the
        workqueue so a controller that started against pre-existing
        data still reconciles it.
        """
        while not self._stopping:
            try:
                # Synchronous registration via async-with: enforces
                # cleanup on every exit path (WatchOverflow, exception,
                # cancellation). The previous "open then stream" split
                # leaked subscriptions if a list/snapshot raise jumped
                # past the stream call.
                async with self._store.subscription(spec.resource_type) as sub:
                    ready.set()
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
                # Outer loop will re-enter the with block and re-list.
                continue
            except asyncio.CancelledError:
                return
            except Exception:
                logger.exception(f"watch loop on {spec.resource_type.__name__} crashed; restarting")
                # Brief pause before reconnecting to avoid a tight loop
                # if the store is in a degraded state.
                await asyncio.sleep(1.0)

    async def _worker_loop(self) -> None:
        async for ref in self._queue.items():
            try:
                result = await self._controller.reconcile(self._store, ref)
            except asyncio.CancelledError:
                self._queue.done(ref, success=False)
                raise
            except Exception:
                logger.exception(f"reconcile {self._controller.owned_kind.__name__}/{ref} raised")
                self._queue.done(ref, success=False)
                continue
            self._queue.done(ref, success=result.ok)
            if result.requeue_after_s is not None:
                self._queue.add_after(ref, result.requeue_after_s)


def collect_refs(events: AsyncIterator[WatchEvent[_AnyResource]]) -> AsyncIterator[ItemRef]:
    """Test helper: identity-map an event stream to refs."""

    async def _gen() -> AsyncIterator[ItemRef]:
        async for event in events:
            for ref in identity_mapper(event):
                yield ref

    return _gen()
