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
from typing import Generic, Protocol, TypeVar, final, override


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


_TRef = TypeVar("_TRef", bound=Hashable)


@dataclass(frozen=True, order=True)
class _Scheduled(Generic[_TRef]):
    deadline: float
    seq: int  # monotonic tiebreaker; ensures total order in heap
    ref: _TRef = field(compare=False)


@final
class WorkQueue(Generic[_TRef]):
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

        self._heap: list[_Scheduled[_TRef]] = []
        self._scheduled_refs: set[_TRef] = set()
        self._in_flight: set[_TRef] = set()
        # Refs added while in-flight: rescheduled on done() rather than
        # racing with a parallel processor.
        self._dirty_in_flight: set[_TRef] = set()
        self._failure_count: dict[_TRef, int] = {}

        self._not_empty: asyncio.Event = asyncio.Event()
        self._closed: bool = False
        self._seq: int = 0

    # ---- producer side -------------------------------------------------

    def add(self, ref: _TRef) -> None:
        """Schedule ``ref`` for immediate processing (deduped)."""
        self._add_at(ref, self._clock.now())

    def add_after(self, ref: _TRef, delay_s: float) -> None:
        self._add_at(ref, self._clock.now() + max(0.0, delay_s))

    def _add_at(self, ref: _TRef, deadline: float) -> None:
        if self._closed:
            return
        if ref in self._in_flight:
            self._dirty_in_flight.add(ref)
            return
        if ref in self._scheduled_refs:
            # Already pending. The earlier scheduled entry will fire;
            # don't add a duplicate.
            return
        self._scheduled_refs.add(ref)
        self._seq += 1
        heapq.heappush(self._heap, _Scheduled(deadline=deadline, seq=self._seq, ref=ref))
        self._not_empty.set()

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self._not_empty.set()

    # ---- consumer side -------------------------------------------------

    async def get(self) -> _TRef:
        """Block until a ref is due, then return it.

        Caller must call :meth:`done` after processing — until then the
        ref is "in flight" and won't be redelivered.

        Raises :class:`QueueClosed` once :meth:`close` has been called —
        regardless of whether the heap still has future-deadline items.
        Honoring future deadlines after close would mean spinning on
        ``wait_for(_not_empty)`` because close sets the event permanently;
        instead we treat close as authoritative and drop pending work.
        """
        while True:
            if self._closed:
                raise QueueClosed()
            if not self._heap:
                self._not_empty.clear()
                _ = await self._not_empty.wait()
                continue
            head = self._heap[0]
            now = self._clock.now()
            if head.deadline <= now:
                _ = heapq.heappop(self._heap)
                self._scheduled_refs.discard(head.ref)
                self._in_flight.add(head.ref)
                if not self._heap:
                    self._not_empty.clear()
                return head.ref
            # Sleep until the head is due; new arrivals trigger the event
            # which races with the sleep.
            wait_s = head.deadline - now
            try:
                _ = await asyncio.wait_for(self._not_empty.wait(), timeout=wait_s)
            except asyncio.TimeoutError:
                pass

    def done(self, ref: _TRef, *, success: bool) -> None:
        """Signal that processing of ``ref`` finished.

        Failure schedules an exponential-backoff requeue. Success resets
        the failure count. If ``add(ref)`` was called while processing,
        it's now scheduled (possibly with backoff if this run failed).
        """
        if ref not in self._in_flight:
            return
        self._in_flight.discard(ref)
        was_dirty = ref in self._dirty_in_flight
        self._dirty_in_flight.discard(ref)

        if success:
            _ = self._failure_count.pop(ref, None)
            if was_dirty:
                self._add_at(ref, self._clock.now())
            return

        attempts = self._failure_count.get(ref, 0) + 1
        self._failure_count[ref] = attempts
        delay = min(self._max, self._base * (2.0 ** (attempts - 1)))
        self._add_at(ref, self._clock.now() + delay)

    def forget(self, ref: _TRef) -> None:
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

    def failure_count(self, ref: _TRef) -> int:
        return self._failure_count.get(ref, 0)

    # ---- iteration helper ---------------------------------------------

    async def items(self) -> AsyncIterator[_TRef]:
        """Iterate the queue; stops cleanly on close()."""
        while True:
            try:
                ref = await self.get()
            except QueueClosed:
                return
            yield ref
