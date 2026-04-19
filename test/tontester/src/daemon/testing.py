"""Test doubles for the daemon package."""

import asyncio
from collections import deque
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import final, override

from .compose import ContainerStartFailed, PodmanTimeout
from .models import RunMetadata, RunStatus, Service, TestMetadata
from .protocols import ComposeLike, ProcessHandle, ProcessRunner, ProvisioningLike, StorageBackend
from .runs import RunsSupervisor
from .services import GrafanaProvisioning
from .sqlite_storage import SQLiteStorage


@dataclass(frozen=True)
class FakeContainer:
    service: Service


@final
class FakeCompose(ComposeLike):
    """In-memory container tracker that looks enough like the real Compose."""

    def __init__(self) -> None:
        self.containers: dict[str, FakeContainer] = {}
        self.pending_up_gate: asyncio.Event | None = None
        self.up_should_fail = False  # one-shot: auto-clears after firing
        self.down_should_fail = (
            False  # sticky: stays set across calls (shutdown tests fail every down)
        )
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
        if self.down_should_fail:
            raise PodmanTimeout([name], 1.0)
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


@dataclass(frozen=True)
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


@dataclass(eq=False)
class WsServer:
    """Handle to a uvicorn instance running the ws IPC router for tests."""

    url: str
    rig: "Rig"


@dataclass(eq=False)
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
    """Wraps a real ``SQLiteStorage`` with one-shot fault injection.

    The ``fail_next_*`` fields, when non-``None``, raise the stored
    exception on the matching call and then clear themselves. Used by
    exception-safety tests to verify rollback invariants.
    """

    def __init__(self, delegate: SQLiteStorage):
        self._delegate = delegate
        self.fail_next_register_run: Exception | None = None
        self.fail_next_set_status: dict[RunStatus, Exception] = {}
        self.fail_next_get_run_metadata: Exception | None = None

    @override
    async def register_run(self, run_id: str, metadata: TestMetadata) -> None:
        exc = self.fail_next_register_run
        if exc is not None:
            self.fail_next_register_run = None
            raise exc
        await self._delegate.register_run(run_id, metadata)

    @override
    async def set_run_status(self, run_id: str, status: RunStatus) -> None:
        exc = self.fail_next_set_status.pop(status, None)
        if exc is not None:
            raise exc
        await self._delegate.set_run_status(run_id, status)

    @override
    async def list_runs(self, limit: int = 50) -> list[RunMetadata]:
        return await self._delegate.list_runs(limit=limit)

    @override
    async def get_run_metadata(self, run_id: str) -> RunMetadata | None:
        exc = self.fail_next_get_run_metadata
        if exc is not None:
            self.fail_next_get_run_metadata = None
            raise exc
        return await self._delegate.get_run_metadata(run_id)

    @override
    async def list_runs_with_status(self, status: RunStatus) -> list[RunMetadata]:
        return await self._delegate.list_runs_with_status(status)


# ============================================================ Provisioning test double


@final
class FaultyProvisioning(ProvisioningLike):
    """Wraps a real ``GrafanaProvisioning`` with one-shot fault injection.

    Forwards ``datasource_file`` for test assertions. Fault flags are
    one-shot: the exception is raised once, then the flag clears.
    """

    def __init__(self, delegate: GrafanaProvisioning):
        self._delegate = delegate
        self.fail_next_write: Exception | None = None
        self.fail_next_remove: Exception | None = None

    @override
    def write_run_datasource(self, run_id: str) -> None:
        exc = self.fail_next_write
        if exc is not None:
            self.fail_next_write = None
            raise exc
        self._delegate.write_run_datasource(run_id)

    @override
    def remove_run_datasource(self, run_id: str) -> None:
        exc = self.fail_next_remove
        if exc is not None:
            self.fail_next_remove = None
            raise exc
        self._delegate.remove_run_datasource(run_id)

    @override
    def list_provisioned_runs(self) -> set[str]:
        return self._delegate.list_provisioned_runs()

    @override
    async def reload_datasources(self) -> None:
        # Tests don't run a real Grafana; treat as a no-op rather than
        # forwarding (which would attempt a real HTTP call).
        return None

    def datasource_file(self, run_id: str) -> Path:
        return self._delegate.datasource_file(run_id)


@dataclass(eq=False)
class Rig:
    """Everything a test wants from the supervisor plus the fakes behind it."""

    manager: RunsSupervisor  # named ``manager`` for legacy test compat
    compose: FakeCompose
    storage: StorageBackend
    sqlite: SQLiteStorage
    hooks: HookedStorage | None
    provisioning: GrafanaProvisioning
    faulty_provisioning: FaultyProvisioning | None
    instance_dir: Path


def build_rig(
    tmp_path: Path,
    *,
    hooked_storage: bool = False,
    faulty_provisioning: bool = False,
) -> Rig:
    compose = FakeCompose()
    sqlite = SQLiteStorage(tmp_path / "runs.db")
    hooks = HookedStorage(sqlite) if hooked_storage else None
    storage: StorageBackend = hooks if hooks is not None else sqlite
    provisioning = GrafanaProvisioning(
        tmp_path / "grafana" / "provisioning",
        "http://daemon",
        grafana_admin_url="http://grafana.test",
    )
    faulty = FaultyProvisioning(provisioning) if faulty_provisioning else None
    supervisor_provisioning: ProvisioningLike = faulty if faulty is not None else provisioning
    supervisor = RunsSupervisor(
        instance_dir=tmp_path,
        storage=storage,
        compose=compose,
        provisioning=supervisor_provisioning,
        port_range=(9100, 9199),
        daemon_port=8080,
        daemon_base_url="http://127.0.0.1:8080",
        archive_idle_ttl_seconds=60.0,
        reap_interval_seconds=1000.0,  # tests drive ``run_reaper_pass`` directly
    )
    return Rig(
        manager=supervisor,
        compose=compose,
        storage=storage,
        sqlite=sqlite,
        hooks=hooks,
        provisioning=provisioning,
        faulty_provisioning=faulty,
        instance_dir=tmp_path,
    )
