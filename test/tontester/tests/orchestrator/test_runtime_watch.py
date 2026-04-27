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
from orchestrator.testing import wait_for_asyncio_idle

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


def _event(i: int) -> RuntimeEvent:
    return RuntimeEvent(
        type=RuntimeEventType.STATUS_CHANGED,
        workload_namespace="default",
        workload_name=f"w{i}",
        status=WorkloadStatus(),
        timestamp=datetime.now(timezone.utc),
    )


async def test_watch_overflow_raises_broadcast_overflow(fake_runtime: FakeRuntime):
    """Bug 2 (audit round 4) — original shape: ``_publish`` dropped a
    slow subscriber from ``_subs`` without enqueuing a sentinel, so
    its consumer's ``await queue.get()`` blocked forever — runtime →
    status reflection stopped silently.

    The fix is structural: every Runtime backend's fan-out is
    implemented in terms of :class:`BroadcastQueue`, the same
    primitive ``WatchBus`` uses. ``BroadcastQueue.publish`` does
    enqueue the None sentinel on overflow, and the iterator raises
    :exc:`BroadcastOverflow` on the next poll — caller can re-list
    and re-subscribe (k8s informer pattern).

    This test pins the new contract: if a subscriber falls behind,
    iterator raises a typed overflow exception within bounded time.
    """
    from orchestrator.broadcast import BroadcastOverflow

    overflow_raised = asyncio.Event()

    async def _consume() -> None:
        async with fake_runtime.watch() as events:
            try:
                # Iterate without blocking in the body. We synchronously
                # publish 300 events below before the consumer gets to
                # run, filling the per-sub queue past its 256 limit;
                # BroadcastQueue then sets the overflow flag on this
                # subscriber. When the consumer drains the queued
                # events, the iterator surfaces ``BroadcastOverflow``.
                async for _event in events:
                    pass
            except BroadcastOverflow:
                overflow_raised.set()

    consumer = asyncio.create_task(_consume(), name="t.consumer")
    # Let the consumer enter watch() (registers on the broadcast set),
    # then synchronously fire enough events to overrun the queue
    # before the consumer gets a chance to drain.
    await wait_for_asyncio_idle()
    for i in range(300):
        fake_runtime.emit(_event(i))

    try:
        _ = await asyncio.wait_for(overflow_raised.wait(), timeout=2.0)
    except TimeoutError:
        pytest.fail(
            (
                "consumer did not surface BroadcastOverflow after the runtime's "
                "per-subscriber queue overflowed; the broadcast contract is broken"
            )
        )
    finally:
        _ = consumer.cancel()
        try:
            await consumer
        except asyncio.CancelledError:
            pass
