"""In-process pub/sub for resource changes.

One :class:`WatchBus` per kind. Internally a thin wrapper over the
package-wide :class:`~orchestrator.broadcast.BroadcastQueue` — same
overflow rule (slow subscribers get :exc:`WatchOverflow`, never
block the publisher), same synchronous-registration contract, just
typed for resource events and re-raising the package's typed
overflow exception.

Publish ordering is total per kind: every event has a strictly
increasing ``resource_version`` set by :class:`InMemoryStore` under
its per-kind lock. Subscribers receive events in publish order;
cross-kind ordering is undefined (and that's fine — controllers
reconcile per resource, not per cross-resource transaction).
"""

from collections.abc import AsyncGenerator, AsyncIterator
from contextlib import asynccontextmanager
from dataclasses import dataclass
from enum import StrEnum
from typing import final

from pydantic import BaseModel

from ..broadcast import BroadcastOverflow, BroadcastQueue
from ..errors import WatchOverflow
from ..resources import ResourceLike


class WatchEventType(StrEnum):
    ADDED = "added"
    MODIFIED = "modified"
    DELETED = "deleted"


@dataclass(frozen=True)
class WatchEvent[T: ResourceLike[BaseModel, BaseModel]]:
    """One delta on the bus.

    ``resource_version`` is the post-write version of the object. For
    ``DELETED`` events the object carries the final pre-delete state
    (k8s convention) so consumers can read its labels for cleanup.
    """

    type: WatchEventType
    resource: T
    resource_version: int


@final
class WatchBus[T: ResourceLike[BaseModel, BaseModel]]:
    """Per-kind fan-out bus — wraps :class:`BroadcastQueue`.

    The store holds one bus per kind and publishes under the per-kind
    lock, so events arrive in resource_version order. Subscribers are
    independent; a slow one is dropped, never blocks the bus.
    """

    DEFAULT_QUEUE_SIZE = 256

    def __init__(self, kind: str, *, queue_size: int = DEFAULT_QUEUE_SIZE):
        self._kind: str = kind
        self._inner: BroadcastQueue[WatchEvent[T]] = BroadcastQueue(kind, queue_size=queue_size)

    @property
    def subscriber_count(self) -> int:
        return self._inner.subscriber_count

    def publish(self, event: WatchEvent[T]) -> None:
        self._inner.publish(event)

    def close(self) -> None:
        self._inner.close()

    @asynccontextmanager
    async def subscribe(self) -> AsyncGenerator[AsyncIterator[WatchEvent[T]]]:
        """Register synchronously; iterate inside the ``async with``.

        Re-raises :exc:`BroadcastOverflow` as the package-level
        :exc:`WatchOverflow` so callers see one consistent error
        type for "you fell behind, re-list."
        """
        async with self._inner.subscribe() as events:
            yield _wrap_overflow(events, self._kind)


async def _wrap_overflow[T: ResourceLike[BaseModel, BaseModel]](
    events: AsyncIterator[WatchEvent[T]],
    kind: str,
) -> AsyncIterator[WatchEvent[T]]:
    try:
        async for event in events:
            yield event
    except BroadcastOverflow as e:
        raise WatchOverflow(kind) from e
