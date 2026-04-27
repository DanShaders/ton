"""Startup filesystem cleanup.

The orchestrator-crash-=-payload-crash contract means we never expect
to find live processes from a previous run, but we *do* expect to
find debris on the filesystem (cgroup dirs, bundle dirs, stale lock
files). The agent calls ``clean_state_dir`` once at startup to mop
those up before serving any reconciles.

Best-effort: every operation that fails logs and continues. The agent
shouldn't refuse to start because a leftover dir is wedged.
"""

import logging
import os
import shutil
import signal
from pathlib import Path

logger = logging.getLogger(__name__)


def clean_state_dir(state_dir: Path) -> None:
    """Remove every workload artifact in ``state_dir``.

    Intended layout:
      state_dir/
        workloads/
          ns-name/
            <bundle stuff>
            cgroup -> /sys/fs/cgroup/wl-ns-name (symlink)

    We don't *use* this layout in the prototype subprocess backend, but
    we still walk and clean it so RuncRuntime can drop in without an
    additional cleanup pass.
    """
    if not state_dir.exists():
        return
    workloads_dir = state_dir / "workloads"
    if not workloads_dir.exists():
        return
    for entry in workloads_dir.iterdir():
        try:
            if entry.is_symlink() or entry.is_file():
                entry.unlink(missing_ok=True)
                continue
            shutil.rmtree(entry, ignore_errors=True)
        except OSError:
            logger.exception(f"cleanup of {entry} raised; leaving for the next pass")


def clean_cgroup_root(cgroup_root: Path | None) -> None:
    """Kill survivors and rmdir per-workload cgroups under ``cgroup_root``.

    On clean exits, :class:`SubprocessRuntime._stop_locked` already
    rmdir'd these. This sweep catches the case where a previous
    orchestrator process was SIGKILLed before it could.
    """
    if cgroup_root is None or not cgroup_root.exists():
        return
    for entry in cgroup_root.iterdir():
        if not entry.name.startswith("wl-"):
            continue
        if not entry.is_dir():
            continue
        _kill_and_rmdir(entry)


def _kill_and_rmdir(cgroup_dir: Path) -> None:
    procs_file = cgroup_dir / "cgroup.procs"
    if procs_file.exists():
        try:
            content = procs_file.read_text()
        except OSError:
            content = ""
        for line in content.split():
            if not line:
                continue
            try:
                pid = int(line)
            except ValueError:
                continue
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            except PermissionError:
                logger.warning(f"no permission to kill survivor pid {pid} in {cgroup_dir}")
    try:
        cgroup_dir.rmdir()
    except OSError:
        logger.exception(f"could not rmdir {cgroup_dir}; leaving for next sweep")
