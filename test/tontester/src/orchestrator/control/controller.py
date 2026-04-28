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

**Lifecycle.** ``ControllerRunner`` implements the
:class:`~orchestrator.lifecycle.Resource` Protocol:
``async with runner.running():`` spawns the watch + worker tasks and
``__aexit__`` is **synchronous** — it sets ``stop_token``, marks the
runner not-running, requests cancellation of every spawned task, and
closes the workqueue. It does *not* await for the tasks to unwind;
that's :meth:`shutdown`'s job. Caller-cancel of the surrounding
``async with`` propagates because the body's tasks observe the
cancel signal via ``stop_token`` / ``QueueClosed`` and exit on their
own.
"""

import asyncio
import logging
from collections.abc import AsyncGenerator, AsyncIterator
from contextlib import asynccontextmanager
from dataclasses import dataclass
from typing import Generic, Protocol, Self, TypeVar, final, override

from pydantic import BaseModel

from ..errors import WatchOverflow
from ..lifecycle import CheckedExitStack, GracefulAbort, Resource, ResourceNotRunning, StopToken
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
class ControllerRunner(Resource, Generic[_TOwned]):
    """Wires one Controller to the store + a workqueue + worker tasks.

    Implements the :class:`~orchestrator.lifecycle.Resource` shape::

        async with runner.running():
            ...
            await runner.shutdown()  # optional: graceful drain

    Sync ``__aexit__`` discipline: on context exit, the runner sets
    its ``stop_token``, marks itself not-running, cancels every
    spawned background task, and closes the workqueue. No awaits.
    Background tasks observe the cancel and exit on their own (best
    effort under the cancel that's now pending). For graceful drain
    of in-flight reconciles, call ``await runner.shutdown()`` inside
    the with-block (wrap with :func:`asyncio.wait_for` for a budget).

    One-shot: after ``running()`` exits, the runner is "not running"
    and cannot be re-entered. Construct a fresh runner if needed.
    """

    def __init__(
        self,
        controller: Controller[_TOwned],
        *,
        store: InMemoryStore,
        worker_count: int = 1,
        clock: Clock | None = None,
        parent_token: StopToken | None = None,
    ):
        self._controller: Controller[_TOwned] = controller
        self._store: InMemoryStore = store
        self._worker_count: int = worker_count
        self._queue: WorkQueue[ItemRef] = WorkQueue(clock=clock or RealClock())
        self._stop_token: StopToken = (
            parent_token.child() if parent_token is not None else StopToken()
        )
        self._is_running: bool = False
        # Tasks owned during running(). Cleared on __aexit__.
        self._tasks: list[asyncio.Task[None]] = []

    @property
    def queue(self) -> WorkQueue[ItemRef]:
        return self._queue

    @property
    @override
    def stop_token(self) -> StopToken:
        return self._stop_token

    @override
    @asynccontextmanager
    async def running(self) -> AsyncGenerator[Self]:
        """Spawn the watch + worker tasks; yield once they're ready.

        Setup uses :class:`CheckedExitStack` so a stop_token fired
        mid-init aborts gracefully via :exc:`GracefulAbort` (caught
        below). Watch-loop *setup* errors (e.g. an unregistered kind)
        surface synchronously through ``ready.set_exception`` and
        propagate from this method, *after* every spawned task has
        been cancelled.

        On context exit (sync ``finally``):

        1. ``_is_running`` flips to False.
        2. ``stop_token.set()`` — cooperative loops observe and exit.
        3. ``queue.close()`` — workers wake from ``QueueClosed``.
        4. Every spawned task is sent ``cancel()`` (idempotent).

        No awaits in the cleanup — Resource discipline.
        """
        if self._is_running:
            raise ResourceNotRunning("ControllerRunner.running re-entered while already running")

        watches = list(self._controller.watches)
        owned_kind = self._controller.owned_kind
        if not any(w.resource_type is owned_kind for w in watches):
            watches.insert(0, WatchSpec(resource_type=owned_kind, map_event=identity_mapper))

        loop = asyncio.get_running_loop()
        ready_futures: list[asyncio.Future[None]] = []
        try:
            async with CheckedExitStack(self._stop_token):
                for spec in watches:
                    ready: asyncio.Future[None] = loop.create_future()
                    ready_futures.append(ready)
                    task = asyncio.create_task(
                        self._watch_loop(spec, ready),
                        name=f"watch[{owned_kind.__name__}<-{spec.resource_type.__name__}]",
                    )
                    task.add_done_callback(_drain_task_exception)
                    self._tasks.append(task)
                for i in range(self._worker_count):
                    task = asyncio.create_task(
                        self._worker_loop(),
                        name=f"reconcile[{owned_kind.__name__}#{i}]",
                    )
                    task.add_done_callback(_drain_task_exception)
                    self._tasks.append(task)
                try:
                    # Setup-failure surface: ``_watch_loop`` sets the
                    # exception on its ``ready`` future before raising,
                    # so awaiting ``ready`` re-raises here. We catch
                    # ``BaseException`` to ensure the cancel-everything
                    # path runs regardless of exception type.
                    for ready in ready_futures:
                        _ = await ready
                except BaseException:
                    self._cancel_all_tasks()
                    raise
                self._is_running = True
                try:
                    yield self
                finally:
                    # SYNC. No await. Cascading sync cleanup only.
                    self._is_running = False
                    self._stop_token.set()
                    self._queue.close()
                    self._cancel_all_tasks()
        except GracefulAbort:
            # stop_token fired mid-init via CheckedExitStack safe-point.
            # Convert to ResourceNotRunning so the caller sees a clear
            # semantic error instead of @asynccontextmanager's
            # RuntimeError("generator didn't yield").
            raise ResourceNotRunning(
                "ControllerRunner.running entered with stop_token already set"
            ) from None

    @override
    async def shutdown(self) -> None:
        """Graceful drain: signal stop, wait for spawned tasks to finish.

        Must be called inside ``running()``; raises
        :exc:`ResourceNotRunning` otherwise.

        Caller controls the budget by wrapping with
        :func:`asyncio.wait_for`. Caller-cancel propagates as
        :exc:`asyncio.CancelledError`; ``__aexit__`` reconciles any
        half-state.
        """
        if not self._is_running:
            raise ResourceNotRunning("ControllerRunner.shutdown called outside running()")
        self._stop_token.set()
        self._queue.close()
        self._cancel_all_tasks()
        if self._tasks:
            _ = await asyncio.gather(*self._tasks, return_exceptions=True)

    def _cancel_all_tasks(self) -> None:
        for t in self._tasks:
            _ = t.cancel()

    async def _watch_loop(self, spec: WatchSpec, ready: asyncio.Future[None]) -> None:
        """Subscribe to one kind; route events via spec.map_event into the queue.

        ``ready`` is resolved once the bus subscription is registered.
        :meth:`running` awaits it so the caller can publish events
        immediately after the context yields.

        Failure handling has two regimes, distinguished by whether
        ``ready`` has been set:

        - **Setup failure (ready not yet set):** the exception is set
          on the ``ready`` future and re-raised. ``running()`` sees
          it via the awaited future and tears down every spawned
          task before propagating. Catches bugs like "watched a kind
          that wasn't registered."
        - **Steady-state failure (ready already set):** the exception
          is logged and the loop pauses before re-subscribing — same
          shape as a k8s informer recovery on transient store errors.

        On WatchOverflow we re-list and re-subscribe regardless of
        regime. Exits when the runner cancels this task on shutdown.
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
                # (runner shutdown), propagate. Otherwise the cancel
                # hit only the subtask — the workqueue superseded
                # this reconcile — and we loop to pick up the
                # requeued ref.
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


def _drain_task_exception(task: asyncio.Task[object]) -> None:
    """Suppress the "task exception was never retrieved" warning.

    Spawned tasks that die on their own (without being awaited by
    ``shutdown()``) would otherwise log a noisy unraisable warning at
    GC time. We log the exception here for debuggability and consider
    it retrieved.
    """
    if task.cancelled():
        return
    exc = task.exception()
    if exc is not None:
        logger.error(
            f"controller runner task {task.get_name()} crashed",
            exc_info=exc,
        )


def collect_refs(events: AsyncIterator[WatchEvent[_AnyResource]]) -> AsyncIterator[ItemRef]:
    """Test helper: identity-map an event stream to refs."""

    async def _gen() -> AsyncIterator[ItemRef]:
        async for event in events:
            for ref in identity_mapper(event):
                yield ref

    return _gen()
