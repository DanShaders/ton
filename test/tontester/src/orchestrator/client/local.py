"""In-process client — direct method calls on the store.

For laptop-local runs and tests. Skips serialization entirely; the
caller's pydantic instance is what lands in the store. The store still
deep-copies on the way in/out so callers can't mutate stored rows.
"""

from collections.abc import AsyncIterator
from typing import TypeVar, final, override

from pydantic import BaseModel

from ..resources import LabelSelector, ResourceLike
from ..store import InMemoryStore, WatchEvent
from .client import Client

_TRes = TypeVar("_TRes", bound=ResourceLike[BaseModel, BaseModel])


@final
class LocalClient(Client):
    """In-process Client backed by an :class:`InMemoryStore`.

    Constructed with the manager's store reference; the manager itself
    owns the lifecycle. The client doesn't open or close the store.
    """

    def __init__(self, store: InMemoryStore):
        self._store: InMemoryStore = store

    @override
    async def apply(self, desired: _TRes, *, expected_version: int | None = None) -> _TRes:
        return await self._store.apply(desired, expected_version=expected_version)

    @override
    async def get(
        self,
        resource_type: type[_TRes],
        *,
        namespace: str | None,
        name: str,
    ) -> _TRes:
        return self._store.get(resource_type, namespace=namespace, name=name)

    @override
    async def list(
        self,
        resource_type: type[_TRes],
        *,
        namespace: str | None = None,
        selector: LabelSelector | None = None,
    ) -> list[_TRes]:
        return self._store.list(resource_type, namespace=namespace, selector=selector)

    @override
    async def delete(
        self,
        resource_type: type[_TRes],
        *,
        namespace: str | None,
        name: str,
    ) -> None:
        await self._store.delete(resource_type, namespace=namespace, name=name)

    @override
    def watch(
        self,
        resource_type: type[_TRes],
    ) -> AsyncIterator[WatchEvent[_TRes]]:
        return self._store.subscribe(resource_type)
