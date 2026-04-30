"""Bounded fan-out broadcast queue.

One implementation of "publish to N independent subscribers, drop the
slow ones." Used by the store's :class:`WatchBus` and by every
:class:`Runtime` backend's event stream. Three correct things in one
place beats three half-correct hand-rolled fan-outs.

**Backpressure rule.** Each subscriber gets a bounded
:class:`asyncio.Queue`. ``publish`` is synchronous and never waits;
if a subscriber's queue is full, the subscriber is dropped from the
broadcast set and its iterator surfaces :exc:`BroadcastOverflow` on
the next iteration step. Compare k8s' "410 Gone": a slow client
cannot stall the publisher, but it gets an explicit error so it
knows to re-list and re-subscribe.

**Subscription is synchronous.** ``async with broadcast.subscribe()
as events`` registers immediately; events published after the
``__aenter__`` returns are observed by ``async for event in events``.
There is no "registration sentinel" needed — the act of being inside
the ``async with`` block *is* the registration.

**Cleanup is structural.** The async-with's ``__aexit__`` removes the
subscription from the broadcast set even if iteration was never
started. There is no ``unregister`` call to forget.
"""

import asyncio
from collections.abc import AsyncGenerator, AsyncIterator
from contextlib import asynccontextmanager
from enum import StrEnum
from typing import final


class BroadcastOverflow(RuntimeError):
    """A subscriber's queue filled before it caught up.

    The subscriber is detached from the broadcast as soon as
    overflow is detected; further events will not reach this
    subscription. The caller should drop the iterator and start a
    fresh subscription if it still wants to follow the stream.
    """

    def __init__(self, name: str):
        super().__init__(f"broadcast overflow on {name}; subscriber dropped")
        self.name: str = name


class BroadcastClosed(RuntimeError):
    """``subscribe()`` was called after ``close()``.

    Subscribing to a closed bus would silently hang the consumer:
    no None sentinel is delivered (close already cleared
    ``_subs``), and ``next()`` blocks forever. Surfacing the misuse
    as a typed exception forces callers to recognize the broken
    invariant rather than diagnose a hang.
    """

    def __init__(self, name: str):
        super().__init__(f"broadcast {name} is closed; cannot subscribe")
        self.name: str = name


@final
class _BroadcastSub[T]:
    """One subscriber's per-instance queue + overflow flag."""

    def __init__(self, *, queue_size: int):
        self._queue: asyncio.Queue[T | None] = asyncio.Queue(maxsize=queue_size)
        self._overflowed: bool = False
        self._closed: bool = False

    @property
    def overflowed(self) -> bool:
        return self._overflowed

    def offer(self, item: T) -> bool:
        """Try to enqueue ``item``. Return False if the queue is full
        (subscriber is now overflowed and should be dropped)."""
        if self._closed or self._overflowed:
            return False
        try:
            self._queue.put_nowait(item)
            return True
        except asyncio.QueueFull:
            self._overflowed = True
            # Wake the iterator with a sentinel so it raises promptly.
            try:
                self._queue.put_nowait(None)
            except asyncio.QueueFull:
                pass
            return False

    def close(self) -> None:
        """Push a None sentinel and mark closed. Idempotent."""
        if self._closed:
            return
        self._closed = True
        try:
            self._queue.put_nowait(None)
        except asyncio.QueueFull:
            pass

    async def next(self) -> T | None:
        return await self._queue.get()


class _State(StrEnum):
    OPEN = "open"
    CLOSED = "closed"


@final
class BroadcastQueue[T]:
    """Per-publisher fan-out bus.

    Construct one per distinct stream. ``publish`` fans out
    synchronously to every active subscriber; ``subscribe`` is an
    async context manager that yields an ``AsyncIterator[T]``.

    State machine: ``OPEN`` → ``CLOSED`` (one-way). All methods
    document and enforce which states they accept. ``subscribe``
    on a CLOSED bus raises :exc:`BroadcastClosed` rather than
    silently handing back a hanging iterator.
    """

    def __init__(self, name: str, *, queue_size: int = 256):
        self._name: str = name
        self._queue_size: int = queue_size
        self._subs: set[_BroadcastSub[T]] = set()
        self._state: _State = _State.OPEN

    @property
    def name(self) -> str:
        return self._name

    @property
    def subscriber_count(self) -> int:
        return len(self._subs)

    @property
    def is_closed(self) -> bool:
        return self._state is _State.CLOSED

    def publish(self, item: T) -> None:
        """Fan out to every subscriber. Synchronous, never awaits.

        Allowed in any state — publishing on a closed bus is a no-op.
        Permitting publish-after-close is intentional: in tear-down,
        a sync stack callback may publish into a bus the runtime
        already closed, and we'd rather drop the event than crash
        the unwind.

        Subscribers whose queues are full are dropped here — their
        iterators will raise :exc:`BroadcastOverflow` on the next
        poll.
        """
        if self._state is _State.CLOSED:
            return
        dead: list[_BroadcastSub[T]] = []
        for sub in self._subs:
            if not sub.offer(item):
                dead.append(sub)
        for sub in dead:
            self._subs.discard(sub)

    def close(self) -> None:
        """Tell every subscriber to stop. Idempotent.

        Transitions ``OPEN → CLOSED``. After this returns, every
        active iterator gets a clean None-sentinel exit, and any
        subsequent :meth:`subscribe` raises :exc:`BroadcastClosed`.
        """
        if self._state is _State.CLOSED:
            return
        self._state = _State.CLOSED
        for sub in list(self._subs):
            sub.close()
        self._subs.clear()

    @asynccontextmanager
    async def subscribe(self) -> AsyncGenerator[AsyncIterator[T]]:
        """Register a subscriber for the lifetime of the ``async with``.

        Synchronous registration: events published after this returns
        are observed. The yielded iterator stops cleanly on
        :meth:`close` and raises :exc:`BroadcastOverflow` if this
        subscriber falls behind.

        Raises :exc:`BroadcastClosed` if the bus is already closed —
        otherwise the new subscriber would never receive a sentinel
        (close already happened) and ``next()`` would block forever.
        """
        if self._state is _State.CLOSED:
            raise BroadcastClosed(self._name)
        sub: _BroadcastSub[T] = _BroadcastSub(queue_size=self._queue_size)
        self._subs.add(sub)
        try:
            yield self._iter(sub)
        finally:
            self._subs.discard(sub)
            sub.close()

    async def _iter(self, sub: _BroadcastSub[T]) -> AsyncIterator[T]:
        while True:
            # Overflow at the top of the loop: under sustained pressure
            # the queue stays full so the bus's None push fails too.
            # Without this check the consumer blocks in next() forever
            # even though the subscription is already broken.
            if sub.overflowed:
                raise BroadcastOverflow(self._name)
            item = await sub.next()
            if item is None:
                if sub.overflowed:
                    raise BroadcastOverflow(self._name)
                return
            yield item
