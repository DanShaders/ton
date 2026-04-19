"""Per-run lifecycle state machine — one actor coroutine per run.

Each :class:`RunActor`:

- owns the lifecycle state machine for one run (:class:`Dormant` |
  :class:`Live`)
- processes messages from its inbox in order
- is the only coroutine that calls ``compose.up`` / ``compose.down`` /
  DB writes for its run_id

The supervisor's routing and cross-run concerns live in :mod:`.runs`.
This file is self-contained except for the message types and state
primitives that the supervisor needs to construct and observe.

Exception-safety contract
-------------------------

*Action* handlers (``RegisterMsg``, ``AcquirePinMsg``) provide strong
exception safety: either the operation commits in full or no observable
state — in-memory, SQLite, filesystem, podman — has changed and the
caller's reply resolves with the originating exception. Rollback is done
via :class:`contextlib.AsyncExitStack`, which walks registered undo
callbacks in reverse; on success we ``pop_all()`` the stack (implicitly,
by never entering it as a context manager) so the callbacks become the
payload of :attr:`Live.resources` instead.

**Noexcept-release contract** for ``Live.resources``. Callbacks
pushed to a run's resource stack must not raise — analogous to the
"destructor must not throw" convention in C++ or ``Drop::drop`` must not
panic in Rust. If the underlying operation can fail (e.g. a SQLite
write), wrap it in a local ``async def`` that swallows the exception
and logs. The payoff is that ``await resources.aclose()`` is
unconditionally safe: :meth:`RunActor._release_live` can commit the
transition to :class:`Dormant` without a try/except, and register-rollback
doesn't need a two-stack split to keep FS-undo steps from being skipped
by a raising release. Today ``compose.down`` is already no-throw by
construction; ``ports.release`` can't raise in practice; only
``set_run_status`` needs an explicit wrapper at push site.

*Cleanup* handlers (``ReleaseMsg``, ``MaybeReapMsg``, ``ShutdownMsg``) are
best-effort. ``ShutdownMsg`` in particular swallows every failure — there
is nothing useful a caller can do with a shutdown error. Correctness on
abnormal termination comes from three layers outside this file:

1. ``systemd-run --user`` + ``KillMode=control-group`` (see ``cli.py``):
   if the daemon's cgroup dies, podman children die with it.
2. ``podman run --rm`` (see ``compose.Compose.up``): containers are
   removed on exit, so an abnormal death never leaves zombie containers.
3. :meth:`RunsSupervisor.recover` at startup reconciles filesystem,
   SQLite, and podman against each other.

Persistent writes are ordered **filesystem → SQLite → podman**. The
ordering matters because :meth:`RunsSupervisor.recover` pivots on it:
a ``LIVE`` row means filesystem provisioning happened (but may be out
of date); a running container means both filesystem and SQLite writes
completed. New handlers MUST preserve this order.

The reply invariant: every message's reply future is completed exactly
once before the handler returns. The actor loop enforces this by
centralising ``reply.set_result`` / ``reply.set_exception`` in
:meth:`RunActor._dispatch`; handlers just return or raise. Actor tasks
are never externally cancelled in the default shutdown path — the
supervisor asks each actor to stop via ``ShutdownMsg`` — so handlers need
no ``CancelledError`` handling. The one violation is :meth:`RunsSupervisor.shutdown`
with ``reply_timeout`` set (escalated shutdown), which force-cancels
stuck actors. In that case partial state is accepted; recover() fixes
it on next boot.

Cancel zone
-----------

This module is the **actor zone**: code under :meth:`RunActor.run` and
its handlers runs to completion and may be written as straight-line code.
The boundary into this zone is the actor's inbox; callers in the uvicorn
zone (:mod:`.ipc`, :mod:`.prom_proxy`) cross it via
``await actor.send(msg)`` followed by ``await reply``. The send step is
atomic — ``asyncio.Queue.put`` on an unbounded queue does not yield —
so once a message is enqueued, the actor processes it regardless of the
caller's cancellation.
"""

import asyncio
import contextlib
import logging
from collections.abc import Coroutine
from dataclasses import dataclass
from pathlib import Path
from typing import final

import httpx

from .models import (
    DaemonStopped,
    NoFreePorts,
    RunAlreadyActive,
    RunMetadata,
    RunOwner,
    RunStartFailed,
    RunStateSnapshot,
    RunStatus,
    TestMetadata,
)
from .protocols import ComposeLike, ProvisioningLike, StorageBackend
from .services import (
    prometheus_container_name,
    prometheus_service,
    remove_prometheus_config,
    write_prometheus_config,
)

logger = logging.getLogger(__name__)


# ==================================================================== actor state


@dataclass(frozen=True)
class Dormant:
    """No external resources held. The run may or may not exist in the DB."""


