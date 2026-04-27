# pyright: reportPrivateUsage=false
"""Runtime ``watch()`` contract: subscribers, overflow, registration ack.

The fan-out shape in ``SubprocessRuntime._publish`` and
``FakeRuntime._publish`` is identical: a per-subscriber bounded
``asyncio.Queue`` plus a ``set`` of subscribers. Both implementations
share the same overflow-handling bug, so a single test against the
fake exercises it.
"""

import asyncio
from datetime import datetime, timezone

import pytest
from orchestrator import (
    FakeRuntime,
    RuntimeEvent,
    RuntimeEventType,
    WorkloadStatus,
)

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


def _event(i: int) -> RuntimeEvent:
    return RuntimeEvent(
        type=RuntimeEventType.STATUS_CHANGED,
        workload_namespace="default",
        workload_name=f"w{i}",
        status=WorkloadStatus(),
        timestamp=datetime.now(timezone.utc),
    )


async def test_watch_overflow_wakes_consumer(fake_runtime: FakeRuntime):
    """Bug (audit round 4): ``_publish`` catches ``QueueFull``, removes
    the slow subscriber from ``_subs``, but never enqueues a sentinel
    on its queue. The consumer's ``await queue.get()`` then blocks
    forever — no new events arrive (it's been dropped), no None wakes
    it. Compare ``WatchBus.Subscription.offer`` which deliberately puts
    a None sentinel on overflow so the iterator surfaces the drop.

    Real-world impact: if the agent's runtime→status loop ever falls
    behind by 256 events, it deadlocks silently and Workload status
    stops mirroring runtime state.
    """
    sentinel_received = asyncio.Event()
    iterator_done = asyncio.Event()

    async def _consume() -> None:
        try:
            async for event in fake_runtime.watch():
                if event is None:
                    sentinel_received.set()
                    continue
                # Block here so the per-sub queue fills up. The agent's
                # real consumer awaits patch_status which can also stall;
                # we simulate by holding the loop indefinitely.
                _ = await asyncio.Event().wait()
        finally:
            iterator_done.set()

    consumer = asyncio.create_task(_consume(), name="t.consumer")
    _ = await asyncio.wait_for(sentinel_received.wait(), timeout=2.0)

    # Per-sub queue is bounded at 256; fire enough to overflow.
    for i in range(300):
        await fake_runtime.emit(_event(i))

    # If the runtime correctly enqueues a None sentinel on overflow, the
    # consumer's iterator returns and ``iterator_done`` fires. With the
    # bug, ``queue.get()`` blocks forever and the wait_for times out.
    try:
        _ = await asyncio.wait_for(iterator_done.wait(), timeout=2.0)
    except TimeoutError:
        pytest.fail(
            (
                "consumer did not wake after watch overflow — _publish dropped "
                "the queue from _subs without enqueuing a None sentinel"
            )
        )
    finally:
        _ = consumer.cancel()
        try:
            await consumer
        except asyncio.CancelledError:
            pass
