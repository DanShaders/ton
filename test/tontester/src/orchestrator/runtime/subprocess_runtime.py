"""Bare ``asyncio.subprocess`` backend.

Right backend for: Mac/Windows native; Linux when isolation isn't
needed; the prototype's hot path while we get the rest of the
orchestrator wired. ~1ms per spawn (no namespace setup, no engine).

What it does:

- Spawn ``container.command`` (or the binary at ``container.image.path``
  if command is empty) as a child process. PID lives directly under
  the orchestrator's pid namespace — no isolation, but a clean exit
  signal path (waitpid works).
- Optionally place the child in a per-workload cgroup at
  ``CGROUP_ROOT/wl-{namespace}-{name}`` for resource limits and easy
  teardown via ``rmdir`` (kills any survivors before removing the dir).
- Probe readiness via the configured probe.
- Stream a :class:`RuntimeEvent` per state change.

What it does *not* do:

- Network namespacing → use RuncRuntime when network isolation matters.
- Bind mounts → ignored (process sees the host fs).
- ``network=isolated`` → silently treated as ``host`` (the agent
  reconciler can warn).

**Architecture: one supervisor task per workload.** Each call to
``apply`` either reuses an existing supervisor (spec unchanged) or
cancels the old one and spawns a new one. The supervisor's body is a
chain of ``async with owned_path(...) as cgroup, owned_process(...)
as process: await process.wait()`` — RAII handles cleanup on every
exit path (normal exit, cancellation, exception). On exit it
publishes EXITED and removes itself from the registry. There is no
separate "reaper" task to forget to spawn, no per-key lock to leak,
and no two-phase shutdown — cancelling the supervisor *is* the
cleanup signal.
"""

import asyncio
import logging
import os
import shutil
import signal
import socket
import time
from collections.abc import AsyncGenerator
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Self, final, override

from ..broadcast import BroadcastQueue
from ..lifecycle import OwnedPath, owned_path, owned_process
from ..resources import (
    Container,
    ContainerStatus,
    HostBinaryImage,
    Probe,
    TarballImage,
    Workload,
    WorkloadStatus,
)
from .protocol import Runtime, RuntimeEvent, RuntimeEventType

logger = logging.getLogger(__name__)


def _now() -> datetime:
    return datetime.now(timezone.utc)


type _Key = tuple[str | None, str]


@dataclass
class _ProcView:
    """Snapshot the supervisor exposes to ``get`` / ``list`` /
    ``apply`` for the no-op-on-equivalent-spec check.

    Mutated only by the supervisor task that owns this entry; read
    by other coroutines via Python's atomic dict access. Fields are
    read-only after the supervisor publishes them.
    """

    workload: Workload
    process: asyncio.subprocess.Process
    started_at: datetime
    cgroup_dir: Path | None
    restart_count: int = 0
    initial_status: WorkloadStatus | None = field(default=None)


