"""Thin podman wrapper — the only file in the package that shells out to podman.

Everything that wants to run a container goes through :class:`Compose`, which
keeps no in-memory state: every query for "is X running" goes to `podman ps`,
so the daemon's view of reality cannot drift from reality itself.
"""

import asyncio
import json
import logging
import os
import subprocess
from collections.abc import Awaitable, Callable
from pathlib import Path
from typing import Protocol, cast, final, override

import httpx
from pydantic import BaseModel

from tl import JSONSerializable

logger = logging.getLogger(__name__)


class VolumeMount(BaseModel):
    host_path: Path
    container_path: str
    read_only: bool = False


class HttpReadyProbe(BaseModel):
    path: str
    container_port: int
    timeout_seconds: float = 30.0
    interval_seconds: float = 0.25


class Service(BaseModel):
    name: str
    image: str
    command: list[str] = []
    env: dict[str, str] = {}
    ports: dict[int, int] = {}  # host_port -> container_port
    volumes: list[VolumeMount] = []
    ready_probe: HttpReadyProbe | None = None
    stop_timeout_seconds: int = 60  # give prometheus room to flush the head block


class ContainerStartFailed(RuntimeError):
    pass


class ContainerReadinessTimeout(RuntimeError):
    pass


class PodmanTimeout(RuntimeError):
    def __init__(self, command: list[str], timeout: float):
        preview = " ".join(command[:4])
        super().__init__(f"podman {preview} timed out after {timeout:.0f}s")
        self.command: list[str] = command
        self.timeout: float = timeout


_DEFAULT_TIMEOUT = 10.0
_STOP_TIMEOUT = 90.0


class ProcessHandle(Protocol):
    """Subset of ``asyncio.subprocess.Process`` that :class:`Compose` uses."""

    @property
    def returncode(self) -> int | None: ...

    async def wait(self) -> int | None: ...

    def terminate(self) -> None: ...

    def kill(self) -> None: ...


class ProcessRunner(Protocol):
    """Abstracts subprocess creation so tests can substitute a fake."""

    async def run_capture(self, argv: list[str], *, timeout: float) -> tuple[int, bytes, bytes]:
        """Run ``argv`` to completion, return (returncode, stdout, stderr).

        Must raise ``asyncio.TimeoutError`` if the process does not exit
        within ``timeout``; Compose translates that to :class:`PodmanTimeout`.
        """
        ...

    async def run_attached(self, argv: list[str]) -> ProcessHandle:
        """Spawn an attached process (inherits stdout/stderr) and return a
        handle to it. Must not wait for exit."""
        ...


type ReadyProbeFn = Callable[[str, float], Awaitable[bool]]
"""Takes a URL and a timeout, returns True iff the endpoint responded with
a 2xx within the timeout. Tests substitute this to avoid real HTTP."""


@final
class _RealProcessRunner(ProcessRunner):
    @override
    async def run_capture(self, argv: list[str], *, timeout: float) -> tuple[int, bytes, bytes]:
        proc = await asyncio.create_subprocess_exec(
            argv[0],
            *argv[1:],
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        try:
            stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=timeout)
        except asyncio.TimeoutError:
            try:
                proc.kill()
            except ProcessLookupError:
                pass
            try:
                _ = await asyncio.wait_for(proc.wait(), timeout=2.0)
            except asyncio.TimeoutError:
                pass
            raise
        assert proc.returncode is not None
        return proc.returncode, stdout, stderr

    @override
    async def run_attached(self, argv: list[str]) -> ProcessHandle:
        return await asyncio.create_subprocess_exec(
            argv[0], *argv[1:], stdin=asyncio.subprocess.DEVNULL
        )


async def _default_ready_probe(url: str, timeout: float) -> bool:
    async with httpx.AsyncClient(timeout=timeout) as client:
        try:
            response = await client.get(url)
            return response.is_success
        except httpx.HTTPError:
            return False


class ComposeLike(Protocol):
    """Minimal surface consumed by :class:`RunsManager`."""

    async def up(self, service: Service) -> None: ...

    async def down(self, name: str) -> None: ...

    async def list_running(self, prefix: str) -> list[str]: ...


