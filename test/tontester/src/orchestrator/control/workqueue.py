"""k8s-style rate-limited dedup workqueue.

One queue per controller. Each item is an :class:`ItemRef` — typically
``(namespace, name)`` for the kind the controller owns. Properties:

- **Dedup** — adding the same ref twice while it's pending coalesces.
  This is what makes "watch event for X arrives 5 times during a
  reconcile burst" cost one reconcile, not five.
- **Per-item rate limit** — a failed reconcile is requeued with
  exponential backoff keyed by the ref. A successful reconcile resets
  the counter.
- **In-flight tracking** — the queue refuses to redeliver an item that
  is currently being processed. The processor signals completion via
  :meth:`done`. Until then, ``add(ref)`` marks "process again after
  current run finishes" — coalescing works the same way as for pending
  items.
- **Cancel-aware** — ``get`` is awaitable; closing the queue wakes
  pending getters with :exc:`StopAsyncIteration` (via the iterator
  helper) or :exc:`QueueClosed` for the explicit ``get()``.
"""

import asyncio
import heapq
import time
from collections.abc import AsyncIterator, Hashable
from dataclasses import dataclass, field
from typing import Protocol, final, override


class QueueClosed(RuntimeError):
    pass


class Clock(Protocol):
    """Time source — injected so tests can run on aiotools' VirtualClock."""

    def now(self) -> float: ...


@final
class RealClock(Clock):
    @override
    def now(self) -> float:
        try:
            return asyncio.get_running_loop().time()
        except RuntimeError:
            return time.monotonic()


@dataclass(frozen=True, order=True)
class _Scheduled[T: Hashable]:
    deadline: float
    seq: int  # monotonic tiebreaker; ensures total order in heap
    ref: T = field(compare=False)