@dataclass(eq=False)
class Live:
    """A run with external resources held by :attr:`resources`.

    The stack owns — in reverse of acquisition order — the port allocation,
    the podman container, and (for ws owner) the ``set_run_status(DORMANT)``
    write that settles the DB row. Closing the stack releases them all in
    one step. Filesystem artifacts (prom config, grafana datasource) are
    intentionally NOT in the stack: they persist across dormant periods so
    the run remains queryable via archive lazy-boot.
    """

    owner: RunOwner
    host_port: int
    metadata: TestMetadata
    pins: int
    last_access: float
    resources: contextlib.AsyncExitStack


type RunState = Dormant | Live


# ====================================================================== messages


@dataclass(frozen=True)
class RegisterMsg:
    metadata: TestMetadata
    reply: asyncio.Future[RunMetadata]


@dataclass(frozen=True)
class ReleaseMsg:
    reply: asyncio.Future[None]


@dataclass(frozen=True)
class AcquirePinMsg:
    reply: asyncio.Future[int | None]


@dataclass(frozen=True)
class ReleasePinMsg:
    reply: asyncio.Future[None]


@dataclass(frozen=True)
class MaybeReapMsg:
    deadline: float
    reply: asyncio.Future[None]


@dataclass(frozen=True)
class ShutdownMsg:
    reply: asyncio.Future[None]


@dataclass(frozen=True)
class InspectMsg:
    reply: asyncio.Future[RunStateSnapshot]


@dataclass(frozen=True)
class ResolveNodeMsg:
    """Ask the actor for the configured address of a named node.

    Used by the scrape proxy on the hot path (every ~5 s per node). The
    actor is the source of truth for the run's current metadata; going
    through it avoids a SQLite hit per scrape and guarantees consistency
    with the in-memory state the rest of the actor's handlers see.
    """

    node_name: str
    reply: asyncio.Future[str | None]


type Msg = (
    RegisterMsg
    | ReleaseMsg
    | AcquirePinMsg
    | ReleasePinMsg
    | MaybeReapMsg
    | ShutdownMsg
    | InspectMsg
    | ResolveNodeMsg
)


# =============================================================== port allocator


@final
class PortAllocator:
    """In-memory host-port allocator.

    Called synchronously from actors; since every actor and this allocator
    run on the same event loop in a single thread, there's no interleaving
    without an ``await``, so no lock is needed.
    """

    def __init__(self, range_: tuple[int, int]):
        self._range = range_
        self._allocated: dict[str, int] = {}

    def allocate(self, run_id: str, preferred: int | None = None) -> int:
        used = set(self._allocated.values())
        if preferred is not None and preferred not in used:
            self._allocated[run_id] = preferred
            return preferred
        for port in range(self._range[0], self._range[1] + 1):
            if port not in used:
                self._allocated[run_id] = port
                return port
        raise NoFreePorts(self._range)

    def release(self, run_id: str) -> None:
        _ = self._allocated.pop(run_id, None)

    def allocated_ports(self) -> set[int]:
        return set(self._allocated.values())


# ===================================================================== run actor


