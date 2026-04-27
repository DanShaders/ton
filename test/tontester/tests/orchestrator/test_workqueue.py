# pyright: reportPrivateUsage=false
"""WorkQueue: dedup, backoff, in-flight tracking."""

import asyncio
from typing import final, override

import pytest
from orchestrator import WorkQueue
from orchestrator.control.workqueue import Clock, QueueClosed
from orchestrator.testing import wait_for_asyncio_idle

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


@final
class _ManualClock(Clock):
    def __init__(self) -> None:
        self._t: float = 0.0

    @override
    def now(self) -> float:
        return self._t

    def advance(self, by: float) -> None:
        self._t += by


async def test_add_dedups():
    q: WorkQueue[str] = WorkQueue()
    q.add("a")
    q.add("a")
    q.add("b")
    assert q.pending_count() == 2

    first = await q.get()
    assert first in {"a", "b"}
    q.done(first, success=True)
    second = await q.get()
    assert second != first
    q.done(second, success=True)


async def test_done_clears_in_flight():
    q: WorkQueue[str] = WorkQueue()
    q.add("a")
    item = await q.get()
    assert q.in_flight_count() == 1
    q.done(item, success=True)
    assert q.in_flight_count() == 0


async def test_failure_backoff():
    """A failed item is requeued with exponential backoff."""
    clock = _ManualClock()
    q: WorkQueue[str] = WorkQueue(base_backoff_s=0.01, max_backoff_s=1.0, clock=clock)
    q.add("a")
    item = await q.get()
    q.done(item, success=False)
    assert q.failure_count("a") == 1
    assert q.pending_count() == 1
    # Item is in the heap but not yet due.
    item_due = q._heap[0].deadline
    assert abs(item_due - 0.01) < 0.001


async def test_add_after_during_in_flight_preserves_deadline():
    """Round-5 #6: ``WorkQueue.add_after(ref, delay)`` while ``ref``
    is in-flight currently sets a flag and forgets the delay; on
    ``done()`` the dirty re-queue uses ``now()`` regardless. Any
    controller that returned ``Result(ok=True, requeue_after_s=N)``
    intending to debounce loses that delay if a watch event arrives
    mid-flight.

    Pin the contract: the requested deadline is preserved; the heap
    entry's deadline is at least the requested time.
    """
    clock = _ManualClock()
    q: WorkQueue[str] = WorkQueue(clock=clock)
    q.add("a")
    item = await q.get()  # in-flight
    q.add_after("a", delay_s=10.0)  # request 10s debounce
    q.done(item, success=True)
    assert q.pending_count() == 1
    assert q._heap[0].deadline >= 10.0, (
        f"add_after's deadline was lost; heap deadline is {q._heap[0].deadline}, expected >= 10.0"
    )


async def test_add_during_in_flight_does_not_overwrite_later_deadline():
    """If both ``add(ref)`` (deadline=now) and ``add_after(ref, 10s)``
    arrive while ``ref`` is in-flight, the *earlier* deadline wins
    — the urgent signal trumps the debounce. Tests that the
    deadline-tracking dict uses ``min()``.
    """
    clock = _ManualClock()
    q: WorkQueue[str] = WorkQueue(clock=clock)
    q.add("a")
    item = await q.get()
    q.add_after("a", delay_s=10.0)  # debounce
    q.add("a")  # urgent — should win
    q.done(item, success=True)
    assert q.pending_count() == 1
    assert q._heap[0].deadline <= 0.001, (
        f"urgent add() didn't trump add_after's deadline; heap deadline is {q._heap[0].deadline}"
    )


async def test_re_add_during_processing_requeues_after_done():
    """Add(ref) while ref is in-flight: requeues after done."""
    q: WorkQueue[str] = WorkQueue()
    q.add("a")
    item = await q.get()
    q.add("a")  # in-flight; deferred
    assert q.pending_count() == 0
    assert q.in_flight_count() == 1
    q.done(item, success=True)
    assert q.in_flight_count() == 0
    assert q.pending_count() == 1


async def test_close_wakes_pending_get():
    q: WorkQueue[str] = WorkQueue()

    async def _consumer() -> str:
        return await q.get()

    task = asyncio.create_task(_consumer())
    await wait_for_asyncio_idle()
    q.close()
    with pytest.raises(QueueClosed):
        _ = await task


async def test_close_with_future_deadline_items_does_not_busy_loop():
    """Regression: close() permanently sets _not_empty; if heap held a
    future-deadline item, get() would spin on wait_for-with-set-event
    until the deadline passed. Now close is authoritative."""
    q: WorkQueue[str] = WorkQueue()
    q.add_after("a", delay_s=30.0)
    q.close()
    with pytest.raises(QueueClosed):
        _ = await q.get()


@final
class _CountingClock(Clock):
    """Wraps an inner clock and counts every ``now()`` call.

    A healthy ``WorkQueue.get()`` calls ``now()`` O(1) times per actual
    wakeup; a busy-looping one calls it once per spin iteration.
    """

    def __init__(self, inner: Clock) -> None:
        self._inner: Clock = inner
        self.call_count: int = 0

    @override
    def now(self) -> float:
        self.call_count += 1
        return self._inner.now()


async def test_get_does_not_busy_spin_on_future_deadline_item():
    """Bug (audit round 4): when the heap has a single future-deadline
    item, ``_not_empty`` is set (from the ``add_after``) and never cleared
    — the clear-event branch only runs when the heap is empty. ``get()``
    then loops doing ``await wait_for(self._not_empty.wait(), timeout=wait_s)``,
    which returns immediately because the event is already set, then
    re-evaluates the deadline (still in the future) and goes around again.
    The loop spins at ~100% CPU until the deadline arrives, also failing
    to yield enough for VirtualClock to advance virtual time — so any test
    that triggers it just hangs.

    Detect by counting ``clock.now()`` invocations: a healthy
    implementation calls it a small handful of times; the buggy one calls
    it many thousands of times in a brief window.
    """
    inner = _ManualClock()
    clock = _CountingClock(inner)
    q: WorkQueue[str] = WorkQueue(clock=clock)
    q.add_after("a", delay_s=30.0)

    async def _consume() -> str:
        return await q.get()

    task = asyncio.create_task(_consume())
    # Explicit ``sleep(0)`` count rather than ``wait_for_asyncio_idle``
    # because the bug under test is exactly "loop never goes idle":
    # the helper would itself hang waiting for stability that never
    # happens under VirtualClock.
    for _ in range(50):
        await asyncio.sleep(0)

    _ = task.cancel()
    with pytest.raises(asyncio.CancelledError):
        _ = await task

    # Healthy ceiling is small (a few calls per actual wake event).
    # Buggy version produces hundreds in this window.
    assert clock.call_count < 100, (
        f"WorkQueue.get() called clock.now() {clock.call_count} times waiting "
        f"for a single future-deadline item — busy-spin on permanently-set "
        f"_not_empty event"
    )
