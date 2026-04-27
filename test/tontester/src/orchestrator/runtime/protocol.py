"""Runtime backend interface.

A :class:`Runtime` is the single seam between the orchestrator's
declarative resources and the host's process model. The agent calls a
Runtime; nothing else does. Three intended impls:

- ``SubprocessRuntime`` — ``asyncio.subprocess``, no namespace
  isolation, no cgroup-per-workload by default. Usable on Mac/Windows;
  on Linux it's the simplest path and the one this prototype ships.
- ``RuncRuntime`` (future) — synth an OCI bundle per workload, exec
  ``crun``. Full user+net+mount+pid+uts+ipc namespaces, cgroup-per-
  workload, ~13ms cold start.
- ``PodmanRuntime`` (bridge) — wraps today's daemon ``compose.py`` so
  we can land the orchestrator without first migrating the daemon's
  podman dependency.

The Protocol is intentionally narrow: ``apply`` (idempotent), ``delete``,
``get``, ``list``, ``watch``. Everything richer (signal, exec, logs)
belongs on a separate Protocol so backends can opt in.
"""

from collections.abc import AsyncIterator
from dataclasses import dataclass
from datetime import datetime
from enum import StrEnum
from typing import Protocol

from ..resources import Workload, WorkloadStatus


class RuntimeEventType(StrEnum):
    STARTED = "started"
    EXITED = "exited"
    STATUS_CHANGED = "status_changed"


@dataclass(frozen=True)
class RuntimeEvent:
    """Push notification from the runtime when a workload changes state.

    The agent reflects these into ``WorkloadStatus`` writes against the
    store. The runtime never writes the store directly — keeps the
    "store is the only writer" invariant clean.
    """

    type: RuntimeEventType
    workload_namespace: str | None
    workload_name: str
    status: WorkloadStatus
    timestamp: datetime


class Runtime(Protocol):
    """Backend that owns processes for one host."""

    async def apply(self, workload: Workload) -> WorkloadStatus:
        """Idempotent: ensure the workload is running per its current spec.

        - First apply: spawn.
        - Apply on a running workload with same spec: no-op (same uid +
          same generation -> same processes).
        - Apply with new spec: respawn (Recreate strategy; the agent's
          reconciler computes the diff).

        Returns the post-apply observed status.
        """
        ...

    async def delete(self, *, namespace: str | None, name: str) -> None:
        """Idempotent: ensure the workload is gone.

        Sends SIGTERM, waits up to ``termination_grace_s``, then
        SIGKILL. Cleans up the per-workload cgroup, netns, bundle.
        """
        ...

    async def get(self, *, namespace: str | None, name: str) -> WorkloadStatus | None:
        """Return the runtime's view of a workload's status, or None
        if it doesn't exist."""
        ...

    async def list(self) -> list[WorkloadStatus]:
        """All workloads currently known to this runtime.

        Used by the agent at startup to reconcile observed-vs-desired
        without needing the orchestrator's state to persist.
        """
        ...

    def watch(self) -> AsyncIterator[RuntimeEvent | None]:
        """Push stream of runtime events. Never returns; cancel to stop.

        **Registration sentinel:** the first yielded value MUST be
        ``None``, fired synchronously after the runtime has registered
        the subscriber. Subsequent yields are real :class:`RuntimeEvent`
        instances. The agent uses the sentinel as a "ready" ack so it
        knows to block ``start()`` until the runtime side is actually
        listening — without it, the first ``apply()`` after start could
        race the watch loop's first scheduling and the corresponding
        STARTED event would be lost.
        """
        ...

    async def close(self) -> None:
        """Tear down: stop every workload, drop subscriptions.

        Idempotent. The agent's AsyncExitStack calls this on shutdown.
        Backends that bind only loop-bound resources (FakeRuntime) can
        keep this trivial; backends with cgroup/netns ownership clean
        them up here.
        """
        ...


class ProcessSupervisor(Protocol):
    """Per-workload supervisor handle.

    Backends may expose this via ``get_supervisor(name)`` for advanced
    operations the agent needs (signal, exec). Kept off the main
    Runtime Protocol so simple backends don't have to implement them.
    """

    async def signal(self, signum: int) -> None: ...

    async def exec(self, command: list[str]) -> tuple[int, bytes, bytes]: ...
