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

**Architecture: one supervisor Resource per workload.** Each call to
``apply`` either reuses an existing :class:`_ProcessSupervisor` (spec
unchanged) or shuts down the old one and starts a fresh one.

Each supervisor implements the :class:`~orchestrator.lifecycle.Resource`
Protocol cleanly: a single :class:`~contextlib.AsyncExitStack` owns
every cleanup action (cgroup rmdir, sync SIGKILL, monitor-task
cancel, probe-task cancel). Cleanup actions are pushed as **sync**
callbacks so the supervisor's ``__aexit__`` has no awaits — the
unwind is unconditional and cancellation-proof, by construction.

- ``__aexit__`` (sync): unwinds the stack in LIFO. Sends SIGKILL
  via the kernel (a sync syscall — fires unconditionally), cancels
  the monitor and probe tasks, removes the cgroup. Publishes a
  synthetic ``EXITED`` if the natural-exit path didn't already.
  No awaits.
- ``shutdown()`` (async): graceful drain. SIGTERM → wait up to
  ``termination_grace_s`` → SIGKILL if still alive → wait for reap.
  Then the surrounding ``__aexit__`` runs the stack and the kill
  callback is a no-op (process already dead).

The runtime composes supervisors as child resources: it owns each
supervisor's ``running()`` context for the supervisor's lifetime,
calls ``shutdown()`` on supersede/delete (graceful), and falls back
to sync release on its own ``__aexit__`` (forceful).
"""

import asyncio
import logging
import os
import shutil
import signal
import socket
from collections.abc import AsyncGenerator
from contextlib import AbstractAsyncContextManager, asynccontextmanager
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Self, final, override

from ..broadcast import BroadcastQueue
from ..lifecycle import (
    CheckedExitStack,
    GracefulAbort,
    Reaper,
    Resource,
    ResourceNotRunning,
    StopToken,
    cancel_and_collect,
)
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
class _ProcessSupervisor(Resource):
    """One workload, one process, one Resource lifetime.

    Built around a single :class:`CheckedExitStack` registered during
    ``running().__aenter__``. The stack owns every cleanup action:
    cgroup directory, sync SIGKILL, monitor task cancel, probe task
    cancel. Cleanup actions are pushed as **sync** callbacks
    (``stack.callback(...)``), so the stack's unwind on ``__aexit__``
    has no awaits — sync release is unconditional and cancellation-
    proof by construction.

    The supervisor does **not** spawn a body task that contains the
    process's lifetime. The process is spawned directly during
    ``__aenter__`` and held as ``self._process``. A small monitor
    task waits for ``process.wait()`` and publishes ``EXITED`` on
    natural exit; if the supervisor is torn down forcefully, the
    monitor task is cancelled and ``__aexit__`` publishes a
    synthetic ``EXITED`` itself.

    Surface to the runtime:

    - :attr:`initial_status` — the status snapshot computed at spawn,
      returned synchronously once ``__aenter__`` completes.
    - :attr:`view` — the latest ``_ProcView`` snapshot.
    - :attr:`workload` — the spec this supervisor owns.
    """

    def __init__(
        self,
        workload: Workload,
        *,
        host_label: str,
        cgroup_root: Path | None,
        events_bus: BroadcastQueue[RuntimeEvent],
        parent_token: StopToken,
        reaper: Reaper,
        predecessor: "_ProcessSupervisor | None" = None,
        predecessor_cm: AbstractAsyncContextManager["_ProcessSupervisor"] | None = None,
    ):
        self._workload: Workload = workload
        self._host_label: str = host_label
        self._cgroup_root: Path | None = cgroup_root
        self._events: BroadcastQueue[RuntimeEvent] = events_bus
        self._reaper: Reaper = reaper
        self._stop_token: StopToken = parent_token.child()
        self._is_running: bool = False
        self._process: asyncio.subprocess.Process | None = None
        self._view: _ProcView | None = None
        self._initial_status: WorkloadStatus | None = None
        self._monitor_task: asyncio.Task[None] | None = None
        self._probe_task: asyncio.Task[None] | None = None
        self._cgroup_dir: Path | None = None
        # Set once the EXITED event has been published, so the sync
        # ``__aexit__`` doesn't double-publish if the monitor task
        # already published on natural exit.
        self._exit_published: bool = False
        # Predecessor (the supervisor we're superseding) and its
        # context manager. We own its tear-down: drain it gracefully
        # in our own ``__aenter__`` and aexit its cm there too.
        # See ``running()`` for the discipline; refs are dropped
        # after release to make "shutdown after aexit" structurally
        # impossible.
        assert (predecessor is None) == (predecessor_cm is None), (
            "predecessor and predecessor_cm must be supplied together"
        )
        self._predecessor: _ProcessSupervisor | None = predecessor
        self._predecessor_cm: AbstractAsyncContextManager[_ProcessSupervisor] | None = (
            predecessor_cm
        )

    @property
    def workload(self) -> Workload:
        return self._workload

    @property
    def view(self) -> _ProcView | None:
        return self._view

    @property
    def initial_status(self) -> WorkloadStatus:
        """Status snapshot taken at spawn time. Available immediately
        after ``running().__aenter__`` returns."""
        if self._initial_status is None:
            raise ResourceNotRunning("supervisor.initial_status before running()")
        return self._initial_status

    @property
    @override
    def stop_token(self) -> StopToken:
        return self._stop_token

    @override
    @asynccontextmanager
    async def running(self) -> AsyncGenerator[Self]:
        """Drain predecessor (if any), spawn the process, yield.

        Two-phase init:

        **Predecessor drain (before our own stack opens).** If we
        were handed a predecessor, we await ``predecessor.shutdown()``
        here — *outside* our own :class:`CheckedExitStack`. Two
        reasons: (a) our ``stop_token``'s ``GracefulAbort`` shouldn't
        interrupt the predecessor's cleanup (we already committed
        to draining it); (b) doing the predecessor cleanup *before*
        our stack opens means our stack stays empty until we're
        ready to spawn — so a stop_token check at the first push
        aborts cleanly with nothing of ours to unwind.

        The drain is wrapped in ``try/finally`` so that a cancel
        during ``await predecessor.shutdown()`` still aexits the
        predecessor cm (its body has no awaits, so the aexit
        completes even under cancel-pending). After both shutdown
        and aexit have run, we *drop the references* — making it
        structurally impossible to call ``shutdown`` on a
        predecessor whose cm has already been aexit'd.

        **Our own setup (inside CheckedExitStack):**

        1. Create the cgroup directory (if applicable) and register
           ``_sync_rm_cgroup``.
        2. Spawn the subprocess and register ``_sync_kill_process``
           (which on forceful tear-down also offloads the reap +
           EXITED publish to the reaper).
        3. Join the cgroup (best-effort) and build the view + initial
           status. Publish ``STARTED``.
        4. Spawn the monitor task and register sync cancel. Monitor
           publishes EXITED on natural exit; if cancelled forcefully,
           the reaper task takes over the publish.
        5. Spawn the probe task if configured. Register sync cancel.

        ``__aexit__`` is sync: the stack unwinds in LIFO, every
        callback runs without awaits. Anything that genuinely needs
        async work (waitpid, cgroup-rmdir-after-reap) is offloaded
        to the reaper.
        """
        if self._is_running:
            raise ResourceNotRunning("_ProcessSupervisor.running re-entered while already running")
        # ---- predecessor drain FIRST (no own-stack open yet) ----
        # Runs before any other prelude work because the runtime has
        # already removed the predecessor from its dicts in
        # ``_start_supervisor`` — if anything below (``_build_argv``
        # raising on a TarballImage, env construction, anything else)
        # raises, we'd orphan the predecessor's process. Drain first
        # so the predecessor is unconditionally released no matter
        # what the rest of init does.
        await self._drain_predecessor()

        workload = self._workload
        # pydantic enforces min_length=1 on WorkloadSpec.containers;
        # this assert is the runtime's own check that the contract held.
        assert workload.spec.containers, f"workload {workload.metadata.name} has zero containers"
        primary = workload.spec.containers[0]
        argv = _build_argv(primary)
        env = dict(os.environ) | primary.env

        try:
            async with CheckedExitStack(self._stop_token) as stack:
                # 1. cgroup_dir (best-effort; None disables limits).
                cgroup_dir = _maybe_create_cgroup(workload, self._cgroup_root)
                if cgroup_dir is not None:
                    self._cgroup_dir = cgroup_dir
                    _ = stack.callback(self._sync_rm_cgroup)

                # 2. Spawn process. The handle is exposed on the
                # supervisor — no async-with around the process; the
                # sync kill callback below does cleanup on every
                # exit path.
                process = await asyncio.create_subprocess_exec(
                    *argv,
                    env=env,
                    cwd=primary.workdir,
                    stdin=asyncio.subprocess.DEVNULL,
                    stdout=asyncio.subprocess.DEVNULL,
                    stderr=asyncio.subprocess.DEVNULL,
                )
                self._process = process
                _ = stack.callback(self._sync_kill_process)

                # 3. Build view, publish STARTED.
                if cgroup_dir is not None:
                    _try_join_cgroup(cgroup_dir, process.pid)
                view = _ProcView(
                    workload=workload,
                    process=process,
                    started_at=_now(),
                    cgroup_dir=cgroup_dir,
                )
                self._view = view
                status = _build_status(view, host_label=self._host_label)
                view.initial_status = status
                self._initial_status = status
                self._events.publish(
                    RuntimeEvent(
                        type=RuntimeEventType.STARTED,
                        workload_namespace=workload.metadata.namespace,
                        workload_name=workload.metadata.name,
                        status=status,
                        timestamp=_now(),
                    )
                )

                # 4. Sync safety-net publish: on graceful tear-down
                #    the monitor may have been cancelled before it
                #    got to publish (race between SIGCHLD callback
                #    and shutdown's cancel-and-await). If returncode
                #    is set when this callback fires (graceful path)
                #    we publish here. If returncode is None (forceful
                #    path), this is a no-op — the reaper task that
                #    ``_sync_kill_process`` offloads will publish
                #    after the real reap.
                _ = stack.callback(self._publish_exited_if_known)

                # 5. Monitor task: observe natural process exit.
                #    On natural exit it publishes EXITED with the
                #    real exit code. On forceful tear-down it gets
                #    cancelled by ``_sync_cancel_monitor``; the
                #    EXITED publish then comes from the reaper task
                #    that ``_sync_kill_process`` offloads.
                self._monitor_task = asyncio.create_task(
                    self._monitor_loop(),
                    name=f"sup-monitor[{workload.metadata.name}]",
                )
                self._monitor_task.add_done_callback(_drain_task_exception)
                _ = stack.callback(self._sync_cancel_monitor)

                # 5. Probe task (optional).
                if workload.spec.readiness_probe is not None:
                    self._probe_task = asyncio.create_task(
                        self._probe_loop(view, workload.spec.readiness_probe),
                        name=f"probe[{workload.metadata.name}]",
                    )
                    self._probe_task.add_done_callback(_drain_task_exception)
                    _ = stack.callback(self._sync_cancel_probe)

                self._is_running = True
                try:
                    yield self
                finally:
                    # SYNC. No await. The stack's unwind runs every
                    # registered callback in LIFO order: cancel probe,
                    # cancel monitor, publish EXITED (if not already),
                    # SIGKILL process, rm cgroup. All sync.
                    self._is_running = False
                    self._stop_token.set()
        except GracefulAbort:
            # stop_token fired mid-init via CheckedExitStack safe-point.
            # Convert to a clear semantic error: returning silently
            # would make @asynccontextmanager raise RuntimeError
            # ("generator didn't yield"). ResourceNotRunning is the
            # right caller-facing signal — they tried to enter a
            # resource that's already cancel-bound.
            raise ResourceNotRunning(
                "_ProcessSupervisor.running entered with stop_token already set"
            ) from None

    @override
    async def shutdown(self) -> None:
        """Graceful drain: SIGTERM → grace → SIGKILL → reap.

        On return the process is fully reaped; the surrounding
        ``__aexit__`` then runs the stack and the kill callback is
        a no-op (process already dead). Caller-cancel propagates.
        """
        if not self._is_running:
            raise ResourceNotRunning("_ProcessSupervisor.shutdown called outside running()")
        self._stop_token.set()
        process = self._process
        if process is None:
            return
        if process.returncode is None:
            try:
                process.terminate()
            except ProcessLookupError:
                pass
            grace = self._workload.spec.termination_grace_s
            try:
                _ = await asyncio.wait_for(process.wait(), timeout=grace)
            except TimeoutError:
                pass
            if process.returncode is None:
                try:
                    process.kill()
                except ProcessLookupError:
                    pass
                try:
                    _ = await asyncio.wait_for(process.wait(), timeout=2.0)
                except TimeoutError:
                    pass
        # Cancel + collect monitor / probe so any pending publish
        # finishes before the runtime moves on. ``cancel_and_collect``
        # distinguishes the inner cancel (we issued) from an outer
        # cancel of *us* — propagating the latter is the contract.
        for task in (self._monitor_task, self._probe_task):
            if task is not None:
                await cancel_and_collect(task)

    def release_sync(self) -> None:
        """Sync release: run every cleanup callback directly. Idempotent.

        Used by the runtime's own sync ``__aexit__`` when it must
        release every supervisor without awaiting each one's
        context manager. We call the same sync callbacks the stack
        would invoke on unwind, in the same LIFO order. They're all
        idempotent, so a later stack unwind (if it ever runs) is a
        no-op.
        """
        self._is_running = False
        self._stop_token.set()
        self._sync_cancel_probe()
        self._sync_cancel_monitor()
        # If returncode is already known (graceful path raced past
        # monitor), publish here. _sync_kill_process below offloads
        # the reap+publish to the reaper for the forceful path.
        self._publish_exited_if_known()
        self._sync_kill_process()
        self._sync_rm_cgroup()

    async def _drain_predecessor(self) -> None:
        """Graceful drain + aexit of the supervisor we're superseding.

        Runs *before* our own ``CheckedExitStack`` opens (see
        ``running()``'s docstring for why). The discipline:

        - Skip if no predecessor, or if predecessor is already
          torn down (``_is_running`` False) — the latter can only
          happen via an out-of-band ``release_sync`` (defensive,
          not expected with the current runtime sequencing).
        - ``await predecessor.shutdown()`` (graceful drain).
        - ``finally: await predecessor_cm.__aexit__(...)`` so a
          cancel mid-shutdown still aexits the cm. The cm's body
          has no inner awaits, so it runs even under cancel-pending.
        - Drop both refs after release. This is what makes
          "shutdown after aexit" structurally impossible: the only
          way to call shutdown on the predecessor is via these
          refs, and they're cleared the moment its cm has been
          aexit'd.
        """
        predecessor = self._predecessor
        predecessor_cm = self._predecessor_cm
        if predecessor is None or predecessor_cm is None:
            return
        # Drop refs *before* the awaits below so even an unexpected
        # exception path (e.g. KeyboardInterrupt) can't leave us in
        # a state where we'd retry on a torn-down predecessor.
        self._predecessor = None
        self._predecessor_cm = None
        if not predecessor._is_running:
            # Already released elsewhere; nothing to do, but we
            # still need to run cm.__aexit__ to close the asyncgen.
            _ = await predecessor_cm.__aexit__(None, None, None)
            return
        try:
            await predecessor.shutdown()
        finally:
            # cm.__aexit__'s body has no awaits, so it runs to
            # completion even under cancel-pending. The
            # predecessor's stack unwind fires its sync release.
            _ = await predecessor_cm.__aexit__(None, None, None)

    # ---- sync stack callbacks -----------------------------------------

    def _sync_kill_process(self) -> None:
        """Sync stack callback. SIGKILL the live process, then hand
        the *async* reap (waitpid + EXITED publish) to the reaper.

        Orderly path: ``shutdown()`` already reaped, ``returncode``
        is set, this is a no-op — reaper sees nothing.

        Forceful path: process is alive; we send SIGKILL (sync
        syscall, fires unconditionally) and offload the wait. The
        reaper's task awaits SIGCHLD, then publishes EXITED with the
        real exit code. By construction the published event is
        internally consistent (no synthetic-Failed-with-fake-status).
        """
        process = self._process
        if process is None or process.returncode is not None:
            return
        try:
            process.kill()
        except ProcessLookupError:
            return
        self._reaper.offload(
            self._async_reap_and_publish(process),
            name=f"reap[{self._workload.metadata.name}]",
        )

    async def _async_reap_and_publish(self, process: asyncio.subprocess.Process) -> None:
        """Reaper task: await the OS reaping the process, then
        publish the real EXITED.

        Indefinite by design — the reaper's job is to wait for
        kernel state. Caller's policy decides whether to budget via
        ``wait_for`` around ``reaper.wait()``.
        """
        _ = await process.wait()
        # Returncode is now set; delegate to the shared publisher.
        self._publish_exited_if_known()

    def _sync_rm_cgroup(self) -> None:
        """Sync stack callback. Try fast-path cleanup; offload only if
        the kernel needs time to reap.

        Three cases:

        - Empty cgroup (kernel already reaped): ``rmdir`` succeeds
          inside ``_safe_rmtree``. Done sync.
        - Non-cgroup test fixture (regular dir with files but no
          ``cgroup.procs``): ``_safe_rmtree``'s ``shutil.rmtree``
          fallback removes it. Done sync.
        - Real cgroup with live tasks the kernel hasn't reaped:
          ``_safe_rmtree`` SIGKILLs survivors but the dir lingers
          until SIGCHLD is processed. Offload retry to reaper.
        """
        cgroup_dir = self._cgroup_dir
        if cgroup_dir is None or not cgroup_dir.exists():
            return
        _safe_rmtree(cgroup_dir)
        if not cgroup_dir.exists():
            return
        # Real cgroup, kernel-pending. Offload retry-until-empty.
        self._reaper.offload(
            _async_rm_cgroup_with_retry(cgroup_dir),
            name=f"rm-cgroup[{cgroup_dir.name}]",
        )

    def _sync_cancel_monitor(self) -> None:
        task = self._monitor_task
        if task is not None and not task.done():
            _ = task.cancel()

    def _sync_cancel_probe(self) -> None:
        task = self._probe_task
        if task is not None and not task.done():
            _ = task.cancel()

    def _publish_exited_if_known(self) -> None:
        """Idempotent EXITED publish — only fires when ``returncode``
        is set (i.e., process has actually exited).

        Two callsites:

        - Sync stack callback during ``__aexit__``: graceful path's
          safety net. Monitor may have been cancelled mid-publish;
          shutdown reaped the process so returncode is set; publish
          here.
        - Reaper task ``_async_reap_and_publish``: after waitpid
          returns; same code path, ``_exit_published`` flag prevents
          a double publish if the safety net already fired.

        On the *forceful* path with no shutdown(), returncode is None
        when this fires — no-op. The reaper task offloaded by
        ``_sync_kill_process`` then awaits the real reap and calls
        this method again with returncode set.
        """
        if self._exit_published:
            return
        view = self._view
        if view is None or view.process.returncode is None:
            return
        self._exit_published = True
        self._events.publish(
            RuntimeEvent(
                type=RuntimeEventType.EXITED,
                workload_namespace=view.workload.metadata.namespace,
                workload_name=view.workload.metadata.name,
                status=_build_status(view, host_label=self._host_label),
                timestamp=_now(),
            )
        )

    # ---- background tasks ---------------------------------------------

    async def _monitor_loop(self) -> None:
        """Wait for natural process exit; publish EXITED.

        Cancelled by the stack's ``_sync_cancel_monitor`` callback on
        forceful tear-down. The publish is delegated to the shared
        ``_publish_exited_if_known`` so the safety-net stack callback
        and the reaper task can't double-publish (the
        ``_exit_published`` flag is shared).
        """
        process = self._process
        if process is None:
            return
        _ = await process.wait()
        self._publish_exited_if_known()

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


def _drain_task_exception(task: asyncio.Task[object]) -> None:
    """Suppress 'task exception was never retrieved' for tasks the
    supervisor cancels without awaiting (the sync stack unwind
    can't await).
    """
    if task.cancelled():
        return
    exc = task.exception()
    if exc is not None:
        logger.error(
            f"supervisor background task {task.get_name()} crashed",
            exc_info=exc,
        )


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
        reaper: Reaper,
        cgroup_root: Path | None = None,
        host_label: str = "local",
        parent_token: StopToken | None = None,
    ):
        self._state_dir: Path = state_dir
        self._cgroup_root: Path | None = cgroup_root
        self._host_label: str = host_label
        # Top-level reaper for kernel-state cleanup that ``__aexit__``
        # can't do sync (waitpid after SIGKILL, cgroup rmdir-after-
        # reap). Threaded into each supervisor on construction.
        self._reaper: Reaper = reaper
        self._supervisors: dict[_Key, _ProcessSupervisor] = {}
        # Per-supervisor running() context, kept open for the
        # supervisor's lifetime. Closed (sync ``__aexit__`` of the
        # supervisor) on supersede/delete or runtime tear-down.
        self._supervisor_cms: dict[_Key, AbstractAsyncContextManager[_ProcessSupervisor]] = {}
        # Single fan-out bus for runtime events. ``BroadcastQueue``
        # owns the per-subscriber queues + overflow handling — same
        # primitive WatchBus wraps.
        self._events: BroadcastQueue[RuntimeEvent] = BroadcastQueue("subprocess-runtime")
        self._closed: bool = False
        self._stop_token: StopToken = (
            parent_token.child() if parent_token is not None else StopToken()
        )
        self._is_running: bool = False

    @property
    @override
    def stop_token(self) -> StopToken:
        return self._stop_token

    # ---- Runtime API ---------------------------------------------------

    @override
    async def apply(self, workload: Workload) -> WorkloadStatus:
        if self._closed:
            raise RuntimeError("SubprocessRuntime is closed")
        key: _Key = (workload.metadata.namespace, workload.metadata.name)
        existing = self._supervisors.get(key)
        if existing is not None and _spec_equivalent(existing.workload, workload):
            view = existing.view
            assert view is not None, (
                "supervisor exists but has no view; this is a state-machine bug"
            )
            return _build_status(view, host_label=self._host_label)
        # Spec changed (or no existing): hand the existing supervisor
        # to the new one as its predecessor. The new supervisor's
        # __aenter__ will drain + aexit it before spawning. The
        # cascade lives in the supervisor chain, not in apply().
        existing_cm = self._supervisor_cms.get(key) if existing is not None else None
        return await self._start_supervisor(
            workload,
            predecessor=existing,
            predecessor_cm=existing_cm,
            existing_key=key if existing is not None else None,
        )

    @override
    async def delete(self, *, namespace: str | None, name: str) -> None:
        await self._stop_supervisor((namespace, name))

    @override
    async def get(self, *, namespace: str | None, name: str) -> WorkloadStatus | None:
        sup = self._supervisors.get((namespace, name))
        if sup is None:
            return None
        view = sup.view
        if view is None:
            return None
        return _build_status(view, host_label=self._host_label)

    @override
    async def list(self) -> list[WorkloadStatus]:
        out: list[WorkloadStatus] = []
        for sup in self._supervisors.values():
            view = sup.view
            if view is None:
                continue
            out.append(_build_status(view, host_label=self._host_label))
        return out

    @override
    def watch(self):
        return self._events.subscribe()

    @override
    @asynccontextmanager
    async def running(self) -> AsyncGenerator[Self]:
        """Resource-shape lifecycle.

        ``__aenter__`` flips ``_is_running`` and yields self.
        ``__aexit__`` is **synchronous**: marks closed, sets the
        stop_token (cascade to every supervisor's child token),
        closes the events bus, and *requests* sync release of every
        supervisor (cancel its task, no await). It does **not**
        await — that's :meth:`shutdown`'s job.

        If the caller didn't call ``shutdown()`` first, every
        supervisor's sync stack callbacks fire here (SIGKILL,
        cgroup rmdir) — best-effort but unconditional, since the
        callbacks are sync. Loop close eventually reaps the
        zombies. For graceful drain (SIGTERM + grace + reap), call
        ``await rt.shutdown()`` inside the with-block (wrap with
        ``asyncio.wait_for(...)`` if you want a budget).
        """
        if self._is_running:
            raise ResourceNotRunning("SubprocessRuntime.running re-entered while already running")
        self._is_running = True
        try:
            yield self
        finally:
            # SYNC. No await. Cascading sync cleanup only.
            self._is_running = False
            self._closed = True
            self._stop_token.set()
            for sup in self._supervisors.values():
                sup.release_sync()
            self._events.close()

    @override
    async def shutdown(self) -> None:
        """Graceful drain: shutdown every supervisor and await each.

        Must be called inside ``running()``; raises
        :exc:`ResourceNotRunning` otherwise.

        Caller controls the budget by wrapping with
        :func:`asyncio.wait_for`. Caller-cancel propagates as
        :exc:`asyncio.CancelledError` (or :exc:`TimeoutError` from
        wait_for); ``__aexit__`` reconciles any half-state.
        """
        if not self._is_running:
            raise ResourceNotRunning("SubprocessRuntime.shutdown called outside running()")
        # Mark closed so further apply() rejects, but do NOT clear
        # the running flag — caller is still inside running().
        self._closed = True
        self._stop_token.set()
        sups = list(self._supervisors.items())
        if not sups:
            return
        # Drain in parallel — each supervisor's shutdown handles its
        # own RAII unwind independently.
        _ = await asyncio.gather(
            *(self._stop_supervisor(key) for key, _sup in sups),
            return_exceptions=True,
        )

    # ---- supervisor lifecycle ------------------------------------------

    async def _start_supervisor(
        self,
        workload: Workload,
        *,
        predecessor: _ProcessSupervisor | None = None,
        predecessor_cm: AbstractAsyncContextManager[_ProcessSupervisor] | None = None,
        existing_key: _Key | None = None,
    ) -> WorkloadStatus:
        """Construct + enter a fresh supervisor; return its initial status.

        If ``predecessor`` is supplied, the new supervisor's
        ``__aenter__`` will drain + aexit it before spawning. We
        atomically transfer ownership of the predecessor entries
        from the runtime's dicts to the new supervisor's refs in
        the same sync block — no cancel point in between.
        """
        key: _Key = (workload.metadata.namespace, workload.metadata.name)
        sup = _ProcessSupervisor(
            workload,
            host_label=self._host_label,
            cgroup_root=self._cgroup_root,
            events_bus=self._events,
            parent_token=self._stop_token,
            reaper=self._reaper,
            predecessor=predecessor,
            predecessor_cm=predecessor_cm,
        )
        # Hand off ownership: from this point on, ``sup`` is solely
        # responsible for the predecessor's tear-down. Sync block —
        # no awaits between pop and the create_task in the next call.
        if existing_key is not None:
            del self._supervisors[existing_key]
            del self._supervisor_cms[existing_key]
        cm = sup.running()
        _ = await cm.__aenter__()
        # ``__aenter__`` either succeeded (predecessor drained +
        # aexit'd, our spawn done, status set) or raised (predecessor
        # cleanup ran in the predecessor-drain finally, then our
        # stack unwind ran for any partially-acquired resources).
        self._supervisors[key] = sup
        self._supervisor_cms[key] = cm
        return sup.initial_status

    async def _stop_supervisor(self, key: _Key) -> None:
        sup = self._supervisors.pop(key, None)
        cm = self._supervisor_cms.pop(key, None)
        if sup is None or cm is None:
            return
        try:
            await sup.shutdown()
        finally:
            # cm.__aexit__ runs the supervisor's sync ``__aexit__``,
            # which is idempotent vs ``shutdown()``'s effects (both
            # call ``release_sync``).
            _ = await cm.__aexit__(None, None, None)


# --- helpers --------------------------------------------------------------


def _maybe_create_cgroup(workload: Workload, cgroup_root: Path | None) -> Path | None:
    if cgroup_root is None:
        return None
    primary = workload.spec.containers[0]
    if (
        primary.resources.cpu_millis is None
        and primary.resources.memory_bytes is None
        and primary.resources.io_weight is None
    ):
        return None
    cgroup_dir = (
        cgroup_root / f"wl-{workload.metadata.namespace or 'cluster'}-{workload.metadata.name}"
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


async def _async_rm_cgroup_with_retry(cgroup_dir: Path) -> None:
    """Reaper task: retry cgroup ``rmdir`` until the kernel reaps
    survivors and the dir empties.

    The fast-path sync rmdir in ``_sync_rm_cgroup`` already tried
    once. We retry on a backoff because the only thing in the way
    is "kernel hasn't yet processed SIGCHLD for the pids this cgroup
    held." That's a real-time wait — the asyncio loop's clock under
    a virtual-clock test won't advance the kernel's view, so
    operationally this only matters under real time.

    Indefinite by design (per Reaper contract). The kernel either
    reaps and we succeed, or the operator escalates externally.
    """
    delay = 0.05
    while cgroup_dir.exists():
        try:
            cgroup_dir.rmdir()
            return
        except OSError:
            # Try harder: SIGKILL any survivors, then retry.
            _safe_rmtree(cgroup_dir)
            if not cgroup_dir.exists():
                return
            await asyncio.sleep(delay)
            # Mild exponential, capped — kernel reaps fast normally.
            delay = min(delay * 2.0, 1.0)


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
            except OSError, ValueError:
                # OSError: read failed. ValueError: kernel raced our
                # read with a partial pid write, or cgroup.procs has
                # something we don't expect. Either way, best-effort.
                pids = []
            for pid in pids:
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            # No sync sleep here — blocking the event loop is the
            # wrong shape. The caller of ``_safe_rmtree`` who needs
            # the dir to actually disappear after kernel-reap goes
            # through ``_async_rm_cgroup_with_retry`` (offloaded to
            # the reaper) instead.
        if path.is_dir():
            try:
                path.rmdir()
            except OSError:
                # Fallback: contains regular files (not a cgroup); rmtree.
                shutil.rmtree(path, ignore_errors=True)
    except OSError:
        logger.exception(f"cleanup of {path} raised; leaving for the agent restart sweep")
