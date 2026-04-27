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

Concurrency: every public method is safe to call concurrently against
*different* workloads. Concurrent calls for the *same* workload are
serialized via a per-workload lock — apply/delete races could otherwise
spawn-then-leak or kill-then-respawn.
"""

import asyncio
import contextlib
import logging
import os
import shutil
import signal
import socket
from collections.abc import AsyncIterator, Coroutine
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import final, override

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


@dataclass
class _WorkloadProc:
    """One supervised process per workload."""

    workload: Workload
    process: asyncio.subprocess.Process
    started_at: datetime
    cgroup_dir: Path | None = None
    restart_count: int = 0


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
        self._procs: dict[tuple[str | None, str], _WorkloadProc] = {}
        # One lock per workload key, created lazily by ``_lock_for``.
        # Acquiring this lock is the prerequisite for *any* mutation of
        # ``_procs[key]`` — apply, delete, restart. The previous design
        # locked on ``existing._proc.lock`` after a dict lookup, which
        # left a window between "delete pops nothing because apply
        # hasn't inserted yet" and "apply inserts post-spawn". Now both
        # paths serialize on the same per-key lock from the start.
        # Locks accumulate; we never remove entries because removing
        # while another task is awaiting the same key would race.
        # Memory cost: one Lock per workload name ever seen. Acceptable.
        self._key_locks: dict[tuple[str | None, str], asyncio.Lock] = {}
        # One queue per subscriber. We don't expect many — the agent
        # holds one — but the same fan-out shape lets tests subscribe
        # without monopolising the channel.
        self._subs: set[asyncio.Queue[RuntimeEvent | None]] = set()
        # Background tasks (probe loops, wait-exit reapers) live here so
        # they're not GC'd from under us. The done callback removes them
        # on completion to keep the set bounded.
        self._bg_tasks: set[asyncio.Task[None]] = set()
        self._closed: bool = False

    def _lock_for(self, key: tuple[str | None, str]) -> asyncio.Lock:
        """Get-or-create the per-key serialization lock.

        Synchronous: dict access in single-threaded asyncio is atomic,
        so no further lock is needed around this lookup.
        """
        lock = self._key_locks.get(key)
        if lock is None:
            lock = asyncio.Lock()
            self._key_locks[key] = lock
        return lock

    # ---- Runtime API ---------------------------------------------------

    @override
    async def apply(self, workload: Workload) -> WorkloadStatus:
        if self._closed:
            raise RuntimeError("SubprocessRuntime is closed")
        key = (workload.metadata.namespace, workload.metadata.name)
        async with self._lock_for(key):
            # Re-check under the lock. A concurrent close() may have
            # set ``_closed`` between the cheap check above and our
            # lock acquisition; without this, we would spawn a process
            # that close() already finished its drain pass on, leaking
            # it past the runtime's lifetime.
            if self._closed:
                raise RuntimeError("SubprocessRuntime is closed")
            existing = self._procs.get(key)
            if existing is not None:
                if _spec_equivalent(existing.workload, workload):
                    if existing.process.returncode is not None:
                        await self._restart_locked(existing, workload)
                    return _build_status(existing, host_label=self._host_label)
                # Spec changed — Recreate strategy.
                await self._stop_locked(existing)
                _ = self._procs.pop(key, None)

            proc = await self._spawn(workload)
            # Register the wait-exit reaper *before* publishing or
            # spawning the probe loop. If anything between now and the
            # end of apply() raises (or apply gets cancelled), the
            # reaper still owns the process and will publish EXITED
            # when it exits. Without this ordering, a cancellation
            # mid-apply would leave an unreaped child whose death
            # nobody observes.
            self._procs[key] = proc
            self._spawn_bg(
                self._wait_exit(proc),
                name=f"wait[{workload.metadata.name}]",
            )
            status = _build_status(proc, host_label=self._host_label)
            await self._publish(
                RuntimeEvent(
                    type=RuntimeEventType.STARTED,
                    workload_namespace=workload.metadata.namespace,
                    workload_name=workload.metadata.name,
                    status=status,
                    timestamp=_now(),
                )
            )
            if workload.spec.readiness_probe is not None:
                self._spawn_bg(
                    self._probe_loop(proc, workload.spec.readiness_probe),
                    name=f"probe[{workload.metadata.name}]",
                )
            return status

    def _spawn_bg(self, coro: "Coroutine[object, object, None]", *, name: str) -> None:
        task = asyncio.create_task(coro, name=name)
        self._bg_tasks.add(task)
        task.add_done_callback(self._bg_tasks.discard)

    @override
    async def delete(self, *, namespace: str | None, name: str) -> None:
        key = (namespace, name)
        async with self._lock_for(key):
            proc = self._procs.pop(key, None)
            if proc is None:
                return
            await self._stop_locked(proc)

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
    def watch(self) -> AsyncIterator[RuntimeEvent | None]:
        return self._subscribe()

    @override
    async def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        # Snapshot then drain so concurrent applies don't trip us.
        keys = list(self._procs.keys())
        for ns, name in keys:
            await self.delete(namespace=ns, name=name)
        for sub in list(self._subs):
            try:
                sub.put_nowait(None)
            except asyncio.QueueFull:
                pass
        self._subs.clear()

    # ---- subscribe -----------------------------------------------------

    async def _subscribe(self) -> AsyncIterator[RuntimeEvent | None]:
        queue: asyncio.Queue[RuntimeEvent | None] = asyncio.Queue(maxsize=1024)
        self._subs.add(queue)
        # Yield None immediately so the consumer (agent) sees the
        # registration ack before any event-pump await — see
        # ``Runtime.watch``'s contract.
        yield None
        try:
            while True:
                event = await queue.get()
                if event is None:
                    return
                yield event
        finally:
            self._subs.discard(queue)

    async def _publish(self, event: RuntimeEvent) -> None:
        dead: list[asyncio.Queue[RuntimeEvent | None]] = []
        for sub in self._subs:
            try:
                sub.put_nowait(event)
            except asyncio.QueueFull:
                # Subscriber too slow — drop. Same shape as WatchBus.
                dead.append(sub)
        for sub in dead:
            self._subs.discard(sub)

    # ---- spawn / stop / restart ---------------------------------------

    async def _spawn(self, workload: Workload) -> _WorkloadProc:
        if not workload.spec.containers:
            raise RuntimeError(
                f"workload {workload.metadata.name} has zero containers — pydantic should have rejected"
            )
        primary = workload.spec.containers[0]
        argv = _build_argv(primary)
        env = dict(os.environ) | primary.env
        cgroup_dir = self._maybe_create_cgroup(workload)
        # try/finally rather than narrow ``except OSError`` because the
        # await can also fail with CancelledError (or any BaseException),
        # and the previous narrow catch left the cgroup_dir on disk in
        # those cases. A leaked cgroup_dir survives until either the
        # daemon's systemd unit dies (KillMode=control-group tears the
        # whole tree down) or the next start runs ``clean_cgroup_root``.
        # The ``spawned`` flag distinguishes "we own the dir" from
        # "we successfully passed ownership to the returned _WorkloadProc"
        # so the finally only cleans on failure.
        spawned = False
        try:
            process = await asyncio.create_subprocess_exec(
                argv[0],
                *argv[1:],
                env=env,
                cwd=primary.workdir,
                stdin=asyncio.subprocess.DEVNULL,
                stdout=asyncio.subprocess.DEVNULL,
                stderr=asyncio.subprocess.DEVNULL,
            )
            spawned = True
        finally:
            if not spawned and cgroup_dir is not None:
                _safe_rmtree(cgroup_dir)
        if cgroup_dir is not None:
            _try_join_cgroup(cgroup_dir, process.pid)
        return _WorkloadProc(
            workload=workload,
            process=process,
            started_at=_now(),
            cgroup_dir=cgroup_dir,
        )

    async def _restart_locked(self, existing: _WorkloadProc, workload: Workload) -> None:
        existing.restart_count += 1
        replacement = await self._spawn(workload)
        # Preserve restart_count across the swap.
        replacement.restart_count = existing.restart_count
        existing.process = replacement.process
        existing.started_at = replacement.started_at
        existing.cgroup_dir = replacement.cgroup_dir
        existing.workload = workload

    async def _stop_locked(self, proc: _WorkloadProc) -> None:
        grace = proc.workload.spec.termination_grace_s
        if proc.process.returncode is None:
            try:
                proc.process.terminate()
            except ProcessLookupError:
                pass
            try:
                _ = await asyncio.wait_for(proc.process.wait(), timeout=grace)
            except asyncio.TimeoutError:
                try:
                    proc.process.kill()
                except ProcessLookupError:
                    pass
                with contextlib.suppress(asyncio.TimeoutError):
                    _ = await asyncio.wait_for(proc.process.wait(), timeout=2.0)
        if proc.cgroup_dir is not None:
            _safe_rmtree(proc.cgroup_dir)
        await self._publish(
            RuntimeEvent(
                type=RuntimeEventType.EXITED,
                workload_namespace=proc.workload.metadata.namespace,
                workload_name=proc.workload.metadata.name,
                status=_build_status(proc, host_label=self._host_label),
                timestamp=_now(),
            )
        )

    async def _wait_exit(self, proc: _WorkloadProc) -> None:
        try:
            _ = await proc.process.wait()
        except asyncio.CancelledError:
            return
        await self._publish(
            RuntimeEvent(
                type=RuntimeEventType.EXITED,
                workload_namespace=proc.workload.metadata.namespace,
                workload_name=proc.workload.metadata.name,
                status=_build_status(proc, host_label=self._host_label),
                timestamp=_now(),
            )
        )

    # ---- readiness probe ----------------------------------------------

    async def _probe_loop(self, proc: _WorkloadProc, probe: Probe) -> None:
        if probe.initial_delay_s > 0:
            await asyncio.sleep(probe.initial_delay_s)
        failures = 0
        while proc.process.returncode is None:
            ok = await self._probe_once(proc, probe)
            if ok:
                if failures > 0:
                    failures = 0
                    await self._publish(
                        RuntimeEvent(
                            type=RuntimeEventType.STATUS_CHANGED,
                            workload_namespace=proc.workload.metadata.namespace,
                            workload_name=proc.workload.metadata.name,
                            status=_build_status(proc, host_label=self._host_label),
                            timestamp=_now(),
                        )
                    )
            else:
                failures += 1
                if failures >= probe.failure_threshold:
                    await self._publish(
                        RuntimeEvent(
                            type=RuntimeEventType.STATUS_CHANGED,
                            workload_namespace=proc.workload.metadata.namespace,
                            workload_name=proc.workload.metadata.name,
                            status=_build_status(proc, host_label=self._host_label),
                            timestamp=_now(),
                        )
                    )
                    return
            await asyncio.sleep(probe.period_s)

    async def _probe_once(self, _proc: _WorkloadProc, probe: Probe) -> bool:
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


def _build_status(proc: _WorkloadProc, *, host_label: str) -> WorkloadStatus:
    primary = proc.workload.spec.containers[0]
    if proc.process.returncode is None:
        cstate = "Running"
        wphase = "Running"
        finished_at = None
        exit_code = None
    elif proc.process.returncode == 0:
        cstate = "Terminated"
        wphase = "Succeeded"
        finished_at = _now()
        exit_code = 0
    else:
        cstate = "Terminated"
        wphase = "Failed"
        finished_at = _now()
        exit_code = proc.process.returncode

    container_status = ContainerStatus(
        name=primary.name,
        state=cstate,
        pid=proc.process.pid if proc.process.returncode is None else None,
        started_at=proc.started_at,
        finished_at=finished_at,
        exit_code=exit_code,
        restart_count=proc.restart_count,
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
        if path.is_dir():
            try:
                path.rmdir()
            except OSError:
                # Fallback: contains regular files (not a cgroup); rmtree.
                shutil.rmtree(path, ignore_errors=True)
    except OSError:
        logger.exception(f"cleanup of {path} raised; leaving for the agent restart sweep")