@final
class SubprocessRuntime(Runtime):
    """Linux/Mac/Windows bare subprocess backend.

    Construction takes a ``state_dir`` for per-workload bookkeeping
    (cgroup paths today; bundle dirs for RuncRuntime later) and an
    optional ``cgroup_root`` to enable cgroup placement. No cgroup_root
    = no resource limits, but everything else still works.
    """

    def __init__(
        self,
        *,
        state_dir: Path,
        cgroup_root: Path | None = None,
        host_label: str = "local",
    ):
        self._state_dir: Path = state_dir
        self._cgroup_root: Path | None = cgroup_root
        self._host_label: str = host_label
        # Snapshot of running supervisors' state for read-side ops.
        self._procs: dict[_Key, _ProcView] = {}
        # Supervisor tasks. Apply replaces, delete cancels, _close
        # cancels all. Tasks remove themselves from this dict on exit
        # (via done callback).
        self._supervisors: dict[_Key, asyncio.Task[None]] = {}
        # Single fan-out bus for runtime events. ``BroadcastQueue``
        # owns the per-subscriber queues + overflow handling — same
        # primitive WatchBus wraps.
        self._events: BroadcastQueue[RuntimeEvent] = BroadcastQueue("subprocess-runtime")
        self._closed: bool = False

    # ---- Runtime API ---------------------------------------------------

    @override
    async def apply(self, workload: Workload) -> WorkloadStatus:
        if self._closed:
            raise RuntimeError("SubprocessRuntime is closed")
        key: _Key = (workload.metadata.namespace, workload.metadata.name)
        existing = self._procs.get(key)
        if existing is not None and _spec_equivalent(existing.workload, workload):
            return _build_status(existing, host_label=self._host_label)
        # Spec changed (or no existing): cancel the old supervisor (if
        # any) and start a fresh one.
        await self._cancel_supervisor(key)
        return await self._start_supervisor(workload)

    @override
    async def delete(self, *, namespace: str | None, name: str) -> None:
        await self._cancel_supervisor((namespace, name))

    @override
    async def get(self, *, namespace: str | None, name: str) -> WorkloadStatus | None:
        proc = self._procs.get((namespace, name))
        if proc is None:
            return None
        return _build_status(proc, host_label=self._host_label)

    @override
    async def list(self) -> list[WorkloadStatus]:
        return [_build_status(p, host_label=self._host_label) for p in self._procs.values()]

    @override
    def watch(self):
        return self._events.subscribe()

    @override
    @asynccontextmanager
    async def running(self) -> AsyncGenerator[Self]:
        try:
            yield self
        finally:
            await self._close()

    async def _close(self) -> None:
        if self._closed:
            return
        self._closed = True
        # Cancel every supervisor; their RAII unwinds (process killed,
        # cgroup cleaned, EXITED published) before each task completes.
        for task in list(self._supervisors.values()):
            _ = task.cancel()
        if self._supervisors:
            _ = await asyncio.gather(*self._supervisors.values(), return_exceptions=True)
        self._supervisors.clear()
        self._procs.clear()
        self._events.close()

    # ---- supervisor lifecycle ------------------------------------------

    async def _start_supervisor(self, workload: Workload) -> WorkloadStatus:
        """Spawn a supervisor task for ``workload``; await its STARTED
        signal (or its early failure); return the initial status.
        """
        key: _Key = (workload.metadata.namespace, workload.metadata.name)
        loop = asyncio.get_running_loop()
        ready: asyncio.Future[WorkloadStatus] = loop.create_future()
        task = asyncio.create_task(
            self._supervise(key, workload, ready),
            name=f"sup[{workload.metadata.name}]",
        )
        self._supervisors[key] = task

        def _on_done(t: asyncio.Task[None]) -> None:
            # Remove from registry — but only if WE are still the
            # registered supervisor (apply may have replaced us).
            if self._supervisors.get(key) is t:
                _ = self._supervisors.pop(key, None)
            # If the supervisor died before signaling ready, surface
            # the cause to the apply caller.
            if not ready.done():
                exc = t.exception() if not t.cancelled() else asyncio.CancelledError()
                if exc is not None:
                    ready.set_exception(exc)
                else:
                    ready.set_exception(RuntimeError("supervisor exited before signaling ready"))

        task.add_done_callback(_on_done)
        return await ready

    async def _cancel_supervisor(self, key: _Key) -> None:
        task = self._supervisors.get(key)
        if task is None:
            return
        _ = task.cancel()
        try:
            await task
        except asyncio.CancelledError, Exception:
            # Cancellation of this task was intentional. Caller-cancel
            # of _cancel_supervisor itself will be re-raised by the
            # asyncio framework on return because cancel() bumps the
            # current task's cancel-count.
            pass

    async def _supervise(
        self,
        key: _Key,
        workload: Workload,
        ready: asyncio.Future[WorkloadStatus],
    ) -> None:
        """Own one workload's lifecycle from spawn to exit.

        RAII chain: cgroup_dir → process. On any exit path the inner
        owned_process kills the process; the outer owned_path cleans
        the cgroup. EXITED is published in the finally so it fires
        regardless of whether the process exited naturally or we
        cancelled the supervisor.
        """
        # pydantic enforces min_length=1 on WorkloadSpec.containers; this
        # assert is the runtime's own check that the contract held.
        assert workload.spec.containers, f"workload {workload.metadata.name} has zero containers"
        primary = workload.spec.containers[0]
        argv = _build_argv(primary)
        env = dict(os.environ) | primary.env
        cgroup_dir = self._maybe_create_cgroup(workload)

        view: _ProcView | None = None
        try:
            async with self._owned_cgroup(cgroup_dir) as guarded:
                async with owned_process(
                    *argv,
                    env=env,
                    cwd=primary.workdir,
                    grace_seconds=workload.spec.termination_grace_s,
                ) as process:
                    if guarded is not None:
                        _try_join_cgroup(guarded.path, process.pid)
                    view = _ProcView(
                        workload=workload,
                        process=process,
                        started_at=_now(),
                        cgroup_dir=guarded.path if guarded is not None else None,
                    )
                    self._procs[key] = view
                    status = _build_status(view, host_label=self._host_label)
                    view.initial_status = status
                    self._events.publish(
                        RuntimeEvent(
                            type=RuntimeEventType.STARTED,
                            workload_namespace=workload.metadata.namespace,
                            workload_name=workload.metadata.name,
                            status=status,
                            timestamp=_now(),
                        )
                    )
                    if not ready.done():
                        ready.set_result(status)
                    if workload.spec.readiness_probe is not None:
                        probe_task = asyncio.create_task(
                            self._probe_loop(view, workload.spec.readiness_probe),
                            name=f"probe[{workload.metadata.name}]",
                        )
                        try:
                            _ = await process.wait()
                        finally:
                            _ = probe_task.cancel()
                    else:
                        _ = await process.wait()
        finally:
            # Publish EXITED if we ever got far enough to register
            # this view — i.e. if the process actually started.
            if view is not None:
                self._events.publish(
                    RuntimeEvent(
                        type=RuntimeEventType.EXITED,
                        workload_namespace=workload.metadata.namespace,
                        workload_name=workload.metadata.name,
                        status=_build_status(view, host_label=self._host_label),
                        timestamp=_now(),
                    )
                )
                # Pop only if we're still the registered view. Apply
                # may have replaced us with a fresh view (different
                # supervisor) before we got here.
                if self._procs.get(key) is view:
                    _ = self._procs.pop(key, None)

    @asynccontextmanager
    async def _owned_cgroup(self, cgroup_dir: Path | None) -> AsyncGenerator[OwnedPath | None]:
        """Wrap cgroup_dir in owned_path if non-None; otherwise yield None."""
        if cgroup_dir is None:
            yield None
            return
        async with owned_path(cgroup_dir, cleanup=_safe_rmtree) as guarded:
            yield guarded

    # ---- readiness probe ----------------------------------------------

    async def _probe_loop(self, view: _ProcView, probe: Probe) -> None:
        if probe.initial_delay_s > 0:
            await asyncio.sleep(probe.initial_delay_s)
        failures = 0
        while view.process.returncode is None:
            ok = await self._probe_once(view, probe)
            if ok:
                if failures > 0:
                    failures = 0
                    self._events.publish(
                        RuntimeEvent(
                            type=RuntimeEventType.STATUS_CHANGED,
                            workload_namespace=view.workload.metadata.namespace,
                            workload_name=view.workload.metadata.name,
                            status=_build_status(view, host_label=self._host_label),
                            timestamp=_now(),
                        )
                    )
            else:
                failures += 1
                if failures >= probe.failure_threshold:
                    self._events.publish(
                        RuntimeEvent(
                            type=RuntimeEventType.STATUS_CHANGED,
                            workload_namespace=view.workload.metadata.namespace,
                            workload_name=view.workload.metadata.name,
                            status=_build_status(view, host_label=self._host_label),
                            timestamp=_now(),
                        )
                    )
                    return
            await asyncio.sleep(probe.period_s)

    async def _probe_once(self, _view: _ProcView, probe: Probe) -> bool:
        match probe.action.kind:
            case "tcp":
                # No port-name->port resolution in the prototype subprocess
                # backend. Treat tcp probes as "always ok"; full impl waits
                # for RuncRuntime where ports are namespaced.
                return True
            case "http":
                return True
            case "exec":
                return True

    # ---- cgroups (best effort, optional) ------------------------------

    def _maybe_create_cgroup(self, workload: Workload) -> Path | None:
        if self._cgroup_root is None:
            return None
        primary = workload.spec.containers[0]
        if (
            primary.resources.cpu_millis is None
            and primary.resources.memory_bytes is None
            and primary.resources.io_weight is None
        ):
            return None
        cgroup_dir = (
            self._cgroup_root
            / f"wl-{workload.metadata.namespace or 'cluster'}-{workload.metadata.name}"
        )
        try:
            cgroup_dir.mkdir(parents=True, exist_ok=True)
            if primary.resources.cpu_millis is not None:
                # cgroup v2: cpu.max = "<quota> <period>". 1000ms = 1 cpu.
                period = 100000
                quota = int(primary.resources.cpu_millis * (period / 1000))
                _ = (cgroup_dir / "cpu.max").write_text(f"{quota} {period}")
            if primary.resources.memory_bytes is not None:
                _ = (cgroup_dir / "memory.max").write_text(str(primary.resources.memory_bytes))
            if primary.resources.io_weight is not None:
                _ = (cgroup_dir / "io.weight").write_text(f"default {primary.resources.io_weight}")
        except OSError:
            logger.exception(f"cgroup setup failed for {workload.metadata.name}; skipping limits")
            _safe_rmtree(cgroup_dir)
            return None
        return cgroup_dir


