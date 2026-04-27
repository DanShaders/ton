"""In-memory ``Runtime`` for tests.

Doesn't fork anything — applies become entries in a dict, status is
whatever the test wires up. Lets reconciler tests run in milliseconds
without subprocess flakiness.

Per the codebase convention (``daemon.testing``), the fake lives in
the production package so test files can import it without sys.path
hacks; pyright treats it as part of the typed surface.
"""

import asyncio
from collections.abc import AsyncIterator
from datetime import datetime, timezone
from typing import final, override

from ..resources import (
    ContainerStatus,
    Workload,
    WorkloadStatus,
)
from .protocol import Runtime, RuntimeEvent, RuntimeEventType


def _now() -> datetime:
    return datetime.now(timezone.utc)


@final
class FakeRuntime(Runtime):
    """Records applies/deletes; lets the test drive observed status.

    Default behaviour: ``apply`` reports ``phase=Running`` immediately;
    ``delete`` reports ``EXITED`` and removes the entry. Tests that
    need a different shape (e.g. apply that fails readiness) set
    ``apply_failure`` or call :meth:`set_status` to override.
    """

    def __init__(self, *, apply_failure: Exception | None = None):
        self._apply_failure: Exception | None = apply_failure
        self._workloads: dict[tuple[str | None, str], Workload] = {}
        self._statuses: dict[tuple[str | None, str], WorkloadStatus] = {}
        self._subs: set[asyncio.Queue[RuntimeEvent | None]] = set()
        self.applies: list[Workload] = []
        self.deletes: list[tuple[str | None, str]] = []

    @override
    async def apply(self, workload: Workload) -> WorkloadStatus:
        self.applies.append(workload)
        if self._apply_failure is not None:
            raise self._apply_failure
        key = (workload.metadata.namespace, workload.metadata.name)
        self._workloads[key] = workload
        status = self._statuses.setdefault(
            key,
            WorkloadStatus(
                phase="Running",
                host="fake",
                network_address="127.0.0.1",
                container_statuses=[
                    ContainerStatus(
                        name=workload.spec.containers[0].name,
                        state="Running",
                        pid=1,
                        started_at=_now(),
                        finished_at=None,
                        exit_code=None,
                        restart_count=0,
                    )
                ],
                conditions=[],
            ),
        )
        await self._publish(
            RuntimeEvent(
                type=RuntimeEventType.STARTED,
                workload_namespace=workload.metadata.namespace,
                workload_name=workload.metadata.name,
                status=status,
                timestamp=_now(),
            )
        )
        return status

    @override
    async def delete(self, *, namespace: str | None, name: str) -> None:
        self.deletes.append((namespace, name))
        key = (namespace, name)
        wl = self._workloads.pop(key, None)
        status = self._statuses.pop(key, None)
        if wl is None:
            return
        await self._publish(
            RuntimeEvent(
                type=RuntimeEventType.EXITED,
                workload_namespace=namespace,
                workload_name=name,
                status=status
                or WorkloadStatus(
                    phase="Succeeded",
                    host="fake",
                    network_address=None,
                    container_statuses=[],
                    conditions=[],
                ),
                timestamp=_now(),
            )
        )

    @override
    async def get(self, *, namespace: str | None, name: str) -> WorkloadStatus | None:
        return self._statuses.get((namespace, name))

    @override
    async def list(self) -> list[WorkloadStatus]:
        return list(self._statuses.values())

    @override
    def watch(self) -> AsyncIterator[RuntimeEvent | None]:
        return self._subscribe()

    async def _subscribe(self) -> AsyncIterator[RuntimeEvent | None]:
        queue: asyncio.Queue[RuntimeEvent | None] = asyncio.Queue(maxsize=256)
        self._subs.add(queue)
        # Registration ack — see Runtime.watch contract.
        yield None
        try:
            while True:
                event = await queue.get()
                if event is None:
                    return
                yield event
        finally:
            self._subs.discard(queue)

    # ---- test driver --------------------------------------------------

    def set_status(self, *, namespace: str | None, name: str, status: WorkloadStatus) -> None:
        self._statuses[(namespace, name)] = status

    async def emit(self, event: RuntimeEvent) -> None:
        await self._publish(event)

    @override
    async def close(self) -> None:
        for sub in list(self._subs):
            try:
                sub.put_nowait(None)
            except asyncio.QueueFull:
                pass
        self._subs.clear()

    async def _publish(self, event: RuntimeEvent) -> None:
        dead: list[asyncio.Queue[RuntimeEvent | None]] = []
        for sub in self._subs:
            try:
                sub.put_nowait(event)
            except asyncio.QueueFull:
                dead.append(sub)
        for sub in dead:
            self._subs.discard(sub)
