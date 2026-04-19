"""Top-level daemon orchestration.

Lifetime model
--------------

Every resource the daemon owns is managed by a context manager composed
under a single :class:`contextlib.AsyncExitStack` in :meth:`DashboardDaemon.run`.
Resources unwind in reverse acquisition order on any exit — graceful
shutdown, startup failure, or an inner task death — without bespoke
try/finally nesting. The layers are:

1. ``_instance_lock`` — the per-instance flock (sync).
2. ``_storage_open`` — SQLite connection (sync).
3. ``_runs_lifecycle`` — supervisor reaper + orderly shutdown.
4. ``grafana_running`` — daemon-scope Grafana container.
5. ``_socket_path`` — stale unix-socket unlink on entry + on exit.
6. ``_signal_handlers`` — SIGTERM/SIGINT/SIGHUP → shutdown event.
7. ``proxy.aclose`` — httpx client for the Prometheus proxy.
8. ``_uvicorn_serving`` — TCP + UDS serve tasks, drained on exit.

Shutdown follows the conventional ``SIGINT → SIGTERM → SIGKILL``
escalation chain — the caller picks the signal that reflects intent:

* **SIGINT** (or **SIGHUP**): ``ShutdownRequest.requested`` fires;
  uvicorn drains, actors finish their in-flight handlers, the stack
  unwinds normally. A 2nd SIGINT escalates to force (see below).
* **SIGTERM**: ``ShutdownRequest.force`` fires immediately. The signal
  handlers are removed at this point. ``_uvicorn_serving`` short-circuits
  its drain and cancels the serve tasks directly. ``_runs_lifecycle``
  passes ``reply_timeout=0`` into :meth:`RunsSupervisor.shutdown`, which
  force-cancels any actor still executing a handler.
* **Second interactive signal (SIGINT/SIGHUP)**: same force path as
  SIGTERM.
* **Post-escalation signal**: hits Python's default handler —
  ``KeyboardInterrupt`` for SIGINT, process terminates for SIGTERM.

Abnormal termination (SIGKILL, OOM) is the backstop: ``systemd-run --user``
with ``KillMode=control-group`` takes the whole daemon + podman children
down together, and ``podman run --rm`` removes containers.
:meth:`RunsSupervisor.recover` reconciles any surviving state on next boot.
"""

import asyncio
import contextlib
import fcntl
import json
import logging
import os
import signal
from collections.abc import AsyncGenerator, Generator, Sequence
from contextlib import asynccontextmanager, contextmanager
from pathlib import Path
from typing import cast, final

import uvicorn

from tl import JSONSerializable

from .api import create_app
from .compose import Compose
from .config import DaemonConfig
from .ipc import RunUrlResolver
from .runs import RunsSupervisor
from .services import GrafanaProvisioning, grafana_running
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
        raise DaemonAlreadyRunning(f"Another daemon holds the lock at {lockfile}")
    return fd


@contextmanager
def _instance_lock(instance_dir: Path) -> Generator[int]:
    fd = acquire_instance_lock(instance_dir)
    try:
        yield fd
    finally:
        os.close(fd)


@contextmanager
def _storage_open(db_path: Path) -> Generator[SQLiteStorage]:
    storage = SQLiteStorage(db_path)
    try:
        yield storage
    finally:
        storage.close()