@final
class Compose:
    def __init__(
        self,
        network: str,
        *,
        runner: ProcessRunner | None = None,
        ready_probe: ReadyProbeFn | None = None,
    ):
        self.network = network
        self._runner = runner or _RealProcessRunner()
        self._ready_probe = ready_probe or _default_ready_probe
        self._processes: dict[str, ProcessHandle] = {}

    async def _podman(
        self,
        args: list[str],
        *,
        check: bool = True,
        timeout: float = _DEFAULT_TIMEOUT,
    ) -> str:
        """Run a one-shot ``podman`` command, bounded by ``timeout``.

        Raises :class:`PodmanTimeout` if the binary hangs. ``check=False``
        suppresses non-zero exits but not timeouts — a wedged backend is
        always surfaced.
        """
        try:
            returncode, stdout, stderr = await self._runner.run_capture(
                ["podman", *args], timeout=timeout
            )
        except asyncio.TimeoutError:
            raise PodmanTimeout(args, timeout)
        if check and returncode != 0:
            raise subprocess.CalledProcessError(returncode, ["podman", *args], stdout, stderr)
        return stdout.decode().strip()

    async def ensure_network(self) -> None:
        # ``network exists`` returns 0 if present, 1 if not; we need the code.
        argv = ["podman", "network", "exists", self.network]
        try:
            returncode, _, _ = await self._runner.run_capture(argv, timeout=_DEFAULT_TIMEOUT)
        except asyncio.TimeoutError:
            raise PodmanTimeout(argv[1:], _DEFAULT_TIMEOUT)
        if returncode != 0:
            _ = await self._podman(["network", "create", self.network])
            logger.info(f"Created podman network '{self.network}'")

    async def is_running(self, name: str) -> bool:
        out = await self._podman(
            ["ps", "--quiet", "--filter", f"name=^{name}$"],
            check=False,
        )
        return bool(out)

    async def list_running(self, prefix: str) -> list[str]:
        out = await self._podman(
            ["ps", "--format", "{{.Names}}", "--filter", f"name=^{prefix}"],
            check=False,
        )
        if not out:
            return []
        return [line for line in out.splitlines() if line]

    async def up(self, service: Service) -> None:
        await self._remove_if_exists(service.name)

        cmd = [
            "run",
            "--rm",
            "--name",
            service.name,
            "--user",
            f"{os.getuid()}:{os.getgid()}",
            "--userns",
            "keep-id",
            "--network",
            self.network,
            "--add-host",
            "host.containers.internal:host-gateway",
            "--stop-timeout",
            str(service.stop_timeout_seconds),
        ]
        for host_port, container_port in service.ports.items():
            cmd.extend(["-p", f"127.0.0.1:{host_port}:{container_port}"])
        for mount in service.volumes:
            mount.host_path.mkdir(parents=True, exist_ok=True)
            suffix = ":ro" if mount.read_only else ""
            cmd.extend(["-v", f"{mount.host_path}:{mount.container_path}:Z{suffix}"])
        for key, value in service.env.items():
            cmd.extend(["-e", f"{key}={value}"])
        cmd.append(service.image)
        cmd.extend(service.command)

        try:
            process = await self._runner.run_attached(["podman", *cmd])
        except OSError as e:
            raise ContainerStartFailed(f"Failed to spawn podman for {service.name}: {e}") from e
        self._processes[service.name] = process

        if service.ready_probe is not None:
            try:
                await self._wait_ready(service, service.ready_probe, process)
            except Exception:
                await self.down(service.name)
                raise

        if process.returncode is not None:
            # Post-ready exit: container started, became ready, then died.
            # Tear down anything left and raise so the caller rolls back.
            await self.down(service.name)
            raise ContainerStartFailed(
                f"{service.name} exited during startup (code {process.returncode})"
            )

        logger.info(f"Started container {service.name}")

    async def down(self, name: str) -> None:
        """Stop a container and reap its attached podman process.

        Idempotent: calling on an already-gone container is a no-op.
        """
        try:
            _ = await self._podman(["stop", name], check=False, timeout=_STOP_TIMEOUT)
        except PodmanTimeout:
            logger.warning(f"podman stop {name} hung; falling back to signaling the attached child")
        proc = self._processes.pop(name, None)
        if proc is not None and proc.returncode is None:
            await _reap_attached_process(name, proc)
        try:
            await self._remove_if_exists(name)
        except PodmanTimeout:
            logger.warning(f"podman rm -f {name} hung; leaving for recovery")
        logger.info(f"Stopped container {name}")

    async def inspect(self, name: str) -> dict[str, JSONSerializable] | None:
        out = await self._podman(["inspect", name], check=False)
        if not out:
            return None
        try:
            parsed = cast(JSONSerializable, json.loads(out))
        except json.JSONDecodeError:
            return None
        if not isinstance(parsed, list) or not parsed:
            return None
        first = parsed[0]
        if not isinstance(first, dict):
            return None
        return first

    async def _remove_if_exists(self, name: str) -> None:
        _ = await self._podman(["rm", "-f", name], check=False)

    async def _wait_ready(
        self,
        service: Service,
        probe: HttpReadyProbe,
        process: ProcessHandle,
    ) -> None:
        host_port = _find_host_port(service.ports, probe.container_port)
        if host_port is None:
            raise ContainerReadinessTimeout(
                (
                    f"Readiness probe for {service.name} targets container port "
                    f"{probe.container_port} which is not mapped"
                )
            )
        url = f"http://127.0.0.1:{host_port}{probe.path}"
        deadline = asyncio.get_running_loop().time() + probe.timeout_seconds
        while True:
            if process.returncode is not None:
                raise ContainerStartFailed(
                    f"{service.name} exited before becoming ready (code {process.returncode})"
                )
            if await self._ready_probe(url, 2.0):
                return
            if asyncio.get_running_loop().time() >= deadline:
                raise ContainerReadinessTimeout(
                    (
                        f"{service.name} did not become ready at {url} "
                        f"within {probe.timeout_seconds}s"
                    )
                )
            await asyncio.sleep(probe.interval_seconds)


def _try_signal(signal_fn: Callable[[], None]) -> None:
    # ``terminate()`` / ``kill()`` raise ProcessLookupError if the process
    # exited between our check and the signal call — harmless.
    try:
        signal_fn()
    except ProcessLookupError:
        pass


async def _reap_attached_process(name: str, proc: ProcessHandle) -> None:
    """Wait for the attached podman process to exit, escalating signals.

    First we just wait (the container should be on its way down from
    ``podman stop``); then SIGTERM; then SIGKILL. Each stage is bounded
    so a wedged child can't pin the daemon.
    """

    async def _wait(seconds: float) -> bool:
        try:
            _ = await asyncio.wait_for(proc.wait(), timeout=seconds)
            return True
        except asyncio.TimeoutError:
            return False

    if await _wait(10.0):
        return
    logger.warning(f"podman run for {name} did not exit after stop; terminating")
    _try_signal(proc.terminate)
    if await _wait(5.0):
        return
    _try_signal(proc.kill)
    if not await _wait(2.0):
        logger.error(f"podman run for {name} unreapable even after SIGKILL")


def _find_host_port(ports: dict[int, int], container_port: int) -> int | None:
    for host_port, cport in ports.items():
        if cport == container_port:
            return host_port
    return None