@final
class WorkQueue[T: Hashable]:
    """Rate-limited dedup queue.

    The queue is a min-heap on ``(deadline, seq)``. ``add(ref)`` schedules
    immediately if ``ref`` isn't already pending; ``add_after(ref, delay)``
    schedules with a delay. ``done(ref, success)`` clears the in-flight
    flag; on failure the next requeue uses exponential backoff.
    """

    def __init__(
        self,
        *,
        base_backoff_s: float = 0.005,
        max_backoff_s: float = 30.0,
        clock: Clock | None = None,
    ):
        self._base = base_backoff_s
        self._max = max_backoff_s
        self._clock: Clock = clock or RealClock()

        self._heap: list[_Scheduled[T]] = []
        self._scheduled_refs: set[T] = set()
        self._in_flight: set[T] = set()
        # Tracks the worker's reconcile task per in-flight ref so
        # ``_add_at`` can supersede it: when a new event arrives for
        # a ref already being reconciled, we cancel the in-flight task
        # so the worker re-runs against current state instead of
        # finishing a now-obsolete spec. The supervisor pattern in
        # SubprocessRuntime makes this cancellation safe — RAII
        # unwinds any in-progress process spawn.
        self._in_flight_tasks: dict[T, asyncio.Task[object]] = {}
        # Refs added while in-flight: rescheduled on done() at the
        # *earliest* requested deadline. Storing the deadline (not
        # just a flag) keeps add_after's debounce request alive across
        # the in-flight window — otherwise a watch event mid-reconcile
        # would silently collapse a 10s requeue_after_s into
        # "immediate" (round-5 #6).
        self._dirty_in_flight: dict[T, float] = {}
        # Refs whose in-flight reconcile we cancelled (supersede). On
        # done() we use this to skip failure-backoff increment — the
        # cancellation was our doing, not the reconciler's failure.
        self._superseded: set[T] = set()
        self._failure_count: dict[T, int] = {}

        # Per-call wakeup futures. Each ``get`` waiter appends; producers
        # resolve one. A bare ``asyncio.Event`` was the original
        # implementation but had two bugs: stays-set after add means
        # ``await wait_for(event, timeout=wait_s)`` returns immediately
        # in the future-deadline branch (busy-spin); and a single Event
        # only wakes one consumer at a time deterministically. Per-call
        # futures fix both at the cost of one Future allocation per get
        # iteration.
        self._waiters: list[asyncio.Future[None]] = []
        self._closed: bool = False
        self._seq: int = 0

    # ---- producer side -------------------------------------------------

    def add(self, ref: T) -> None:
        """Schedule ``ref`` for immediate processing (deduped)."""
        self._add_at(ref, self._clock.now())

    def add_after(self, ref: T, delay_s: float) -> None:
        self._add_at(ref, self._clock.now() + max(0.0, delay_s))

    def _add_at(self, ref: T, deadline: float) -> None:
        if self._closed:
            return
        if ref in self._in_flight:
            # Earliest deadline wins: an urgent ``add()`` (deadline=now)
            # racing with a debouncing ``add_after(ref, 10s)`` should
            # fire immediately on done(), not after 10s.
            existing = self._dirty_in_flight.get(ref)
            self._dirty_in_flight[ref] = deadline if existing is None else min(existing, deadline)
            # Supersede: cancel the in-flight reconcile so the worker
            # re-runs against current state. The worker catches the
            # cancel, calls done(), and we requeue at the dirty
            # deadline. If the in-flight task happens to finish before
            # the cancel takes effect, done()'s dirty path requeues
            # anyway — same end state.
            self._superseded.add(ref)
            task = self._in_flight_tasks.get(ref)
            if task is not None and not task.done():
                _ = task.cancel()
            return
        if ref in self._scheduled_refs:
            # Already pending. The earlier scheduled entry will fire;
            # don't add a duplicate.
            return
        self._scheduled_refs.add(ref)
        self._seq += 1
        heapq.heappush(self._heap, _Scheduled(deadline=deadline, seq=self._seq, ref=ref))
        self._wake_one()

    def register_in_flight_task(self, ref: T, task: asyncio.Task[object]) -> None:
        """Worker registers its current reconcile task so the queue
        can supersede-cancel it on a fresh add().

        Called by :class:`ControllerRunner` worker right after pulling
        a ref via :meth:`get` and spawning the reconcile subtask. If
        the ref isn't in-flight (e.g. caller misuse), this is a no-op.
        """
        if ref in self._in_flight:
            self._in_flight_tasks[ref] = task

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self._wake_all()

    def _wake_one(self) -> None:
        """Resolve one waiter's future, if any."""
        while self._waiters:
            w = self._waiters.pop(0)
            if not w.done():
                w.set_result(None)
                return

    def _wake_all(self) -> None:
        """Resolve every waiter's future. Used by :meth:`close`."""
        for w in self._waiters:
            if not w.done():
                w.set_result(None)
        self._waiters.clear()

    # ---- consumer side -------------------------------------------------

    async def get(self) -> T:
        """Block until a ref is due, then return it.

        Caller must call :meth:`done` after processing — until then the
        ref is "in flight" and won't be redelivered.

        Raises :class:`QueueClosed` once :meth:`close` has been called —
        regardless of whether the heap still has future-deadline items.
        Honoring future deadlines after close would mean spinning on
        a permanently-set wakeup; instead we treat close as
        authoritative and drop pending work.
        """
        while True:
            if self._closed:
                raise QueueClosed()
            head = self._heap[0] if self._heap else None
            if head is not None:
                now = self._clock.now()
                if head.deadline <= now:
                    _ = heapq.heappop(self._heap)
                    self._scheduled_refs.discard(head.ref)
                    self._in_flight.add(head.ref)
                    return head.ref
                wait_s: float | None = head.deadline - now
            else:
                wait_s = None  # heap empty: wait indefinitely

            # Fresh waiter per iteration. There's no lost-wakeup race
            # because the loop is single-threaded: the heap snapshot
            # above and this Future creation happen before any await,
            # so a subsequent ``_add_at`` will see this Future on
            # ``self._waiters`` and resolve it.
            loop = asyncio.get_running_loop()
            waiter: asyncio.Future[None] = loop.create_future()
            self._waiters.append(waiter)
            try:
                if wait_s is None:
                    _ = await waiter
                else:
                    _ = await asyncio.wait_for(waiter, timeout=wait_s)
            except asyncio.TimeoutError:
                pass
            finally:
                if waiter in self._waiters:
                    self._waiters.remove(waiter)

    def done(self, ref: T, *, success: bool) -> None:
        """Signal that processing of ``ref`` finished.

        Failure schedules an exponential-backoff requeue. Success resets
        the failure count. If ``add(ref)`` / ``add_after(ref, N)`` was
        called while processing, the earliest requested deadline (saved
        in ``_dirty_in_flight``) is honored on the requeue.

        If the in-flight reconcile was *superseded* (cancelled by the
        queue itself because a fresh ``add()`` arrived), the
        cancellation is not treated as a failure — no backoff
        increment — and the requeue uses the dirty deadline directly.
        """
        if ref not in self._in_flight:
            return
        self._in_flight.discard(ref)
        _ = self._in_flight_tasks.pop(ref, None)
        was_superseded = ref in self._superseded
        self._superseded.discard(ref)
        dirty_deadline = self._dirty_in_flight.pop(ref, None)

        if was_superseded:
            # Our cancel; not the reconciler's failure. Don't penalize
            # via backoff — just requeue at the dirty deadline (which
            # was set by the supersede-triggering add).
            if dirty_deadline is not None:
                self._add_at(ref, dirty_deadline)
            return

        if success:
            _ = self._failure_count.pop(ref, None)
            if dirty_deadline is not None:
                self._add_at(ref, dirty_deadline)
            return

        attempts = self._failure_count.get(ref, 0) + 1
        self._failure_count[ref] = attempts
        backoff_delay = min(self._max, self._base * (2.0 ** (attempts - 1)))
        backoff_deadline = self._clock.now() + backoff_delay
        # On failure, both the dirty deadline and the backoff deadline
        # are "wait at least this long." Pick the later — backoff
        # protects against hot loops on persistent failure; dirty
        # deadline protects an explicit add_after's debounce.
        deadline = (
            max(backoff_deadline, dirty_deadline)
            if dirty_deadline is not None
            else backoff_deadline
        )
        self._add_at(ref, deadline)

    def forget(self, ref: T) -> None:
        """Drop any failure-count history for ``ref``.

        Use when a ref becomes irrelevant (object deleted) and we want
        future handling not to inherit the old backoff.
        """
        _ = self._failure_count.pop(ref, None)

    # ---- introspection -------------------------------------------------

    def pending_count(self) -> int:
        return len(self._heap)

    def in_flight_count(self) -> int:
        return len(self._in_flight)

    def failure_count(self, ref: T) -> int:
        return self._failure_count.get(ref, 0)

    # ---- iteration helper ---------------------------------------------

    async def items(self) -> AsyncIterator[T]:
        """Iterate the queue; stops cleanly on close()."""
        while True:
            try:
                ref = await self.get()
            except QueueClosed:
                return
            yield ref
