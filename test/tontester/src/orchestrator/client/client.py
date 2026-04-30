"""Public typed client API.

Same shape as the k8s typed client (``apply``/``get``/``list``/``watch``
/``delete`` per kind). The :class:`Client` is a Protocol so the
in-process and over-the-wire implementations both satisfy it.

For the prototype only :class:`LocalClient` exists, talking directly
to the manager's store. A WebSocket implementation will plug in as
:class:`RemoteClient` (separate file) without changing the Protocol.
"""

from collections.abc import AsyncIterator
from typing import Protocol

from pydantic import BaseModel

from ..resources import LabelSelector, ResourceLike
from ..store import WatchEvent

_AnyResource = ResourceLike[BaseModel, BaseModel]


class Client(Protocol):
    """Typed orchestrator client.

    Every method is async because the over-the-wire implementation
    needs it. The in-process implementation satisfies trivially.
    """

    async def apply[T: _AnyResource](
        self, desired: T, *, expected_version: int | None = None
    ) -> T: ...

    async def get[T: _AnyResource](
        self,
        resource_type: type[T],
        *,
        namespace: str | None,
        name: str,
    ) -> T: ...

    async def list[T: _AnyResource](
        self,
        resource_type: type[T],
        *,
        namespace: str | None = None,
        selector: LabelSelector | None = None,
    ) -> list[T]: ...

    async def delete[T: _AnyResource](
        self,
        resource_type: type[T],
        *,
        namespace: str | None,
        name: str,
    ) -> None: ...

    def watch[T: _AnyResource](
        self,
        resource_type: type[T],
    ) -> AsyncIterator[WatchEvent[T]]: ...
