import asyncio
import fcntl
import json
import logging
import os
import signal
from collections.abc import Awaitable
from pathlib import Path
from typing import cast, final

import uvicorn

from tl import JSONSerializable

from .api import create_app
from .compose import Compose
from .config import DaemonConfig
from .ipc import RunUrlResolver
from .runs import RunsManager
from .services import GrafanaProvisioning, grafana_container_name, grafana_service
from .sqlite_storage import SQLiteStorage

logger = logging.getLogger(__name__)


class DaemonAlreadyRunning(RuntimeError):
    pass


def acquire_instance_lock(instance_dir: Path) -> int:
    """Acquire an exclusive ``flock`` on ``<instance_dir>/daemon.lock``.

    Returns the file descriptor; caller must close it (and the OS releases
    the lock automatically when the fd is closed or the process dies). Any
    second daemon pointed at the same instance dir blocks here instead of
    racing the socket-bind, which is the only serialization point that
    survives ``systemd-run`` transient-unit churn.
    """
    instance_dir.mkdir(parents=True, exist_ok=True)
    lockfile = instance_dir / "daemon.lock"
    fd = os.open(str(lockfile), os.O_RDWR | os.O_CREAT, 0o644)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        os.close(fd)
        raise DaemonAlreadyRunning(
            f"Another daemon holds the lock at {lockfile}"
        )
    return fd


@final
class DashboardDaemon:
    def __init__(self, instance_dir: Path, socket_path: Path, frontend_dir: Path):
        self.instance_dir = instance_dir
        self.socket_path = socket_path
        self.frontend_dir = frontend_dir

        self._shutdown_event = asyncio.Event()
        self._storage = SQLiteStorage(instance_dir / "runs.db")
        self._compose = Compose(network="tontester")

    async def run(self) -> None:
        lock_fd = acquire_instance_lock(self.instance_dir)
        try:
            await self._run_locked()
        finally:
            os.close(lock_fd)

    async def _run_locked(self) -> None:
        config = _load_config(self.instance_dir / "config.json")

        dashboard_url = f"http://{config.host}:{config.dashboard_port}"
        grafana_url = f"http://{config.host}:{config.grafana_port}"

        provisioning = GrafanaProvisioning(
            provisioning_dir=self.instance_dir / "grafana" / "provisioning",
            daemon_host_url=dashboard_url,
        )
        provisioning.write_dashboard_provider()
        provisioning.write_dashboard()

        runs = RunsManager(
            instance_dir=self.instance_dir,
            storage=self._storage,
            compose=self._compose,
            provisioning=provisioning,
            port_range=config.prometheus_port_range,
        )

        await self._compose.ensure_network()
        await runs.recover()
        await runs.start_reaper()

        grafana_started = False
        try:
            await self._compose.up(
                grafana_service(
                    host_port=config.grafana_port,
                    data_dir=self.instance_dir / "grafana" / "data",
                    provisioning_dir=self.instance_dir / "grafana" / "provisioning",
                )
            )
            grafana_started = True
        except Exception:
            logger.warning("Could not start Grafana; dashboard queries will fail", exc_info=True)

        url_resolver = RunUrlResolver(dashboard_url=dashboard_url, grafana_url=grafana_url)
        app, state = create_app(
            storage=self._storage,
            runs=runs,
            url_resolver=url_resolver,
            frontend_dir=str(self.frontend_dir),
            on_shutdown_request=self._shutdown_event.set,
        )

        self.socket_path.parent.mkdir(parents=True, exist_ok=True)
        if self.socket_path.exists() or self.socket_path.is_symlink():
            self.socket_path.unlink()

        tcp_config = uvicorn.Config(
            app,
            host=config.host,
            port=config.dashboard_port,
            log_level="info",
            lifespan="off",
        )
        uds_config = uvicorn.Config(
            app,
            uds=str(self.socket_path),
            log_level="info",
            lifespan="off",
        )
        tcp_server = uvicorn.Server(tcp_config)
        uds_server = uvicorn.Server(uds_config)

        tcp_task = asyncio.create_task(tcp_server.serve(), name="tcp-server")
        uds_task = asyncio.create_task(uds_server.serve(), name="uds-server")

        loop = asyncio.get_running_loop()
        loop.add_signal_handler(signal.SIGTERM, self._shutdown_event.set)
        loop.add_signal_handler(signal.SIGINT, self._shutdown_event.set)
        # SIGHUP arrives when our foreground systemd-run wrapper loses its
        # PTY (terminal closed). Treat the same as SIGTERM.
        loop.add_signal_handler(signal.SIGHUP, self._shutdown_event.set)

        logger.info(f"Dashboard: {dashboard_url}")
        logger.info(f"Grafana:   {grafana_url}")
        logger.info(f"IPC:       {self.socket_path}")

        try:
            _ = await self._shutdown_event.wait()
        finally:
            logger.info("Shutting down...")

            tcp_server.should_exit = True
            uds_server.should_exit = True
            try:
                _ = await asyncio.wait_for(
                    asyncio.gather(tcp_task, uds_task, return_exceptions=True),
                    timeout=30.0,
                )
            except asyncio.TimeoutError:
                logger.warning("uvicorn servers did not stop in time; cancelling")
                _ = tcp_task.cancel()
                _ = uds_task.cancel()
                _ = await asyncio.gather(tcp_task, uds_task, return_exceptions=True)

            await state.proxy.aclose()

            # runs.shutdown and Grafana teardown are independent; one failing
            # must not skip the other.
            teardown: list[Awaitable[object]] = [runs.shutdown()]
            if grafana_started:
                teardown.append(self._compose.down(grafana_container_name()))
            for result in await asyncio.gather(*teardown, return_exceptions=True):
                if isinstance(result, BaseException):
                    logger.exception("Teardown step failed", exc_info=result)

            if self.socket_path.exists():
                self.socket_path.unlink()

            self._storage.close()


def _load_config(path: Path) -> DaemonConfig:
    if not path.exists():
        raise RuntimeError(f"No configuration at {path}")
    raw = cast(JSONSerializable, json.loads(path.read_text()))
    if not isinstance(raw, dict):
        raise RuntimeError(f"Configuration at {path} is not a JSON object")
    return DaemonConfig.model_validate(raw)
