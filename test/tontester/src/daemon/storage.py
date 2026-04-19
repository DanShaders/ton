from abc import ABC, abstractmethod
from dataclasses import dataclass
from datetime import datetime
from enum import StrEnum

from pydantic import BaseModel


class NodeTarget(BaseModel):
    name: str
    address: str  # host:port reachable from the prometheus container


class TestMetadata(BaseModel):
    description: str = ""
    git_branch: str = ""
    git_commit_id: str = ""
    nodes: list[NodeTarget] = []


class RunStatus(StrEnum):
    """Lifecycle of a per-run Prometheus instance.

    - ``live``:     a WebSocket is currently holding this run (scraping).
    - ``dormant``:  no WebSocket; data is on disk, queryable via lazy-boot.

    A daemon crash can leave a ``live`` row behind; startup recovery flips
    every such row to ``dormant`` since by definition no WS is connected.
    """

    LIVE = "live"
    DORMANT = "dormant"


@dataclass
class RunMetadata:
    run_id: str
    start_time: datetime
    end_time: datetime | None
    status: RunStatus
    metadata: TestMetadata
    host_port: int  # host port allocated to this run's Prometheus container


class StorageBackend(ABC):
    @abstractmethod
    async def register_run(self, run_id: str, metadata: TestMetadata, host_port: int) -> None: ...

    @abstractmethod
    async def set_run_status(
        self,
        run_id: str,
        status: RunStatus,
        *,
        if_port: int | None = None,
    ) -> None:
        """Update status, optionally guarded by a ``host_port`` equality.

        Going DORMANT always stamps ``end_time = now`` — "last time the run
        went dormant" wins over any earlier stamp, so ``/api/runs`` shows
        the most recent completion.

        When ``if_port`` is set, the update only takes effect if the stored
        row's ``host_port`` still matches. This lets a failed-registration
        rollback flip to DORMANT *only* if no concurrent register has since
        overwritten the row — a compare-and-swap on port.
        """
        ...

    @abstractmethod
    async def list_runs(self, limit: int = 50) -> list[RunMetadata]: ...

    @abstractmethod
    async def get_run_metadata(self, run_id: str) -> RunMetadata | None: ...

    @abstractmethod
    async def list_runs_with_status(self, status: RunStatus) -> list[RunMetadata]: ...

    @abstractmethod
    async def list_allocated_ports(self) -> set[int]: ...
