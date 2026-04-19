"""Thin podman wrapper — the only file in the package that shells out to podman.

Everything that wants to run a container goes through :class:`Compose`, which
keeps no in-memory state: every query for "is X running" goes to `podman ps`,
so the daemon's view of reality cannot drift from reality itself.
"""

import asyncio
import logging
import os
import subprocess
from collections.abc import Callable
from typing import final, override

import httpx

from .models import HttpReadyProbe, Service
from .protocols import ComposeLike, ProcessHandle, ProcessRunner, ReadyProbeFn

logger = logging.getLogger(__name__)


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


@final
class Compose(ComposeLike):
    """podman wrapper.

    Containers run on rootless pasta networking with per-service port
    whitelists: no default host-to-guest or guest-to-host forwarding, and
    each :class:`Service` explicitly declares the host loopback ports it
    needs to reach. With everything disabled by default the daemon can
    stay bound on ``127.0.0.1`` and containers still can't wander outside
    the whitelist — no external-interface exposure, and isolation between
    containers that don't opt into the same ports.
    """

    def __init__(
        self,
        *,
        runner: ProcessRunner | None = None,
        ready_probe: ReadyProbeFn | None = None,
    ):
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

    async def is_running(self, name: str) -> bool:
        out = await self._podman(
            ["ps", "--quiet", "--filter", f"name=^{name}$"],
            check=False,
        )
        return bool(out)

    @override
    async def list_running(self, prefix: str) -> list[str]:
        out = await self._podman(
            ["ps", "--format", "{{.Names}}", "--filter", f"name=^{prefix}"],
            check=False,
        )
        if not out:
            return []
        return [line for line in out.splitlines() if line]

    @override
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
            _build_pasta_network_arg(service),
            "--stop-timeout",
            str(service.stop_timeout_seconds),
        ]
        for mount in service.volumes:
            mount.host_path.mkdir(parents=True, exist_ok=True)
            cmd.extend(["-v", f"{mount.host_path}:{mount.container_path}:Z"])
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

    @override
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


def _build_pasta_network_arg(service: Service) -> str:
    """Compose ``--network=pasta:...`` with explicit port whitelists.

    Isolation contract:

    * ``-t none`` / ``-u none`` — no default host-to-guest forwarding; only
      the ports from ``service.ports`` become reachable from outside the
      container. Pasta syntax ``address/host_port:container_port`` maps
      host's bound port to the container's listening port.
    * ``-T`` / ``-U none`` — no default guest-to-host forwarding; the
      container's loopback ports ``127.0.0.1:<p>`` for ``p`` in
      ``host_egress_ports`` are spliced to host's ``127.0.0.1:<p>``.
    * ``-o 127.0.0.1`` — pin pasta's outbound NAT source to loopback. Any
      container attempt to reach a non-loopback destination (external IPs,
      LAN) fails because a socket bound to ``127.0.0.1`` can't route to a
      non-loopback target. Splice-bypass is a separate code path and is
      NOT affected. Combined with the ``-T`` whitelist this gives the
      container exactly the access the whitelist names — nothing else.
    * ``-4`` — disable IPv6 entirely in the namespace so there's no
      parallel ``::1`` or IPv6-external path that sidesteps ``-o``.

    Lists of multiple ports within one pasta flag use ``","`` as pasta's
    separator; podman splits options on ``","`` too, so we escape each
    internal comma as ``",,"`` per podman's documented pasta escaping rule.
    """
    parts: list[str] = ["pasta"]
    if service.ports:
        specs = [
            f"127.0.0.1/{h}:{c}" if h != c else f"127.0.0.1/{h}" for h, c in service.ports.items()
        ]
        parts.extend(["-t", ",,".join(specs)])
    else:
        parts.extend(["-t", "none"])
    parts.extend(["-u", "none"])
    if service.host_egress_ports:
        # pasta -T doesn't accept an address qualifier in practice (it
        # always listens on the namespace's 127.0.0.1), just the port list.
        parts.extend(["-T", ",,".join(str(p) for p in service.host_egress_ports)])
    else:
        parts.extend(["-T", "none"])
    parts.extend(["-U", "none"])
    parts.extend(["-o", "127.0.0.1", "-4"])
    return parts[0] + ":" + ",".join(parts[1:])


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
