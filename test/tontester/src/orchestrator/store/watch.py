"""In-process pub/sub for resource changes.

One :class:`WatchBus` per kind. Subscribers get an
``AsyncIterator[WatchEvent]``; publishing fans out to every subscriber.

**Backpressure rule**: subscriber queues are bounded. If a queue fills
because the consumer fell behind, we drop the subscriber and raise
:class:`~orchestrator.errors.WatchOverflow` from the iterator. The
caller is expected to re-list and re-subscribe with the new
resource_version — same shape as k8s "410 Gone". Dropping over silently
queueing keeps the *publisher* fast (every publish is O(subscribers),
no waiting on a slow client).

Publish ordering is total per kind: every event has a strictly
increasing ``resource_version`` set by :class:`InMemoryStore` under its
per-kind lock. Subscribers receive events in publish order; cross-kind
ordering is undefined (and that's fine — controllers reconcile per
resource, not per cross-resource transaction).
"""

import asyncio
from collections.abc import AsyncIterator
from dataclasses import dataclass
from enum import StrEnum
from typing import Generic, TypeVar, final

from pydantic import BaseModel

from ..errors import WatchOverflow
from ..resources import ResourceLike


class WatchEventType(StrEnum):
    ADDED = "added"
    MODIFIED = "modified"
    DELETED = "deleted"


_TRes = TypeVar("_TRes", bound=ResourceLike[BaseModel, BaseModel])


@dataclass(frozen=True)
class WatchEvent(Generic[_TRes]):
    """One delta on the bus.

    ``resource_version`` is the post-write version of the object. For
    ``DELETED`` events the object carries the final pre-delete state
    (k8s convention) so consumers can read its labels for cleanup.
    """

    type: WatchEventType
    resource: _TRes
    resource_version: int


@final
class Subscription(Generic[_TRes]):
    """One subscriber's queue. Closed on overflow or explicit unsubscribe.

    Constructed by :meth:`WatchBus.open` (synchronous registration);
    consumed by :meth:`WatchBus.stream` (async iteration). Splitting
    the two halves avoids the race where a generator-style ``subscribe``
    only registers on its first ``__anext__`` — events published in the
    interim window would have been lost.
    """

    def __init__(self, *, queue_size: int):
        self._queue: asyncio.Queue[WatchEvent[_TRes] | None] = asyncio.Queue(maxsize=queue_size)
        self._overflowed: bool = False
        self._closed: bool = False

    def offer(self, event: WatchEvent[_TRes]) -> bool:
        """Try to enqueue; mark overflowed and return False if the queue is full.

        Non-blocking: bus.publish() never waits on a slow consumer.
        """
        if self._closed or self._overflowed:
            return False
        try:
            self._queue.put_nowait(event)
            return True
        except asyncio.QueueFull:
            self._overflowed = True
            # Wake the iterator with the sentinel so it raises promptly.
            try:
                self._queue.put_nowait(None)
            except asyncio.QueueFull:
                pass
            return False

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        try:
            self._queue.put_nowait(None)
        except asyncio.QueueFull:
            # Consumer will see whatever's already buffered, then EOF on next poll.
            pass

    async def next_event(self) -> WatchEvent[_TRes] | None:
        return await self._queue.get()

    @property
    def overflowed(self) -> bool:
        return self._overflowed


@final
class WatchBus(Generic[_TRes]):
    """Per-kind fan-out bus.

    The store holds one bus per kind and publishes under the per-kind
    lock, so events arrive in resource_version order. Subscribers are
    independent; a slow one is dropped, never blocks the bus.
    """

    DEFAULT_QUEUE_SIZE = 256

    def __init__(self, kind: str, *, queue_size: int = DEFAULT_QUEUE_SIZE):
        self._kind = kind
        self._queue_size = queue_size
        self._subscriptions: set[Subscription[_TRes]] = set()

    @property
    def subscriber_count(self) -> int:
        return len(self._subscriptions)

    def publish(self, event: WatchEvent[_TRes]) -> None:
        """Fan out one event. Synchronous, never awaits.

        Dropped subscriptions (overflowed) are removed eagerly so they
        don't accumulate. The consumer-side iterator surfaces the drop.
        """
        dead: list[Subscription[_TRes]] = []
        for sub in self._subscriptions:
            if not sub.offer(event):
                dead.append(sub)
        for sub in dead:
            self._subscriptions.discard(sub)

    def close(self) -> None:
        """Tell every subscriber to stop. Safe to call multiple times."""
        for sub in list(self._subscriptions):
            sub.close()
        self._subscriptions.clear()

    def unregister(self, sub: "Subscription[_TRes]") -> None:
        """Close ``sub`` and remove it from the bus's subscriber set.

        Both halves are needed for full cleanup: closing alone enqueues
        the None sentinel on the per-subscription queue (so any awaiting
        consumer wakes up) but leaves the token in ``_subscriptions``
        until a subsequent publish discovers ``offer()`` returned False.
        On a quiet bus that may never happen, leaking the token. The
        consumer-side cleanup paths (``Subscription.close`` /
        ``__aexit__``) call this so close-without-iterate is leak-free.
        Idempotent.
        """
        sub.close()
        self._subscriptions.discard(sub)

    def open(self) -> "Subscription[_TRes]":
        """Synchronously register a new subscription.

        Returned token is unsubscribed by ``stream(token)``'s finally
        clause. Splitting open/stream avoids the async-generator race
        where the caller's first apply happens between subscribe() and
        the generator body registering itself.
        """
        sub: Subscription[_TRes] = Subscription(queue_size=self._queue_size)
        self._subscriptions.add(sub)
        return sub

    async def stream(self, sub: "Subscription[_TRes]") -> AsyncIterator[WatchEvent[_TRes]]:
        """Iterate events delivered to ``sub`` until close or overflow."""
        try:
            async for event in self._iter(sub):
                yield event
        finally:
            sub.close()
            self._subscriptions.discard(sub)

    async def _iter(self, sub: Subscription[_TRes]) -> AsyncIterator[WatchEvent[_TRes]]:
        while True:
            # Check overflow at the top of every loop, not just after a
            # None sentinel — under sustained pressure the queue can stay
            # full so the bus's None push fails too. Without this check
            # the consumer blocks in next_event forever even though the
            # subscription is already broken.
            if sub.overflowed:
                raise WatchOverflow(self._kind)
            event = await sub.next_event()
            if event is None:
                if sub.overflowed:
                    raise WatchOverflow(self._kind)
                return
            yield event
