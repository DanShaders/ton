# pyright: reportPrivateUsage=false
"""Watch bus + store-subscribe semantics.

Goals:

- ADD/MODIFY/DELETE events fire in resource_version order.
- DELETED carries the pre-delete resource (k8s convention).
- Slow consumers don't block the bus — they get WatchOverflow and
  must re-list.
- Multiple subscribers each receive every event.
"""

import asyncio

import pytest
from orchestrator import (
    Container,
    HostBinaryImage,
    InMemoryStore,
    Metadata,
    WatchEventType,
    WatchOverflow,
    Workload,
    WorkloadSpec,
)
from orchestrator.testing import wait_for_asyncio_idle

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


def _wl(name: str, *, env: str = "x") -> Workload:
    return Workload(
        metadata=Metadata(name=name, namespace="default"),
        spec=WorkloadSpec(
            containers=[
                Container(
                    name="primary",
                    image=HostBinaryImage(path="/bin/true"),
                    env={"E": env},
                )
            ],
        ),
    )


async def test_subscribe_receives_add_modify_delete(store: InMemoryStore):
    received: list[tuple[WatchEventType, str, int]] = []

    async def _consume() -> None:
        async for event in store.subscribe(Workload):
            received.append((event.type, event.resource.metadata.name, event.resource_version))
            if len(received) >= 3:
                return

    task = asyncio.create_task(_consume())
    await wait_for_asyncio_idle()

    _ = await store.apply(_wl("a"))
    _ = await store.apply(_wl("a", env="v2"))
    await store.delete(Workload, namespace="default", name="a")

    await asyncio.wait_for(task, timeout=2.0)
    assert [(t, n) for t, n, _ in received] == [
        (WatchEventType.ADDED, "a"),
        (WatchEventType.MODIFIED, "a"),
        (WatchEventType.DELETED, "a"),
    ]
    rvs = [rv for _, _, rv in received]
    assert rvs == sorted(rvs)


async def test_two_subscribers_each_get_every_event(store: InMemoryStore):
    a_seen: list[str] = []
    b_seen: list[str] = []

    async def _watch(out: list[str]) -> None:
        async for event in store.subscribe(Workload):
            out.append(event.type)
            if len(out) >= 2:
                return

    t_a = asyncio.create_task(_watch(a_seen))
    t_b = asyncio.create_task(_watch(b_seen))
    await wait_for_asyncio_idle()

    _ = await store.apply(_wl("x"))
    _ = await store.apply(_wl("x", env="v2"))

    _ = await asyncio.wait_for(asyncio.gather(t_a, t_b), timeout=2.0)
    assert a_seen == [WatchEventType.ADDED, WatchEventType.MODIFIED]
    assert b_seen == [WatchEventType.ADDED, WatchEventType.MODIFIED]


async def test_subscription_close_unregisters_from_bus(store: InMemoryStore):
    """Round-2 finding 1.2: close() must remove the token from the bus's
    _subscriptions set, not just enqueue None on the per-sub queue.
    Otherwise, opening + closing without iterating leaks the token
    (only a subsequent publish that finds the queue closed would
    discard it — and on a quiet bus that may never happen).
    """
    state = store._kinds[Workload]
    assert state.bus.subscriber_count == 0
    async with store.subscription(Workload):
        # Don't iterate — exit cleanly via context manager.
        assert state.bus.subscriber_count == 1
    # After context exit the bus must reflect the unregistration.
    assert state.bus.subscriber_count == 0


async def test_overflow_raises_watch_overflow(store: InMemoryStore):
    """A consumer that can't keep up gets booted with WatchOverflow.

    Tighten the per-subscription queue to 2 and synchronously fire 10
    events. The iterator surfaces overflow on its next loop iteration.
    """
    from orchestrator.store import WatchEvent
    from orchestrator.store.watch import WatchBus

    state = store._kinds[Workload]
    state.bus = WatchBus(Workload.__name__, queue_size=2)

    async def _consume() -> None:
        async for _ in store.subscribe(Workload):
            await asyncio.sleep(0)

    task = asyncio.create_task(_consume())
    await wait_for_asyncio_idle()

    # Synchronously fire enough events to overrun the queue.
    for i in range(10):
        state.bus.publish(
            WatchEvent(
                type=WatchEventType.ADDED,
                resource=_wl(f"w{i}"),
                resource_version=i,
            )
        )

    # Subscriber should fail with WatchOverflow once the queue fills.
    with pytest.raises(WatchOverflow):
        await asyncio.wait_for(task, timeout=2.0)
