"""In-memory ``Runtime`` for tests.

Doesn't fork anything — applies become entries in a dict, status is
whatever the test wires up. Lets reconciler tests run in milliseconds
without subprocess flakiness.

Per the codebase convention (``daemon.testing``), the fake lives in
the production package so test files can import it without sys.path
hacks; pyright treats it as part of the typed surface.
"""

import contextlib
from collections.abc import AsyncGenerator
from datetime import datetime, timezone
from typing import Self, final, override

from ..broadcast import BroadcastQueue
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
        self._events: BroadcastQueue[RuntimeEvent] = BroadcastQueue("fake-runtime", queue_size=256)
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
        self._events.publish(
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
        self._events.publish(
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
    def watch(self):
        return self._events.subscribe()

    # ---- test driver --------------------------------------------------

    def set_status(self, *, namespace: str | None, name: str, status: WorkloadStatus) -> None:
        self._statuses[(namespace, name)] = status

    def emit(self, event: RuntimeEvent) -> None:
        self._events.publish(event)

    @override
    @contextlib.asynccontextmanager
    async def running(self) -> AsyncGenerator[Self]:
        try:
            yield self
        finally:
            self._events.close()
