"""All cross-module Protocols and ABCs in one place.

Per the coding rules: concrete dependencies that need to be substitutable
for tests (subprocess, HTTP, sqlite) are injected as Protocols; the one
ABC is :class:`StorageBackend`, where ``HookedStorage`` composes/wraps the
real ``SQLiteStorage`` rather than just implementing the interface
independently — which the rule cites as the reason to prefer ABC over
Protocol.

The concrete implementations live next to their domain:

- :class:`.compose.Compose` implements :class:`ComposeLike` (and its
  helper Protocols ``ProcessHandle`` / ``ProcessRunner``).
- :class:`.sqlite_storage.SQLiteStorage` and
  :class:`.testing.HookedStorage` implement :class:`StorageBackend`.
- :class:`.services.GrafanaProvisioning` and
  :class:`.testing.FaultyProvisioning` implement :class:`ProvisioningLike`.
"""

from abc import ABC, abstractmethod
from collections.abc import Awaitable, Callable
from typing import Protocol

from .models import RunMetadata, RunStatus, Service, TestMetadata

# --- storage -------------------------------------------------------------


class StorageBackend(ABC):
    @abstractmethod
    async def register_run(self, run_id: str, metadata: TestMetadata) -> None: ...

    @abstractmethod
    async def set_run_status(self, run_id: str, status: RunStatus) -> None:
        """Update status.

        Going DORMANT stamps ``end_time = now`` only if it was NULL (i.e.
        the first dormant transition after a LIVE period). Later DORMANT
        writes — notably the archive reaper releasing an already-dormant
        run — must not overwrite the authoritative WS-close timestamp.
        """
        ...

    @abstractmethod
    async def list_runs(self, limit: int = 50) -> list[RunMetadata]: ...

    @abstractmethod
    async def get_run_metadata(self, run_id: str) -> RunMetadata | None: ...

    @abstractmethod
    async def list_runs_with_status(self, status: RunStatus) -> list[RunMetadata]: ...


# --- compose / subprocess ------------------------------------------------


class ProcessHandle(Protocol):
    """Subset of ``asyncio.subprocess.Process`` that :class:`.compose.Compose` uses."""

    @property
    def returncode(self) -> int | None: ...

    async def wait(self) -> int | None: ...

    def terminate(self) -> None: ...

    def kill(self) -> None: ...


class ProcessRunner(Protocol):
    """Abstracts subprocess creation so tests can substitute a fake."""

    async def run_capture(self, argv: list[str], *, timeout: float) -> tuple[int, bytes, bytes]:
        """Run ``argv`` to completion, return (returncode, stdout, stderr).

        Must raise ``asyncio.TimeoutError`` if the process does not exit
        within ``timeout``; Compose translates that to a ``PodmanTimeout``.
        """
        ...

    async def run_attached(self, argv: list[str]) -> ProcessHandle:
        """Spawn an attached process (inherits stdout/stderr) and return a
        handle to it. Must not wait for exit."""
        ...


class ComposeLike(Protocol):
    """Minimal surface consumed by :class:`.runs.RunsSupervisor` /
    :class:`.run_actor.RunActor`."""

    async def up(self, service: Service) -> None: ...

    async def down(self, name: str) -> None: ...

    async def list_running(self, prefix: str) -> list[str]: ...


type ReadyProbeFn = Callable[[str, float], Awaitable[bool]]
"""Takes a URL and a timeout, returns True iff the endpoint responded with
a 2xx within the timeout. Tests substitute this to avoid real HTTP."""


# --- provisioning --------------------------------------------------------


class ProvisioningLike(Protocol):
    """Minimal surface of :class:`.services.GrafanaProvisioning` consumed
    by run actors.

    Only the per-run mutation entry points appear here — the one-shot
    ``write_dashboard*`` calls live on the concrete class and are invoked
    by the daemon's startup code, not by actors.
    """

    def write_run_datasource(self, run_id: str) -> None: ...

    def remove_run_datasource(self, run_id: str) -> None: ...

    def list_provisioned_runs(self) -> set[str]: ...

    async def reload_datasources(self) -> None: ...
