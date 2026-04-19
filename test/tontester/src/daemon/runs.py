"""Routing layer over per-run actors — the public API consumed by
:mod:`.ipc`, :mod:`.prom_proxy`, :mod:`.daemon`.

The whole package has exactly one shared mutable structure:
:attr:`RunsSupervisor._actors` (run_id → :class:`RunActor`), mutated only
by the supervisor's routing methods below. Everything else lives inside
each actor's private state; see :mod:`.run_actor` for the actor-side
invariants (exception safety, noexcept release, FS→SQLite→podman order,
reply invariant).

Client-cancel compensation
--------------------------

When a caller of :meth:`RunsSupervisor.register` or
:meth:`RunsSupervisor.acquire_query_pin` is cancelled after the actor
has already produced the held resource (LIVE run, pinned container), the
state would leak to daemon shutdown. :func:`_compensate_on_cancel` guards
these methods: the reply is wrapped in :func:`asyncio.shield` so the
actor's completion is never cancelled alongside the caller, and on
caller cancellation a background task awaits the eventual result and
sends the paired release message. No handler-level cancellation; actors
still run to completion. This is why ``_on_release`` and
``_on_release_pin`` are idempotent.

Escalated shutdown
------------------

:meth:`RunsSupervisor.shutdown` accepts an optional ``reply_timeout``.
On timeout, any actor that hasn't acknowledged ``_Shutdown`` is
force-cancelled. That's the one path where the actor-zone "no external
cancellation" invariant is deliberately violated: an actor mid-handler
sees ``CancelledError``, unwinds partially, and :meth:`recover`
reconciles whatever state was left on the next daemon boot (same
backstop as any abnormal termination). Drives the 2nd-signal /
``SIGTERM`` escalation in :mod:`.daemon`.
"""

import asyncio
import logging
from collections.abc import AsyncGenerator, Callable, Coroutine
from contextlib import asynccontextmanager
from pathlib import Path
from typing import final

from .models import (
    DaemonStopped,
    NoFreePorts,
    RunAlreadyActive,
    RunMetadata,
    RunSnapshot,
    RunStartFailed,
    RunStatus,
    TestMetadata,
)
from .protocols import ComposeLike, ProvisioningLike, StorageBackend
from .run_actor import (
    AcquirePinMsg,
    ConfigSnapshotMsg,
    MaybeReapMsg,
    PortAllocator,
    RegisterMsg,
    ReleaseMsg,
    ReleasePinMsg,
    RunActor,
    ShutdownMsg,
)

# Re-export the public types callers in .ipc / .daemon / tests import from here.
__all__ = [
    "DaemonStopped",
    "NoFreePorts",
    "QueryPin",
    "RunAlreadyActive",
    "RunSnapshot",
    "RunStartFailed",
    "RunsSupervisor",
]

logger = logging.getLogger(__name__)


@final
class QueryPin:
    """Token yielded by :meth:`RunsSupervisor.query_pin`.

    Exiting the context manager releases the pin automatically. For
    streaming responses whose body must outlive the HTTP handler, call
    :meth:`detach` and then :meth:`release` later from the generator's
    ``finally``; the context manager's exit becomes a no-op.
    """

    def __init__(self, port: int | None, supervisor: "RunsSupervisor", run_id: str):
        self.port = port
        self._supervisor = supervisor
        self._run_id = run_id
        self._detached = False
        self._released = False

    @property
    def detached(self):
        return self._detached

    async def release(self) -> None:
        if self._released:
            return
        self._released = True
        if self.port is not None:
            await self._supervisor.release_query_pin(self._run_id)

    def detach(self) -> None:
        self._detached = True


async def _compensate_on_cancel[T](
    reply: asyncio.Future[T],
    *,
    acquires: Callable[[T], bool],
    compensate: Callable[[], Coroutine[object, object, None]],
    run_id: str,
    op: str,
) -> T:
    """Await ``reply``; if the caller is cancelled after the actor produced a
    held resource, spawn a background task that waits for the actor's result
    and schedules the paired release.

    ``asyncio.shield`` keeps the actor's reply future alive when the caller
    is cancelled. Without it, asyncio cancels the awaited future as part of
    task cancellation, and the actor's later ``reply.set_result`` would hit
    :class:`asyncio.InvalidStateError` and kill the actor loop.
    """
    try:
        return await asyncio.shield(reply)
    except asyncio.CancelledError:

        async def _watch() -> None:
            try:
                result = await reply
            except Exception:
                # Actor raised — no resource was produced; nothing to undo.
                return
            if not acquires(result):
                return
            try:
                await compensate()
            except Exception:
                logger.exception(f"Compensation after cancelled {op}({run_id}) raised")

        _ = asyncio.create_task(_watch(), name=f"compensate-{op}-{run_id}")
        raise


