import argparse
import asyncio
import json
import logging
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import cast

import httpx

from .config import DaemonConfig

logger = logging.getLogger(__name__)


# Kept in this leaf module rather than ``client.py`` so the CLI (which
# calls this from ``daemon start`` / ``daemon status``) doesn't pull in
# websockets + the full WS ``DashboardClient`` machinery on every invocation.
async def probe_daemon(socket_path: Path, timeout: float = 2.0) -> bool:
    """Check whether a daemon is responding on the given UDS socket."""
    transport = httpx.AsyncHTTPTransport(uds=str(socket_path))
    async with httpx.AsyncClient(
        transport=transport, base_url="http://daemon", timeout=timeout
    ) as client:
        try:
            response = await client.get("/health")
            return response.status_code == 200
        except httpx.HTTPError:
            return False


def get_default_paths() -> tuple[Path, Path, Path]:
    integration_dir = Path(__file__).resolve().parents[3] / "integration"
    instance_dir = integration_dir / ".dashboard"
    socket_path = instance_dir / "daemon.sock"
    frontend_dir = Path(__file__).parent / "frontend"
    return instance_dir, socket_path, frontend_dir


def _systemd_user_available() -> bool:
    """True if the current user's systemd session can run transient units.

    We require this because ``systemd-run --user`` is how we pin the daemon
    and all its podman children to one cgroup — so that on any abnormal
    death (OOM, SIGKILL, terminal close) the whole tree dies together and
    no orphan containers survive.
    """
    if shutil.which("systemd-run") is None:
        return False
    if shutil.which("systemctl") is None:
        return False
    try:
        result = subprocess.run(
            ["systemctl", "--user", "show-environment"],
            check=False,
            capture_output=True,
            timeout=5.0,
        )
    except OSError, subprocess.TimeoutExpired:
        return False
    return result.returncode == 0


def _transient_unit_name() -> str:
    return f"tontester-dashboard-{int(time.time())}-{os.getpid()}"


def _invoke_systemd_run(*, detached: bool) -> None:
    """Replace this process with ``systemd-run`` running the daemon.

    Foreground (``detached=False``):
        --pty --wait --send-sighup  — lifetime-bound to our stdio.

    Detached (``detached=True``):
        the unit keeps running after we return. No --wait / --pty.

    ``--collect`` reaps the transient unit from systemd's bookkeeping once
    it exits so the user doesn't need to run ``systemctl --user reset-failed``.

    ``KillMode=control-group`` (the default) ensures podman children die
    with the unit.
    """
    unit = _transient_unit_name()
    python = sys.executable
    module_cmd = [python, "-m", "daemon", "_run"]

    common_args = [
        "systemd-run",
        "--user",
        "--unit",
        unit,
        "--collect",
        "--property=KillMode=control-group",
        # Default KillSignal is SIGTERM, which our daemon treats as
        # "force immediately". Override to SIGINT so ``systemctl stop``
        # (and TimeoutStopSec expiry) goes through the graceful path.
        # SIGTERM stays available via ``kill -TERM`` for explicit force.
        "--property=KillSignal=SIGINT",
        "--property=Type=exec",
        "--setenv=PATH",
        "--setenv=HOME",
        "--setenv=XDG_RUNTIME_DIR",
    ]

    if detached:
        argv = [*common_args, "--", *module_cmd]
        logger.info(f"Starting {unit}")
        try:
            _ = subprocess.run(argv, check=True)
        except subprocess.CalledProcessError as e:
            logger.error(f"systemd-run failed: {e}")
            sys.exit(1)
        _, socket_path, _ = get_default_paths()
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            if socket_path.exists() and asyncio.run(probe_daemon(socket_path, timeout=0.5)):
                logger.info(f"Daemon up; follow logs with: journalctl --user -fu {unit}")
                return
            time.sleep(0.2)
        logger.error(f"Daemon did not respond within 10s; check `journalctl --user -u {unit}`")
        sys.exit(1)

    # Foreground: replace our process. systemd-run forwards SIGINT/SIGTERM
    # to the unit, and on stdin/PTY close it sends SIGHUP.
    argv = [
        *common_args,
        "--pty",
        "--wait",
        "--send-sighup",
        "--quiet",
        "--",
        *module_cmd,
    ]
    os.execvp(argv[0], argv)


