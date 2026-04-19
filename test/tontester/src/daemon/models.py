"""Shared data types and exceptions.

Pure value types with no runtime-resource handles — safe to construct,
copy, compare, and serialize. Runtime state that owns resources
(``Dormant`` / ``Live``, holding :class:`contextlib.AsyncExitStack`)
lives in :mod:`.run_actor` where its lifecycle is enforced.
"""

from dataclasses import dataclass
from datetime import datetime
from enum import StrEnum
from pathlib import Path
from typing import Literal

from pydantic import BaseModel

# --- test-harness schema (pydantic: crosses the IPC boundary) ------------


class NodeTarget(BaseModel):
    name: str
    address: str  # host:port reachable from the prometheus container


class TestMetadata(BaseModel):
    description: str = ""
    git_branch: str = ""
    git_commit_id: str = ""
    nodes: list[NodeTarget] = []


# --- persisted run state -------------------------------------------------


class RunStatus(StrEnum):
    """Lifecycle of a per-run Prometheus instance.

    - ``live``:     a WebSocket is currently holding this run (scraping).
    - ``dormant``:  no WebSocket; data is on disk, queryable via lazy-boot.

    A daemon crash can leave a ``live`` row behind; startup recovery flips
    every such row to ``dormant`` since by definition no WS is connected.
    """

    LIVE = "live"
    DORMANT = "dormant"


@dataclass(frozen=True)
class RunMetadata:
    run_id: str
    start_time: datetime
    end_time: datetime | None
    status: RunStatus
    metadata: TestMetadata
    host_port: int  # host port allocated to this run's Prometheus container


# --- actor-state view ----------------------------------------------------


type RunOwner = Literal["ws", "archive"]
type RunLifecycle = Literal["dormant", "live"]


@dataclass(frozen=True)
class RunStateSnapshot:
    """Read-only view of an actor's in-memory state (tests + diagnostics).

    The live ``Dormant | Live`` sum type in :mod:`.run_actor` holds
    runtime resources; this is its serializable projection.
    """

    run_id: str
    status: RunLifecycle
    owner: RunOwner | None
    host_port: int | None
    pins: int
    last_access: float


# --- cross-module exceptions ---------------------------------------------


class RunAlreadyActive(RuntimeError):
    def __init__(self, run_id: str):
        super().__init__(f"Run {run_id} is already active")
        self.run_id: str = run_id


class RunStartFailed(RuntimeError):
    def __init__(self, run_id: str):
        super().__init__(f"Failed to start Prometheus for run {run_id}")
        self.run_id: str = run_id


class NoFreePorts(RuntimeError):
    def __init__(self, range_: tuple[int, int]):
        super().__init__(f"No free ports in {range_}")
        self.range: tuple[int, int] = range_


class DaemonStopped(RuntimeError):
    def __init__(self) -> None:
        super().__init__("daemon is stopping; new runs are refused")


# --- container service description ---------------------------------------


class VolumeMount(BaseModel):
    host_path: Path
    container_path: str


class HttpReadyProbe(BaseModel):
    path: str
    container_port: int
    timeout_seconds: float = 30.0
    interval_seconds: float = 0.25


class Service(BaseModel):
    name: str
    image: str
    command: list[str] = []
    env: dict[str, str] = {}
    ports: dict[int, int] = {}  # host_port -> container_port (inbound)
    # Host ports the container is allowed to reach via its own loopback.
    # Empty list means the container is fully isolated from the host — no
    # egress TCP whatsoever. pasta forwards ``127.0.0.1:<port>`` inside the
    # container's namespace to ``127.0.0.1:<port>`` on the host.
    host_egress_ports: list[int] = []
    volumes: list[VolumeMount] = []
    ready_probe: HttpReadyProbe | None = None
    stop_timeout_seconds: int = 60  # give prometheus room to flush the head block