@final
class RunActor:
    """One coroutine, one run, one mutable state.

    The only public methods are :meth:`send` (drop a message in the inbox)
    and :meth:`run` (the coroutine body the supervisor awaits). All
    ``_on_*`` handlers run serially in :meth:`run` — no concurrent access
    to ``self.*`` fields is possible.
    """

    def __init__(
        self,
        run_id: str,
        *,
        instance_dir: Path,
        storage: StorageBackend,
        compose: ComposeLike,
        provisioning: ProvisioningLike,
        ports: PortAllocator,
        daemon_port: int,
        daemon_base_url: str,
        archive_idle_ttl_seconds: float,
    ):
        self.run_id = run_id
        self._instance_dir = instance_dir
        self._storage = storage
        self._compose = compose
        self._provisioning = provisioning
        self._ports = ports
        self._daemon_port = daemon_port
        self._daemon_base_url = daemon_base_url
        self._ttl = archive_idle_ttl_seconds

        self._state: RunState = Dormant()
        self._shutting_down: bool = False

        self._inbox: asyncio.Queue[Msg] = asyncio.Queue()

    # ---- public plumbing

    async def send(self, msg: Msg) -> None:
        await self._inbox.put(msg)

    async def run(self) -> None:
        while True:
            msg = await self._inbox.get()
            match msg:
                case RegisterMsg(metadata, reply):
                    await self._dispatch(reply, self._on_register(metadata))
                case ReleaseMsg(reply):
                    await self._dispatch(reply, self._on_release())
                case AcquirePinMsg(reply):
                    await self._dispatch(reply, self._on_acquire_pin())
                case ReleasePinMsg(reply):
                    self._on_release_pin()
                    if not reply.done():
                        reply.set_result(None)
                case MaybeReapMsg(deadline, reply):
                    await self._dispatch(reply, self._on_maybe_reap(deadline))
                case InspectMsg(reply):
                    if not reply.done():
                        reply.set_result(self._snapshot())
                case ResolveNodeMsg(node_name, reply):
                    if not reply.done():
                        reply.set_result(self._resolve_node(node_name))
                case ShutdownMsg(reply):
                    await self._dispatch(reply, self._on_shutdown())
                    return

    async def _dispatch[T](
        self,
        reply: asyncio.Future[T],
        coro: Coroutine[object, object, T],
    ) -> None:
        try:
            result = await coro
        except Exception as e:
            logger.exception(f"Run {self.run_id}: handler raised")
            if not reply.done():
                reply.set_exception(e)
        else:
            if not reply.done():
                reply.set_result(result)

    # ---- handlers

    async def _on_register(self, metadata: TestMetadata) -> RunMetadata:
        if self._shutting_down:
            raise DaemonStopped()
        if isinstance(self._state, Live):
            if self._state.owner == "ws":
                raise RunAlreadyActive(self.run_id)
            # Archive lease; drop it so resources return to the pool.
            await self._release_live()

        host_port = self._ports.allocate(self.run_id)

        resources = contextlib.AsyncExitStack()
        _ = resources.callback(self._ports.release, self.run_id)

        config_dir = self._config_dir()
        try:
            # Filesystem — persists across dormant periods, so not in the
            # resources stack. Cleaned up only on register failure; both
            # removes are idempotent (unlink with missing_ok).
            config_dir.mkdir(parents=True, exist_ok=True)
            self._data_dir().mkdir(parents=True, exist_ok=True)
            write_prometheus_config(
                config_dir,
                run_id=self.run_id,
                daemon_port=self._daemon_port,
                node_names=[n.name for n in metadata.nodes],
            )
            self._provisioning.write_run_datasource(self.run_id)
            # Best-effort nudge so the user doesn't see "Unknown datasource"
            # for the ~10 s until Grafana's file-poller picks the new yaml up.
            try:
                await self._provisioning.reload_datasources()
            except Exception:
                logger.exception(f"Grafana reload after register({self.run_id}) raised")

            # SQLite.
            await self._storage.register_run(self.run_id, metadata, host_port)

            async def _release_set_dormant() -> None:
                # Wrapped: callbacks on Live.resources must not raise.
                try:
                    await self._storage.set_run_status(
                        self.run_id, RunStatus.DORMANT, if_port=host_port
                    )
                except Exception:
                    logger.exception(f"Release: set_run_status(DORMANT) raised for {self.run_id}")

            _ = resources.push_async_callback(_release_set_dormant)

            # Podman. ``compose.down`` is no-throw by construction (swallows
            # PodmanTimeout internally), so it satisfies the Live.resources
            # contract without a wrapper.
            try:
                await self._start_container(host_port, scraping=True)
            except Exception as e:
                raise RunStartFailed(self.run_id) from e
            _ = resources.push_async_callback(
                self._compose.down, prometheus_container_name(self.run_id)
            )

            row = await self._storage.get_run_metadata(self.run_id)
            assert row is not None
        except Exception:
            # ``resources.aclose()`` is noexcept by contract: every pushed
            # callback either can't raise (ports.release, compose.down) or
            # is wrapped at push site (set_run_status). The FS removes are
            # rollback-only, so they stay out of the stack; each is wrapped
            # here so a failure doesn't shadow the triggering exception.
            await resources.aclose()
            try:
                remove_prometheus_config(config_dir)
            except Exception:
                logger.exception(
                    f"Register rollback: remove_prometheus_config raised for {self.run_id}"
                )
            try:
                self._provisioning.remove_run_datasource(self.run_id)
            except Exception:
                logger.exception(
                    f"Register rollback: remove_run_datasource raised for {self.run_id}"
                )
            try:
                await self._provisioning.reload_datasources()
            except Exception:
                logger.exception(f"Register rollback: reload_datasources raised for {self.run_id}")
            raise

        self._state = Live(
            owner="ws",
            host_port=host_port,
            metadata=metadata,
            pins=0,
            last_access=asyncio.get_running_loop().time(),
            resources=resources,
        )
        return row

    async def _on_release(self) -> None:
        match self._state:
            case Live(owner="ws"):
                await self._release_live()
                logger.info(f"Run {self.run_id} -> dormant")
            case _:
                return

    async def _on_acquire_pin(self) -> int | None:
        if self._shutting_down:
            return None

        if isinstance(self._state, Dormant):
            row = await self._storage.get_run_metadata(self.run_id)
            if row is None:
                return None
            if not await self._lazy_boot_archive(row):
                return None

        assert isinstance(self._state, Live)
        self._state.pins += 1
        self._state.last_access = asyncio.get_running_loop().time()
        return self._state.host_port

    async def _lazy_boot_archive(self, row: RunMetadata) -> bool:
        """Bring a dormant run up as ``archive`` owner. On success, transitions
        ``self._state`` to ``Live`` and returns ``True``. On any failure, fully
        rolls back and returns ``False``."""
        try:
            host_port = self._ports.allocate(self.run_id, preferred=row.host_port)
        except NoFreePorts:
            return False

        resources = contextlib.AsyncExitStack()
        _ = resources.callback(self._ports.release, self.run_id)

        config_dir = self._config_dir()

        def _rollback_fs() -> None:
            try:
                remove_prometheus_config(config_dir)
            except Exception:
                logger.exception(
                    f"Lazy-boot rollback: remove_prometheus_config raised for {self.run_id}"
                )

        try:
            config_dir.mkdir(parents=True, exist_ok=True)
            write_prometheus_config(
                config_dir,
                run_id=self.run_id,
                daemon_port=self._daemon_port,
                node_names=[],
            )

            try:
                # Archive mode: no live scraping → no host egress at all.
                await self._start_container(host_port, scraping=False)
            except Exception:
                logger.exception(f"Run {self.run_id}: archive lazy-boot failed")
                await resources.aclose()  # noexcept by Live.resources contract
                _rollback_fs()
                return False
            _ = resources.push_async_callback(
                self._compose.down, prometheus_container_name(self.run_id)
            )
        except Exception:
            await resources.aclose()
            _rollback_fs()
            raise

        self._state = Live(
            owner="archive",
            host_port=host_port,
            metadata=row.metadata,
            pins=0,
            last_access=asyncio.get_running_loop().time(),
            resources=resources,
        )
        logger.info(f"Lazy-booted archive container for run {self.run_id}")
        return True

    def _on_release_pin(self) -> None:
        if isinstance(self._state, Live) and self._state.pins > 0:
            self._state.pins -= 1

    async def _on_maybe_reap(self, deadline: float) -> None:
        match self._state:
            case Live(owner="archive", pins=0, last_access=la) if la <= deadline:
                logger.info(f"Reaping idle archive container for run {self.run_id}")
                await self._release_live()
            case _:
                return

    async def _on_shutdown(self) -> None:
        """Best-effort teardown. Every step is swallowed: the daemon is going
        away, and any residual container/DB state is reconciled on next boot
        by :meth:`RunsSupervisor.recover` (or killed by the systemd cgroup
        if the exit is abnormal). Propagating an error here would only hang
        ``supervisor.shutdown``'s ``gather(*replies)``."""
        self._shutting_down = True
        if isinstance(self._state, Dormant):
            return
        try:
            await self._release_live()
        except Exception:
            logger.exception(f"Run {self.run_id}: release raised during shutdown")

    # ---- internals

    async def _release_live(self) -> None:
        """Close the currently held resources and transition to ``Dormant``.

        Callers must not await ``self._state.resources.aclose()`` directly:
        this helper enforces the state transition alongside the release, so
        :attr:`_state` can never observe ``Live`` with a closed stack.
        """
        if isinstance(self._state, Live):
            await self._state.resources.aclose()
        self._state = Dormant()

    async def _start_container(self, host_port: int, *, scraping: bool) -> None:
        await self._compose.up(
            prometheus_service(
                run_id=self.run_id,
                host_port=host_port,
                daemon_port=self._daemon_port,
                daemon_base_url=self._daemon_base_url,
                scraping=scraping,
                config_dir=self._config_dir(),
                data_dir=self._data_dir(),
            )
        )

    def _config_dir(self) -> Path:
        return self._instance_dir / "runs" / self.run_id / "config"

    def _data_dir(self) -> Path:
        return self._instance_dir / "runs" / self.run_id / "data"

    def _resolve_node(self, node_name: str) -> str | None:
        """In-memory name → address lookup for the scrape proxy. Returns
        ``None`` if the actor isn't Live or has no such node."""
        if not isinstance(self._state, Live):
            return None
        for n in self._state.metadata.nodes:
            if n.name == node_name:
                return n.address
        return None

    def _snapshot(self) -> RunStateSnapshot:
        match self._state:
            case Dormant():
                return RunStateSnapshot(
                    run_id=self.run_id,
                    status="dormant",
                    owner=None,
                    host_port=None,
                    pins=0,
                    last_access=0.0,
                )
            case Live() as live:
                return RunStateSnapshot(
                    run_id=self.run_id,
                    status="live",
                    owner=live.owner,
                    host_port=live.host_port,
                    pins=live.pins,
                    last_access=live.last_access,
                )