@contextmanager
def _socket_path(path: Path) -> Generator[None]:
    """Clean stale socket on entry; unlink on exit.

    Any daemon that exited without unlinking its socket (OOM, SIGKILL)
    leaves the inode behind; a subsequent ``bind()`` would fail with
    ``EADDRINUSE``. The flock has already rejected a second live daemon,
    so any surviving socket file is definitionally stale.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() or path.is_symlink():
        path.unlink()
    try:
        yield
    finally:
        if path.exists():
            try:
                path.unlink()
            except Exception:
                logger.exception("Failed to unlink socket on shutdown")


@final
class ShutdownRequest:
    """Two-stage shutdown signal.

    :attr:`requested` fires when graceful shutdown is requested.
    :attr:`force` fires when the caller wants to short-circuit graceful
    drain and cancel in-flight work.

    Signal mapping follows the conventional escalation chain
    ``SIGINT → SIGTERM → SIGKILL``: SIGINT is the polite interactive
    interrupt (graceful the first time, force on the second press);
    SIGTERM is already an escalation and skips straight to force;
    SIGKILL is not catchable — the systemd cgroup + ``podman --rm``
    are the backstop. SIGHUP (terminal close) tracks SIGINT semantics.
    """

    def __init__(self) -> None:
        self.requested: asyncio.Event = asyncio.Event()
        self.force: asyncio.Event = asyncio.Event()


@contextmanager
def _signal_handlers(loop: asyncio.AbstractEventLoop, shutdown: ShutdownRequest) -> Generator[None]:
    """Install escalating signal handlers.

    * **SIGINT / SIGHUP**: 1st fires the graceful request, 2nd escalates.
    * **SIGTERM**: forces immediately.
    * After ``force`` fires the handlers are removed, so a subsequent
      signal hits Python's default (SIGINT → ``KeyboardInterrupt``;
      SIGTERM → process terminates).
    """
    graceful_signals = (signal.SIGINT, signal.SIGHUP)
    handled = (*graceful_signals, signal.SIGTERM)

    def _remove_all() -> None:
        for s in handled:
            try:
                _ = loop.remove_signal_handler(s)
            except Exception:
                pass

    def _escalate(reason: str) -> None:
        if shutdown.force.is_set():
            return
        logger.warning(f"{reason}; cancelling in-flight work — next signal aborts")
        shutdown.requested.set()
        shutdown.force.set()
        _remove_all()

    def on_graceful() -> None:
        if not shutdown.requested.is_set():
            logger.info("Shutdown requested; send signal again (or SIGTERM) to force-cancel")
            shutdown.requested.set()
        else:
            _escalate("Second interactive signal")

    def on_force() -> None:
        _escalate("SIGTERM received")

    for s in graceful_signals:
        loop.add_signal_handler(s, on_graceful)
    loop.add_signal_handler(signal.SIGTERM, on_force)
    try:
        yield
    finally:
        _remove_all()


@asynccontextmanager
async def _runs_lifecycle(
    runs: RunsSupervisor, *, force: asyncio.Event, reply_timeout: float
) -> AsyncGenerator[None]:
    """Start the reaper on entry; await full supervisor shutdown on exit.

    On force, ``runs.shutdown`` short-circuits its reply gather and cancels
    any actor still in-flight; the partial state that leaves behind is
    reconciled by :meth:`RunsSupervisor.recover` on next boot — the same
    story as any abnormal termination.
    """
    await runs.start_reaper()
    try:
        yield
    finally:
        timeout = 0.0 if force.is_set() else reply_timeout
        await runs.shutdown(reply_timeout=timeout)


@asynccontextmanager
async def _uvicorn_serving(
    servers: Sequence[uvicorn.Server],
    *,
    drain_timeout: float,
    force: asyncio.Event,
) -> AsyncGenerator[list[asyncio.Task[None]]]:
    """Start serve tasks on entry, drain them on exit.

    On normal exit, signals ``should_exit`` and waits up to
    ``drain_timeout`` for each server to stop gracefully. If ``force`` fires
    first or the drain times out, cancels the serve tasks directly so
    uvicorn propagates the cancel into active request handlers.
    """
    tasks = [asyncio.create_task(srv.serve(), name=f"uvicorn-{i}") for i, srv in enumerate(servers)]
    try:
        yield tasks
    finally:
        for srv in servers:
            srv.should_exit = True

        # Heterogeneous awaitables whose return values we don't consume —
        # Task[object] is the legitimate top type here.
        async def _drain_all() -> None:
            _ = await asyncio.gather(*tasks, return_exceptions=True)

        drain: asyncio.Task[object] = asyncio.create_task(_drain_all(), name="uvicorn-drain")
        deadline: asyncio.Task[object] = asyncio.create_task(
            asyncio.sleep(drain_timeout), name="uvicorn-deadline"
        )
        forced: asyncio.Task[object] = asyncio.create_task(force.wait(), name="uvicorn-force-wait")
        done, pending = await asyncio.wait(
            {drain, deadline, forced}, return_when=asyncio.FIRST_COMPLETED
        )
        for t in (deadline, forced):
            if t in pending:
                _ = t.cancel()
        if drain not in done:
            if forced.done() and not forced.cancelled():
                logger.warning("Force-shutdown requested; cancelling uvicorn tasks")
            else:
                logger.warning("uvicorn servers did not stop in time; cancelling")
            for t in tasks:
                _ = t.cancel()
            _ = await asyncio.gather(*tasks, return_exceptions=True)


async def _wait_for_shutdown_or_death(
    serve_tasks: Sequence[asyncio.Task[None]], event: asyncio.Event
) -> None:
    """Block until either the shutdown event fires or a serve task dies.

    Without this, ``asyncio.wait(shutdown_task)`` alone would hang forever
    if uvicorn dies at startup (bind error, internal crash) — nobody would
    set the event.
    """
    shutdown_task = asyncio.create_task(event.wait(), name="shutdown-event")
    try:
        _ = await asyncio.wait(
            {*serve_tasks, shutdown_task},
            return_when=asyncio.FIRST_COMPLETED,
        )
    finally:
        _ = shutdown_task.cancel()
    for task in serve_tasks:
        if task.done() and task.exception() is not None:
            logger.error(f"{task.get_name()} died: {task.exception()}")


@final
class DashboardDaemon:
    def __init__(self, instance_dir: Path, socket_path: Path, frontend_dir: Path):
        self.instance_dir = instance_dir
        self.socket_path = socket_path
        self.frontend_dir = frontend_dir

        self._shutdown: ShutdownRequest = ShutdownRequest()

    async def run(self) -> None:
        async with contextlib.AsyncExitStack() as stack:
            _ = stack.enter_context(_instance_lock(self.instance_dir))
            storage = stack.enter_context(_storage_open(self.instance_dir / "runs.db"))

            config = _load_config(self.instance_dir / "config.json")
            dashboard_url = f"http://{config.host}:{config.dashboard_port}"
            grafana_url = f"http://{config.host}:{config.grafana_port}"
            # pasta's ``-T <dashboard_port>`` forwards the Grafana container's
            # ``127.0.0.1:<dashboard_port>`` to the host's, so the URL we
            # bake into the provisioned datasource is the same one a browser
            # would use. The daemon stays bound on ``127.0.0.1``.

            compose = Compose()
            provisioning = GrafanaProvisioning(
                provisioning_dir=self.instance_dir / "grafana" / "provisioning",
                daemon_host_url=dashboard_url,
                grafana_admin_url=grafana_url,
            )
            provisioning.write_dashboard_provider()
            provisioning.write_dashboard()

            runs = RunsSupervisor(
                instance_dir=self.instance_dir,
                storage=storage,
                compose=compose,
                provisioning=provisioning,
                port_range=config.prometheus_port_range,
                daemon_port=config.dashboard_port,
                daemon_base_url=dashboard_url,
            )

            await runs.recover()
            await stack.enter_async_context(
                _runs_lifecycle(runs, force=self._shutdown.force, reply_timeout=30.0)
            )

            await stack.enter_async_context(
                grafana_running(
                    compose,
                    host_port=config.grafana_port,
                    daemon_port=config.dashboard_port,
                    data_dir=self.instance_dir / "grafana" / "data",
                    provisioning_dir=self.instance_dir / "grafana" / "provisioning",
                )
            )

            stack.enter_context(_socket_path(self.socket_path))
            stack.enter_context(_signal_handlers(asyncio.get_running_loop(), self._shutdown))

            url_resolver = RunUrlResolver(dashboard_url=dashboard_url, grafana_url=grafana_url)
            app, proxy = create_app(
                storage=storage,
                runs=runs,
                url_resolver=url_resolver,
                frontend_dir=str(self.frontend_dir),
                on_shutdown_request=self._shutdown.requested.set,
            )
            _ = stack.push_async_callback(proxy.aclose)

            tcp_server = uvicorn.Server(
                uvicorn.Config(
                    app,
                    host=config.host,
                    port=config.dashboard_port,
                    log_level="info",
                    lifespan="off",
                )
            )
            uds_server = uvicorn.Server(
                uvicorn.Config(
                    app,
                    uds=str(self.socket_path),
                    log_level="info",
                    lifespan="off",
                )
            )

            logger.info(f"Dashboard: {dashboard_url}")
            logger.info(f"Grafana:   {grafana_url}")
            logger.info(f"IPC:       {self.socket_path}")

            serve_tasks = await stack.enter_async_context(
                _uvicorn_serving(
                    [tcp_server, uds_server],
                    drain_timeout=30.0,
                    force=self._shutdown.force,
                )
            )
            await _wait_for_shutdown_or_death(serve_tasks, self._shutdown.requested)
            logger.info("Shutting down...")


def _load_config(path: Path) -> DaemonConfig:
    if not path.exists():
        raise RuntimeError(f"No configuration at {path}")
    raw = cast(JSONSerializable, json.loads(path.read_text()))
    if not isinstance(raw, dict):
        raise RuntimeError(f"Configuration at {path} is not a JSON object")
    return DaemonConfig.model_validate(raw)
