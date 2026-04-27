"""Public typed client API.

Same shape as the k8s typed client (``apply``/``get``/``list``/``watch``
/``delete`` per kind). The :class:`Client` is a Protocol so the
in-process and over-the-wire implementations both satisfy it.

For the prototype only :class:`LocalClient` exists, talking directly
to the manager's store. A WebSocket implementation will plug in as
:class:`RemoteClient` (separate file) without changing the Protocol.
"""

from collections.abc import AsyncIterator
from typing import Protocol, TypeVar

from pydantic import BaseModel

from ..resources import LabelSelector, ResourceLike
from ..store import WatchEvent

_TRes = TypeVar("_TRes", bound=ResourceLike[BaseModel, BaseModel])


class Client(Protocol):
    """Typed orchestrator client.

    Every method is async because the over-the-wire implementation
    needs it. The in-process implementation satisfies trivially.
    """

    async def apply(self, desired: _TRes, *, expected_version: int | None = None) -> _TRes: ...

    async def get(
        self,
        resource_type: type[_TRes],
        *,
        namespace: str | None,
        name: str,
    ) -> _TRes: ...

    async def list(
        self,
        resource_type: type[_TRes],
        *,
        namespace: str | None = None,
        selector: LabelSelector | None = None,
    ) -> list[_TRes]: ...

    async def delete(
        self,
        resource_type: type[_TRes],
        *,
        namespace: str | None,
        name: str,
    ) -> None: ...

    def watch(
        self,
        resource_type: type[_TRes],
    ) -> AsyncIterator[WatchEvent[_TRes]]: ...
