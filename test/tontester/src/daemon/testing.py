"""Test doubles for the daemon package."""

import asyncio
from collections import deque
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import final, override

from .compose import (
    ComposeLike,
    ContainerStartFailed,
    ProcessHandle,
    ProcessRunner,
    Service,
)
from .runs import RunsManager
from .services import GrafanaProvisioning
from .sqlite_storage import SQLiteStorage
from .storage import RunMetadata, RunStatus, StorageBackend, TestMetadata


@dataclass
class FakeContainer:
    service: Service


@final
class FakeCompose(ComposeLike):
    """In-memory container tracker that looks enough like the real Compose."""

    def __init__(self) -> None:
        self.containers: dict[str, FakeContainer] = {}
        self.pending_up_gate: asyncio.Event | None = None
        self.up_should_fail = False
        self.call_log: list[tuple[str, str]] = []

    @override
    async def up(self, service: Service) -> None:
        self.call_log.append(("up", service.name))
        gate = self.pending_up_gate
        if gate is not None:
            _ = await gate.wait()
        if self.up_should_fail:
            self.up_should_fail = False
            raise ContainerStartFailed(f"simulated start failure for {service.name}")
        self.containers[service.name] = FakeContainer(service=service)

    @override
    async def down(self, name: str) -> None:
        self.call_log.append(("down", name))
        _ = self.containers.pop(name, None)

    @override
    async def list_running(self, prefix: str) -> list[str]:
        return [n for n in self.containers if n.startswith(prefix)]

    def is_running(self, name: str) -> bool:
        return name in self.containers


# ============================================================ Compose test doubles


@final
class FakeProcessHandle(ProcessHandle):
    """Controllable stand-in for ``asyncio.subprocess.Process``.

    A process is "running" until a test calls :meth:`simulate_exit` (or
    :meth:`terminate` / :meth:`kill`, which simulate-exit with the signal
    code). Callers to ``wait()`` block on an internal event.
    """

    def __init__(self) -> None:
        self._returncode: int | None = None
        self._exit_event = asyncio.Event()
        # If True, ``terminate()`` and ``kill()`` are no-ops — simulates a
        # wedged process that doesn't respond to either signal.
        self.ignore_signals: bool = False

    @property
    @override
    def returncode(self) -> int | None:
        return self._returncode

    @override
    async def wait(self) -> int | None:
        _ = await self._exit_event.wait()
        return self._returncode

    @override
    def terminate(self) -> None:
        if not self.ignore_signals:
            self.simulate_exit(-15)

    @override
    def kill(self) -> None:
        if not self.ignore_signals:
            self.simulate_exit(-9)

    def simulate_exit(self, returncode: int) -> None:
        if self._returncode is None:
            self._returncode = returncode
            self._exit_event.set()


@dataclass
class _QueuedCapture:
    returncode: int = 0
    stdout: bytes = b""
    stderr: bytes = b""
    hang: bool = False  # if True, capture hangs until the test's timeout fires


@final
class FakeProcessRunner(ProcessRunner):
    """Scriptable subprocess runner for :class:`Compose` tests.

    * ``queue_capture(...)`` stacks responses for the next ``run_capture``
      calls in order. Calls beyond the queue use :attr:`default_capture`.
    * Matching by ``argv`` is done by substring (subcommand like ``stop`` or
      ``network exists``) so tests don't have to list every flag.
    * ``run_attached`` returns fresh :class:`FakeProcessHandle`\\ s that tests
      can manipulate via :attr:`attached`.
    """

    def __init__(self) -> None:
        self.default_capture = _QueuedCapture()
        self._queued_by_match: dict[str, deque[_QueuedCapture]] = {}
        self.capture_log: list[list[str]] = []
        self.attached: list[FakeProcessHandle] = []
        self.attached_log: list[list[str]] = []
        self._attached_by_name: dict[str, FakeProcessHandle] = {}

    def queue_capture(
        self,
        *,
        match: str,
        returncode: int = 0,
        stdout: bytes = b"",
        stderr: bytes = b"",
        hang: bool = False,
    ) -> None:
        self._queued_by_match.setdefault(match, deque()).append(
            _QueuedCapture(returncode=returncode, stdout=stdout, stderr=stderr, hang=hang)
        )

    @override
    async def run_capture(self, argv: list[str], *, timeout: float) -> tuple[int, bytes, bytes]:
        self.capture_log.append(argv)
        response = self._pop_match(argv)
        if response.hang:
            raise asyncio.TimeoutError
        # Real ``podman stop <name>`` propagates SIGTERM to the attached child;
        # mimic that so tests see the run_attached handle exit too.
        if response.returncode == 0 and len(argv) >= 3 and argv[1] == "stop":
            handle = self._attached_by_name.pop(argv[2], None)
            if handle is not None:
                handle.simulate_exit(0)
        return response.returncode, response.stdout, response.stderr

    @override
    async def run_attached(self, argv: list[str]) -> ProcessHandle:
        self.attached_log.append(argv)
        handle = FakeProcessHandle()
        self.attached.append(handle)
        name = _name_from_podman_run(argv)
        if name is not None:
            self._attached_by_name[name] = handle
        return handle

    def _pop_match(self, argv: list[str]) -> _QueuedCapture:
        for match, queue in self._queued_by_match.items():
            if match in " ".join(argv) and queue:
                return queue.popleft()
        return self.default_capture


