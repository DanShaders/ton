"""Per-run Prometheus lifecycle — a ws/archive state machine.

Every live container lives in a single :attr:`RunsManager._containers`
dict; ``owner`` (``ws`` vs ``archive``) distinguishes a WS-held run from
an opportunistic lazy-boot, and the DB's ``LIVE`` status is a shadow of
``owner == "ws"``.
"""

import asyncio
import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Literal, final

from .compose import ComposeLike
from .services import (
    GrafanaProvisioning,
    ScrapeTarget,
    prometheus_container_name,
    prometheus_service,
    write_prometheus_config,
)
from .storage import RunMetadata, RunStatus, StorageBackend, TestMetadata

logger = logging.getLogger(__name__)


type ContainerOwner = Literal["ws", "archive"]


@dataclass
class _ContainerHandle:
    run_id: str
    host_port: int
    owner: ContainerOwner
    last_access: float
    starting: asyncio.Event  # set once the container is ready
    failed: bool = False
    # Number of in-flight queries currently using this container. The reaper
    # refuses to stop a handle with ``pins > 0`` so an idle-reap can't race
    # a proxied request that's still streaming from the container.
    pins: int = 0


@final
class RunsManager:
    def __init__(
        self,
        *,
        instance_dir: Path,
        storage: StorageBackend,
        compose: ComposeLike,
        provisioning: GrafanaProvisioning,
        port_range: tuple[int, int],
        archive_idle_ttl_seconds: float = 300.0,
        reap_interval_seconds: float = 30.0,
    ):
        self.instance_dir = instance_dir
        self.storage = storage
        self.compose = compose
        self.provisioning = provisioning
        self.port_range = port_range
        self.archive_idle_ttl_seconds = archive_idle_ttl_seconds
        self.reap_interval_seconds = reap_interval_seconds

        self._lock = asyncio.Lock()
        self._containers: dict[str, _ContainerHandle] = {}
        self._reaper_task: asyncio.Task[None] | None = None
        self._stopped = False
        # Strong refs to fire-and-forget archive-boot tasks: asyncio only
        # holds weak refs, so a lost task can be GC'd mid-flight.
        self._boot_tasks: set[asyncio.Task[None]] = set()

    def run_dir(self, run_id: str) -> Path:
        return self.instance_dir / "runs" / run_id

    def data_dir(self, run_id: str) -> Path:
        return self.run_dir(run_id) / "data"

    def config_dir(self, run_id: str) -> Path:
        return self.run_dir(run_id) / "config"

    async def start_reaper(self) -> None:
        if self._reaper_task is None:
            self._reaper_task = asyncio.create_task(self._reap_idle_archives())

    async def stop_reaper(self) -> None:
        if self._reaper_task is not None:
            _ = self._reaper_task.cancel()
            try:
                await self._reaper_task
            except asyncio.CancelledError:
                pass
            self._reaper_task = None

    async def recover(self) -> None:
        """Hard reset on startup: DORMANT all LIVE rows, stop survivors."""
        live_runs = await self.storage.list_runs_with_status(RunStatus.LIVE)
        for run in live_runs:
            await self.storage.set_run_status(run.run_id, RunStatus.DORMANT)

        for name in await self.compose.list_running(prefix="tontester-prom-"):
            logger.info(f"Recovery: stopping leftover container {name}")
            await self.compose.down(name)

        # Grafana datasource YAMLs that don't correspond to any DB run are
        # stale (e.g. a purged run) — reap them.
        all_runs = await self.storage.list_runs(limit=10_000)
        db_run_ids = {r.run_id for r in all_runs}
        for provisioned_run_id in self.provisioning.list_provisioned_runs():
            if provisioned_run_id not in db_run_ids:
                logger.warning(f"Removing orphan Grafana datasource for run {provisioned_run_id}")
                self.provisioning.remove_run_datasource(provisioned_run_id)

        # And any run in the DB that lacks a datasource gets one (e.g. the YAML
        # was hand-deleted, or a prior version of the daemon didn't write it).
        for run in all_runs:
            self.provisioning.write_run_datasource(run.run_id)

    async def register(self, run_id: str, metadata: TestMetadata) -> RunMetadata:
        """Idempotent: ensure ``run_id`` is ``LIVE`` and owned by this caller.

        Handles fresh runs, reconnects after a daemon crash, and reconnects
        after a WS flap — the same code path for all three.
        """
        async with self._lock:
            if self._stopped:
                raise DaemonStopped
            existing = self._containers.get(run_id)
            if existing is not None and existing.owner == "ws":
                raise RunAlreadyActive(run_id)
            if existing is not None and existing.owner == "archive":
                await self._stop_container(run_id)

            host_port = await self._pick_port(run_id)
            await self.storage.register_run(run_id, metadata, host_port)

            config_dir = self.config_dir(run_id)
            data_dir = self.data_dir(run_id)
            config_dir.mkdir(parents=True, exist_ok=True)
            data_dir.mkdir(parents=True, exist_ok=True)
            write_prometheus_config(
                config_dir,
                [
                    ScrapeTarget(targets=[node.address], labels={"node": node.name})
                    for node in metadata.nodes
                ],
            )
            self.provisioning.write_run_datasource(run_id)

            handle = _ContainerHandle(
                run_id=run_id,
                host_port=host_port,
                owner="ws",
                last_access=asyncio.get_running_loop().time(),
                starting=asyncio.Event(),
            )
            self._containers[run_id] = handle

        try:
            await self._start_container(run_id, host_port)
            handle.starting.set()
        except Exception:
            async with self._lock:
                handle.failed = True
                handle.starting.set()
                _ = self._containers.pop(run_id, None)
            # CAS on port: only flip DB to DORMANT if the row is still the
            # one we wrote. A concurrent successful register would have
            # bumped the port, leaving our rollback a no-op — which is
            # exactly what we want (their LIVE row stays intact).
            await self.storage.set_run_status(run_id, RunStatus.DORMANT, if_port=host_port)
            raise RunStartFailed(run_id)

        result = await self.storage.get_run_metadata(run_id)
        assert result is not None
        return result

    async def release(self, run_id: str) -> None:
        """WebSocket closed (clean or dirty): drop to ``DORMANT``."""
        async with self._lock:
            handle = self._containers.get(run_id)
            if handle is None or handle.owner != "ws":
                return
            await self._stop_container(run_id)
        await self.storage.set_run_status(run_id, RunStatus.DORMANT)
        logger.info(f"Run {run_id} -> dormant")

    async def ensure_queryable(self, run_id: str) -> int | None:
        """Return the host port currently serving this run.

        - In-memory container already up (ws or archive): return its port.
        - Run known to the DB but no container: lazy-boot (archive-owned).
        - Unknown run: ``None``.
        """
        async with self._lock:
            if self._stopped:
                return None
            handle = self._containers.get(run_id)
            if handle is not None:
                handle.last_access = asyncio.get_running_loop().time()
                starting = handle.starting
                port = handle.host_port
            else:
                run = await self.storage.get_run_metadata(run_id)
                if run is None:
                    return None
                host_port = await self._pick_port(run_id)
                handle = _ContainerHandle(
                    run_id=run_id,
                    host_port=host_port,
                    owner="archive",
                    last_access=asyncio.get_running_loop().time(),
                    starting=asyncio.Event(),
                )
                self._containers[run_id] = handle
                starting = handle.starting
                port = host_port
                # Ensure config_dir has at least an empty targets file;
                # Prometheus refuses to start without prometheus.yml.
                write_prometheus_config(self.config_dir(run_id), [])
                task = asyncio.create_task(self._boot_archive(run_id, host_port))
                self._boot_tasks.add(task)
                task.add_done_callback(self._boot_tasks.discard)

        _ = await starting.wait()
        current = self._containers.get(run_id)
        if current is None or current.failed:
            return None
        return port

    async def acquire_query_pin(self, run_id: str) -> int | None:
        """Ensure the container is up and mark it in-use.

        Callers MUST pair every successful return with ``release_query_pin``
        once the query is fully drained — otherwise the reaper will never
        stop the container.
        """
        port = await self.ensure_queryable(run_id)
        if port is None:
            return None
        async with self._lock:
            handle = self._containers.get(run_id)
            if handle is None:
                return None
            handle.pins += 1
        return port

    async def release_query_pin(self, run_id: str) -> None:
        async with self._lock:
            handle = self._containers.get(run_id)
            if handle is not None and handle.pins > 0:
                handle.pins -= 1

    async def shutdown(self) -> None:
        async with self._lock:
            self._stopped = True
        await self.stop_reaper()

        # Wait for in-flight boots: otherwise our ``podman stop`` can precede
        # ``compose.up``, orphaning the container until next recovery. On
        # timeout, drop the handle — the still-running boot becomes an
        # orphan that ``recover()`` will clean up, but we do NOT issue a
        # racy ``down`` against a container that doesn't exist yet.
        for handle in list(self._containers.values()):
            try:
                _ = await asyncio.wait_for(handle.starting.wait(), timeout=10.0)
            except asyncio.TimeoutError:
                logger.warning(f"Boot of run {handle.run_id} exceeded 10s; leaving for recovery")
                async with self._lock:
                    current = self._containers.get(handle.run_id)
                    if current is handle:
                        _ = self._containers.pop(handle.run_id, None)

        for run_id in list(self._containers.keys()):
            handle = self._containers.get(run_id)
            if handle is None:
                continue
            try:
                async with self._lock:
                    await self._stop_container(run_id)
                if handle.owner == "ws":
                    await self.storage.set_run_status(run_id, RunStatus.DORMANT)
            except Exception:
                logger.exception(f"Error stopping container for run {run_id} during shutdown")

    async def _start_container(self, run_id: str, host_port: int) -> None:
        service = prometheus_service(
            run_id=run_id,
            host_port=host_port,
            config_dir=self.config_dir(run_id),
            data_dir=self.data_dir(run_id),
        )
        await self.compose.up(service)

    async def _boot_archive(self, run_id: str, host_port: int) -> None:
        handle = self._containers.get(run_id)
        if handle is None:
            return
        try:
            await self._start_container(run_id, host_port)
            logger.info(f"Lazy-booted archive container for run {run_id} on port {host_port}")
        except Exception:
            logger.exception(f"Failed to lazy-boot archive for run {run_id}")
            handle.failed = True
            # Drop the failed handle so a subsequent query re-attempts the
            # boot instead of getting ``None`` forever.
            async with self._lock:
                current = self._containers.get(run_id)
                if current is handle:
                    _ = self._containers.pop(run_id, None)
        finally:
            handle.starting.set()

    async def _stop_container(self, run_id: str) -> None:
        # Callers must hold ``self._lock``.
        handle = self._containers.pop(run_id, None)
        if handle is None:
            return
        await self.compose.down(prometheus_container_name(run_id))

    async def _reap_idle_archives(self) -> None:
        while True:
            try:
                await asyncio.sleep(self.reap_interval_seconds)
            except asyncio.CancelledError:
                return
            try:
                await self.run_reaper_pass()
            except Exception:
                # ``run_reaper_pass`` has its own per-candidate except, but a
                # bug in the scan loop itself must not kill the reaper task
                # silently (no one awaits it).
                logger.exception("Reaper pass raised; continuing")

    async def run_reaper_pass(self) -> None:
        """Stop any archive-owned container that has been idle past the TTL.

        Factored out of the reaper loop so tests can drive it directly without
        waiting on real-time sleeps.
        """
        now = asyncio.get_running_loop().time()
        candidates: list[str] = []
        for run_id, handle in self._containers.items():
            if handle.owner != "archive":
                continue
            if not handle.starting.is_set():
                continue
            if now - handle.last_access >= self.archive_idle_ttl_seconds:
                candidates.append(run_id)
        for run_id in candidates:
            # Re-check under the lock: a racing ``register()`` may have
            # promoted this handle to ``ws``, or a new query may have
            # pinned it — never reap a live or in-use run.
            try:
                async with self._lock:
                    handle = self._containers.get(run_id)
                    if handle is None or handle.owner != "archive":
                        continue
                    if now - handle.last_access < self.archive_idle_ttl_seconds:
                        continue
                    if handle.pins > 0:
                        continue
                    logger.info(f"Reaping idle archive container for run {run_id}")
                    await self._stop_container(run_id)
            except Exception:
                logger.exception(f"Error reaping archive container {run_id}")

    async def _pick_port(self, run_id: str) -> int:
        """Pick a free port, preferring the one previously used by this run."""
        in_use = {h.host_port for h in self._containers.values()}
        in_use |= await self.storage.list_allocated_ports()

        existing = await self.storage.get_run_metadata(run_id)
        if existing is not None and existing.host_port not in in_use:
            return existing.host_port

        for port in range(self.port_range[0], self.port_range[1] + 1):
            if port not in in_use:
                return port
        raise NoFreePorts(self.port_range)


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
