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
from contextlib import AbstractAsyncContextManager
from dataclasses import dataclass
from datetime import datetime
from enum import StrEnum
from typing import Protocol

from ..lifecycle import Resource
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


class Runtime(Resource, Protocol):
    """Backend that owns processes for one host.

    Extends :class:`~orchestrator.lifecycle.Resource`: ``running()`` /
    ``shutdown()`` / ``stop_token`` come from there. The Runtime-
    specific surface is the apply / delete / get / list / watch
    operations below.
    """

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

    def watch(self) -> AbstractAsyncContextManager[AsyncIterator[RuntimeEvent]]:
        """Async context manager yielding a stream of runtime events.

        Use as::

            async with runtime.watch() as events:
                async for event in events:
                    ...

        Synchronous registration: by the time ``async with`` enters,
        the subscriber is on the runtime's broadcast set, so any
        :meth:`apply` issued after the ``async with`` enters will
        publish events that this subscription observes. There is no
        "registration sentinel" — registration is structural via the
        context manager.

        On overflow (consumer too slow), the iterator raises
        :exc:`~orchestrator.broadcast.BroadcastOverflow`; the caller
        should re-list and re-subscribe.
        """
        ...

    # ``running()`` / ``shutdown()`` / ``stop_token`` are inherited
    # from :class:`~orchestrator.lifecycle.Resource`. The runtime adds
    # the apply / delete / get / list / watch surface above; the
    # lifecycle shape is identical to every other Resource in the
    # codebase.


class ProcessSupervisor(Protocol):
    """Per-workload supervisor handle.

    Backends may expose this via ``get_supervisor(name)`` for advanced
    operations the agent needs (signal, exec). Kept off the main
    Runtime Protocol so simple backends don't have to implement them.
    """

    async def signal(self, signum: int) -> None: ...

    async def exec(self, command: list[str]) -> tuple[int, bytes, bytes]: ...