def _name_from_podman_run(argv: list[str]) -> str | None:
    for i, a in enumerate(argv):
        if a == "--name" and i + 1 < len(argv):
            return argv[i + 1]
    return None


@dataclass
class WsServer:
    """Handle to a uvicorn instance running the ws IPC router for tests."""

    url: str
    rig: "Rig"


@dataclass
class ComposeRig:
    """Bundle of the fakes behind a :class:`Compose` under test."""

    runner: FakeProcessRunner
    ready: "FakeReadyProbe"


@final
class FakeReadyProbe:
    """Scriptable readiness probe. Defaults to ``True`` once ``ok`` is set.

    ``before_returning_true`` fires the first time the probe is about to
    return ``True`` — lets tests race in a state change (e.g. "the container
    died between readiness and the post-ready check") without relying on
    asyncio scheduling luck.
    """

    def __init__(self) -> None:
        self.ok = False
        self.calls: list[str] = []
        self.before_returning_true: Callable[[], None] | None = None

    async def __call__(self, url: str, timeout: float) -> bool:
        self.calls.append(url)
        if self.ok and self.before_returning_true is not None:
            hook = self.before_returning_true
            self.before_returning_true = None
            hook()
        return self.ok


# ============================================================ Storage test doubles


@final
class HookedStorage(StorageBackend):
    """Wraps a real ``SQLiteStorage`` and lets tests pause individual writes.

    Used by race-condition tests that need to interleave coroutines at a
    specific storage boundary (e.g. pausing the rollback's DORMANT write so
    a concurrent ``register`` can observe the window).
    """

    def __init__(self, delegate: SQLiteStorage):
        self._delegate = delegate
        self._pause_set_status: tuple[RunStatus, asyncio.Event] | None = None

    def pause_next_set_status(self, status: RunStatus) -> asyncio.Event:
        event = asyncio.Event()
        self._pause_set_status = (status, event)
        return event

    @override
    async def register_run(self, run_id: str, metadata: TestMetadata, host_port: int) -> None:
        await self._delegate.register_run(run_id, metadata, host_port)

    @override
    async def set_run_status(
        self,
        run_id: str,
        status: RunStatus,
        *,
        if_port: int | None = None,
    ) -> None:
        pause = self._pause_set_status
        if pause is not None and pause[0] == status:
            self._pause_set_status = None
            _ = await pause[1].wait()
        await self._delegate.set_run_status(run_id, status, if_port=if_port)

    @override
    async def list_runs(self, limit: int = 50) -> list[RunMetadata]:
        return await self._delegate.list_runs(limit=limit)

    @override
    async def get_run_metadata(self, run_id: str) -> RunMetadata | None:
        return await self._delegate.get_run_metadata(run_id)

    @override
    async def list_runs_with_status(self, status: RunStatus) -> list[RunMetadata]:
        return await self._delegate.list_runs_with_status(status)

    @override
    async def list_allocated_ports(self) -> set[int]:
        return await self._delegate.list_allocated_ports()


@dataclass
class Rig:
    """Everything a test wants from the manager plus the fakes behind it."""

    manager: RunsManager
    compose: FakeCompose
    storage: StorageBackend
    sqlite: SQLiteStorage  # the underlying concrete store, always present
    hooks: HookedStorage | None  # set iff build_rig(hooked_storage=True)
    provisioning: GrafanaProvisioning
    instance_dir: Path


def build_rig(tmp_path: Path, *, hooked_storage: bool = False) -> Rig:
    compose = FakeCompose()
    sqlite = SQLiteStorage(tmp_path / "runs.db")
    hooks = HookedStorage(sqlite) if hooked_storage else None
    storage: StorageBackend = hooks if hooks is not None else sqlite
    provisioning = GrafanaProvisioning(tmp_path / "grafana" / "provisioning", "http://daemon")
    manager = RunsManager(
        instance_dir=tmp_path,
        storage=storage,
        compose=compose,
        provisioning=provisioning,
        port_range=(9100, 9199),
        archive_idle_ttl_seconds=60.0,
        reap_interval_seconds=1000.0,  # tests drive ``run_reaper_pass`` directly
    )
    return Rig(
        manager=manager,
        compose=compose,
        storage=storage,
        sqlite=sqlite,
        hooks=hooks,
        provisioning=provisioning,
        instance_dir=tmp_path,
    )