@final
class RunsSupervisor:
    """Routes requests to per-run actors; owns actor lifecycle."""

    def __init__(
        self,
        *,
        instance_dir: Path,
        storage: StorageBackend,
        compose: ComposeLike,
        provisioning: ProvisioningLike,
        port_range: tuple[int, int],
        daemon_port: int,
        daemon_base_url: str,
        archive_idle_ttl_seconds: float = 300.0,
        reap_interval_seconds: float = 30.0,
        on_state_change: Callable[[str], None] | None = None,
    ):
        self.instance_dir = instance_dir
        self.archive_idle_ttl_seconds = archive_idle_ttl_seconds
        self.reap_interval_seconds = reap_interval_seconds

        self._storage = storage
        self._compose = compose
        self._provisioning = provisioning
        self._ports = PortAllocator(port_range)
        self._daemon_port = daemon_port
        self._daemon_base_url = daemon_base_url
        self._on_state_change = on_state_change

        self._actors: dict[str, RunActor] = {}
        self._actor_tasks: dict[str, asyncio.Task[None]] = {}
        self._reaper_task: asyncio.Task[None] | None = None
        self._stopped = False

    def _notify_state_change(self, run_id: str) -> None:
        """Fire the UI-facing notifier.

        Synchronous by contract: the notifier must not block the actor
        path. Implementations that need to do async work (DB read,
        broadcast) should spawn their own task and track its lifetime.
        """
        if self._on_state_change is not None:
            try:
                self._on_state_change(run_id)
            except Exception:
                logger.exception(f"State-change notifier raised for {run_id}")

    # ---- public API

    async def register(self, run_id: str, metadata: TestMetadata) -> RunMetadata:
        if self._stopped:
            raise DaemonStopped
        actor = self._get_or_spawn(run_id)
        reply: asyncio.Future[RunMetadata] = asyncio.get_running_loop().create_future()
        await actor.send(RegisterMsg(metadata=metadata, reply=reply))
        result = await _compensate_on_cancel(
            reply,
            acquires=lambda _: True,  # successful register always holds the run
            compensate=lambda: self.release(run_id),
            run_id=run_id,
            op="register",
        )
        self._notify_state_change(run_id)
        return result

    async def release(self, run_id: str) -> None:
        actor = self._actors.get(run_id)
        if actor is None:
            return
        reply: asyncio.Future[None] = asyncio.get_running_loop().create_future()
        await actor.send(ReleaseMsg(reply=reply))
        await reply
        self._notify_state_change(run_id)

    async def acquire_query_pin(self, run_id: str) -> int | None:
        if self._stopped:
            return None
        actor = self._get_or_spawn(run_id)
        reply: asyncio.Future[int | None] = asyncio.get_running_loop().create_future()
        await actor.send(AcquirePinMsg(reply=reply))
        return await _compensate_on_cancel(
            reply,
            acquires=lambda port: port is not None,
            compensate=lambda: self.release_query_pin(run_id),
            run_id=run_id,
            op="acquire_query_pin",
        )

    async def release_query_pin(self, run_id: str) -> None:
        actor = self._actors.get(run_id)
        if actor is None:
            return
        reply: asyncio.Future[None] = asyncio.get_running_loop().create_future()
        await actor.send(ReleasePinMsg(reply=reply))
        await reply

    @asynccontextmanager
    async def query_pin(self, run_id: str) -> AsyncGenerator[QueryPin]:
        """Scoped pin on the run's container.

        Exiting the block releases the pin automatically unless
        :meth:`QueryPin.detach` was called — that's the hand-off pattern
        for streaming responses whose body generator must outlive the
        HTTP handler (see ``prom_proxy``).
        """
        pin = QueryPin(await self.acquire_query_pin(run_id), self, run_id)
        try:
            yield pin
        finally:
            if not pin.detached:
                await pin.release()

    async def ensure_queryable(self, run_id: str) -> int | None:
        """Convenience: pin and immediately release. Used by tests that care
        only about the lazy-boot side effect, not the pin lifecycle."""
        port = await self.acquire_query_pin(run_id)
        if port is not None:
            await self.release_query_pin(run_id)
        return port

    async def recover(self) -> None:
        """Hard reset on startup: DORMANT all LIVE rows, stop survivors."""
        live_runs = await self._storage.list_runs_with_status(RunStatus.LIVE)
        for run in live_runs:
            await self._storage.set_run_status(run.run_id, RunStatus.DORMANT)

        for name in await self._compose.list_running(prefix="tontester-prom-"):
            logger.info(f"Recovery: stopping leftover container {name}")
            await self._compose.down(name)

        all_runs = await self._storage.list_runs(limit=10_000)
        db_run_ids = {r.run_id for r in all_runs}
        for provisioned_run_id in self._provisioning.list_provisioned_runs():
            if provisioned_run_id not in db_run_ids:
                logger.warning(f"Removing orphan Grafana datasource for {provisioned_run_id}")
                self._provisioning.remove_run_datasource(provisioned_run_id)

        for run in all_runs:
            self._provisioning.write_run_datasource(run.run_id)

    async def start_reaper(self) -> None:
        if self._reaper_task is None:
            self._reaper_task = asyncio.create_task(self._reaper_loop(), name="runs-reaper")

    async def shutdown(self, *, reply_timeout: float | None = None) -> None:
        """Shut down all actors. Waits for graceful ``_Shutdown`` processing.

        If ``reply_timeout`` is set and any actor doesn't respond in that
        time, the remaining actor tasks are force-cancelled. The partial
        state that leaves behind (possibly half-transitioned runs, stale
        LIVE rows, orphan containers) is reconciled by :meth:`recover` on
        next boot — the same backstop as any abnormal termination.
        """
        if self._stopped:
            return
        self._stopped = True
        if self._reaper_task is not None:
            _ = self._reaper_task.cancel()
            try:
                await self._reaper_task
            except asyncio.CancelledError:
                pass
            self._reaper_task = None

        replies: list[asyncio.Future[None]] = []
        for actor in list(self._actors.values()):
            reply: asyncio.Future[None] = asyncio.get_running_loop().create_future()
            await actor.send(ShutdownMsg(reply=reply))
            replies.append(reply)
        if replies:
            gather = asyncio.gather(*replies, return_exceptions=True)
            if reply_timeout is None:
                _ = await gather
            else:
                try:
                    _ = await asyncio.wait_for(gather, timeout=reply_timeout)
                except asyncio.TimeoutError:
                    logger.warning(
                        (
                            f"Actor _Shutdown replies didn't complete in {reply_timeout}s; "
                            f"cancelling stuck actor tasks"
                        )
                    )
                    for task in self._actor_tasks.values():
                        if not task.done():
                            _ = task.cancel()
        if self._actor_tasks:
            _ = await asyncio.gather(*self._actor_tasks.values(), return_exceptions=True)

    # ---- introspection (tests)

    async def snapshot(self, run_id: str) -> RunSnapshot | None:
        """Full run config + runtime state — one trip to the actor.

        Used by the scrape proxy (node lookup), the query proxy
        (``end_time`` clamp for dormant runs), and tests (diagnostics).
        Returns ``None`` when the run isn't spawned *and* storage has no
        row — i.e. the run was never registered.
        """
        actor = self._get_or_spawn(run_id)
        reply: asyncio.Future[RunSnapshot | None] = (
            asyncio.get_running_loop().create_future()
        )
        await actor.send(ConfigSnapshotMsg(reply=reply))
        return await reply

    async def run_reaper_pass(self) -> None:
        """Broadcast ``_MaybeReap`` to every actor. Tests call this directly
        so they don't have to wait for the reaper loop's real-time sleep.
        """
        deadline = asyncio.get_running_loop().time() - self.archive_idle_ttl_seconds
        replies: list[asyncio.Future[None]] = []
        for actor in list(self._actors.values()):
            reply: asyncio.Future[None] = asyncio.get_running_loop().create_future()
            await actor.send(MaybeReapMsg(deadline=deadline, reply=reply))
            replies.append(reply)
        if replies:
            _ = await asyncio.gather(*replies, return_exceptions=True)

    # ---- internals

    def _get_or_spawn(self, run_id: str) -> RunActor:
        existing = self._actors.get(run_id)
        if existing is not None:
            return existing
        actor = RunActor(
            run_id,
            instance_dir=self.instance_dir,
            storage=self._storage,
            compose=self._compose,
            provisioning=self._provisioning,
            ports=self._ports,
            daemon_port=self._daemon_port,
            daemon_base_url=self._daemon_base_url,
            archive_idle_ttl_seconds=self.archive_idle_ttl_seconds,
        )
        self._actors[run_id] = actor
        task = asyncio.create_task(actor.run(), name=f"run-{run_id}")
        # Defensive net: if the actor loop ever exits outside the _Shutdown
        # path, drop it from the registry so a subsequent register() spawns
        # a fresh actor instead of deadlocking on a dead inbox.
        task.add_done_callback(lambda t: self._on_actor_done(run_id, t))
        self._actor_tasks[run_id] = task
        return actor

    def _on_actor_done(self, run_id: str, task: asyncio.Task[None]) -> None:
        if self._stopped:
            return  # orderly shutdown — supervisor.shutdown owns cleanup
        exc = task.exception() if not task.cancelled() else None
        if exc is not None:
            logger.error(f"Actor for run {run_id} died unexpectedly", exc_info=exc)
        else:
            logger.error(f"Actor for run {run_id} exited without _Shutdown")
        _ = self._actors.pop(run_id, None)
        _ = self._actor_tasks.pop(run_id, None)

    async def _reaper_loop(self) -> None:
        while True:
            try:
                await asyncio.sleep(self.reap_interval_seconds)
            except asyncio.CancelledError:
                return
            try:
                await self.run_reaper_pass()
            except Exception:
                logger.exception("Reaper pass raised; continuing")