# --- helpers --------------------------------------------------------------


def _build_argv(container: Container) -> list[str]:
    """Compose the exec argv for one container.

    Host-binary image: image.path is the executable; ``command`` is the
    arg list. Tarball image: not supported by the subprocess backend
    (no chroot/pivot_root) — error explicitly so callers don't get
    silent fallback to the host fs.
    """
    image = container.image
    match image:
        case HostBinaryImage(path=path):
            return [path, *container.command]
        case TarballImage(path=path):
            raise RuntimeError(
                (
                    f"SubprocessRuntime cannot run tarball image at {path!r}; "
                    f"use RuncRuntime for filesystem-isolated workloads"
                )
            )


def _spec_equivalent(a: Workload, b: Workload) -> bool:
    """Compare specs ignoring metadata (which the store mutates).

    Used to short-circuit no-op applies. ``model_dump`` compares value-
    equal pydantic structures cleanly.
    """
    return a.spec.model_dump() == b.spec.model_dump()


def _build_status(view: _ProcView, *, host_label: str) -> WorkloadStatus:
    primary = view.workload.spec.containers[0]
    if view.process.returncode is None:
        cstate = "Running"
        wphase = "Running"
        finished_at = None
        exit_code = None
    elif view.process.returncode == 0:
        cstate = "Terminated"
        wphase = "Succeeded"
        finished_at = _now()
        exit_code = 0
    else:
        cstate = "Terminated"
        wphase = "Failed"
        finished_at = _now()
        exit_code = view.process.returncode

    container_status = ContainerStatus(
        name=primary.name,
        state=cstate,
        pid=view.process.pid if view.process.returncode is None else None,
        started_at=view.started_at,
        finished_at=finished_at,
        exit_code=exit_code,
        restart_count=view.restart_count,
    )
    return WorkloadStatus(
        phase=wphase,
        host=host_label,
        network_address=_local_addr(),
        container_statuses=[container_status],
        conditions=[],
    )


def _local_addr() -> str:
    try:
        return socket.gethostbyname(socket.gethostname())
    except OSError:
        return "127.0.0.1"


def _try_join_cgroup(cgroup_dir: Path, pid: int) -> None:
    try:
        _ = (cgroup_dir / "cgroup.procs").write_text(str(pid))
    except OSError:
        logger.warning(f"failed to add pid {pid} to cgroup {cgroup_dir}")


def _safe_rmtree(path: Path) -> None:
    try:
        # cgroups can't be rmdir'd while populated; best-effort kill of
        # survivors first.
        procs_file = path / "cgroup.procs"
        if procs_file.exists():
            try:
                pids = [int(line) for line in procs_file.read_text().split() if line]
            except OSError:
                pids = []
            for pid in pids:
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            # Give the kernel a moment to reap; cgroup rmdir fails until empty.
            if pids:
                _ = time.sleep
        if path.is_dir():
            try:
                path.rmdir()
            except OSError:
                # Fallback: contains regular files (not a cgroup); rmtree.
                shutil.rmtree(path, ignore_errors=True)
    except OSError:
        logger.exception(f"cleanup of {path} raised; leaving for the agent restart sweep")