async def _internal_run() -> None:
    """The actual daemon body. Invoked by ``systemd-run`` via ``daemon _run``.

    Users should call ``daemon start`` instead; this entry point exists only
    so ``systemd-run`` has a stable argv to exec. Serialization against a
    concurrent daemon on the same instance dir is enforced by the ``flock``
    acquired inside ``DashboardDaemon.run``.
    """
    # Lazy-imported: ``.daemon`` pulls in fastapi/uvicorn/etc., which the CLI
    # shouldn't pay for when the user runs ``daemon start``/``status``/``info``
    # (those only spawn or probe the subprocess that runs this body).
    from .daemon import DaemonAlreadyRunning, DashboardDaemon

    instance_dir, socket_path, frontend_dir = get_default_paths()
    config_path = instance_dir / "config.json"

    if not config_path.exists():
        instance_dir.mkdir(parents=True, exist_ok=True)
        default = DaemonConfig()
        _ = config_path.write_text(default.model_dump_json(indent=2) + "\n")
        logger.info(f"Wrote default config to {config_path}")

    if not frontend_dir.exists():
        logger.error(f"Frontend directory not found at {frontend_dir}")
        sys.exit(1)

    daemon = DashboardDaemon(instance_dir, socket_path, frontend_dir)
    logger.info("Starting dashboard daemon...")
    logger.info(f"Instance: {instance_dir}")
    logger.info(f"Socket:   {socket_path}")

    try:
        await daemon.run()
    except DaemonAlreadyRunning as e:
        logger.error(str(e))
        sys.exit(1)
    except KeyboardInterrupt:
        logger.info("Shutting down...")


def _start(*, detached: bool) -> None:
    _, socket_path, _ = get_default_paths()
    if socket_path.exists() and asyncio.run(probe_daemon(socket_path, timeout=1.0)):
        logger.error(f"Daemon is already running at {socket_path}")
        sys.exit(1)
    if not _systemd_user_available():
        logger.error(
            (
                "systemd --user session unavailable; the daemon requires it to "
                "guarantee that podman containers are torn down with the daemon. "
                "Ensure `systemctl --user show-environment` works."
            )
        )
        sys.exit(1)
    _invoke_systemd_run(detached=detached)


async def _stop() -> None:
    _, socket_path, _ = get_default_paths()
    if not socket_path.exists():
        logger.error("Daemon is not running")
        sys.exit(1)
    transport = httpx.AsyncHTTPTransport(uds=str(socket_path))
    async with httpx.AsyncClient(
        transport=transport, base_url="http://daemon", timeout=5.0
    ) as client:
        try:
            response = await client.post("/admin/shutdown")
            _ = response.raise_for_status()
        except httpx.HTTPError as e:
            logger.error(f"Failed to request shutdown: {e}")
            sys.exit(1)
    logger.info("Shutdown requested")


async def _status() -> None:
    instance_dir, socket_path, _ = get_default_paths()
    config_path = instance_dir / "config.json"

    if not socket_path.exists():
        logger.error("Daemon is not running (no socket)")
        sys.exit(1)

    if not await probe_daemon(socket_path):
        logger.error("Daemon socket exists but is not responding")
        sys.exit(1)

    logger.info("Daemon is running")
    logger.info(f"Config: {config_path}")
    logger.info(f"Socket: {socket_path}")


async def _info() -> None:
    instance_dir, socket_path, _ = get_default_paths()
    config_path = instance_dir / "config.json"
    if not config_path.exists():
        logger.error("No daemon config")
        sys.exit(1)
    data = config_path.read_text()
    config = DaemonConfig.model_validate_json(data)
    out = {
        "dashboard_url": f"http://{config.host}:{config.dashboard_port}",
        "grafana_url": f"http://{config.host}:{config.grafana_port}",
        "prometheus_port_range": list(config.prometheus_port_range),
        "socket": str(socket_path),
    }
    print(json.dumps(out, indent=2))


def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    )

    parser = argparse.ArgumentParser(
        prog="daemon",
        description="tontester dashboard daemon",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    start_parser = subparsers.add_parser(
        "start",
        help="Run the daemon",
    )
    _ = start_parser.add_argument(
        "--daemonize",
        action="store_true",
        help="Keep the daemon running after this command returns",
    )

    _ = subparsers.add_parser("stop", help="Stop the daemon via /admin/shutdown")
    _ = subparsers.add_parser("status", help="Check daemon status")
    _ = subparsers.add_parser("info", help="Print URLs and socket path for the daemon")

    _ = subparsers.add_parser("_run", help=argparse.SUPPRESS)

    args = parser.parse_args()
    command = cast(str, args.command)

    match command:
        case "start":
            _start(detached=cast(bool, args.daemonize))
        case "_run":
            asyncio.run(_internal_run())
        case "stop":
            asyncio.run(_stop())
        case "status":
            asyncio.run(_status())
        case "info":
            asyncio.run(_info())
        case _:
            parser.error(f"Unknown command: {command}")


if __name__ == "__main__":
    main()
