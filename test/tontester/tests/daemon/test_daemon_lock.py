"""Tests for :func:`daemon.daemon.acquire_instance_lock`.

The flock is the only authoritative serialization between concurrent
``daemon start`` invocations that survive the systemd-run transient-unit
teardown. Both the old ``probe_daemon`` check in ``_start`` and the
duplicate probe in ``_internal_run`` were racy; the lock is not.
"""

import os
from pathlib import Path

import pytest
from daemon.daemon import DaemonAlreadyRunning, acquire_instance_lock


def test_lock_is_exclusive_within_instance_dir(tmp_path: Path):
    fd1 = acquire_instance_lock(tmp_path)
    try:
        with pytest.raises(DaemonAlreadyRunning):
            _ = acquire_instance_lock(tmp_path)
    finally:
        os.close(fd1)


def test_lock_releases_on_fd_close(tmp_path: Path):
    fd1 = acquire_instance_lock(tmp_path)
    os.close(fd1)
    fd2 = acquire_instance_lock(tmp_path)
    os.close(fd2)


def test_separate_instance_dirs_do_not_collide(tmp_path: Path):
    a, b = tmp_path / "a", tmp_path / "b"
    fd_a = acquire_instance_lock(a)
    fd_b = acquire_instance_lock(b)
    try:
        # Both held simultaneously; they're independent locks.
        assert (a / "daemon.lock").exists()
        assert (b / "daemon.lock").exists()
    finally:
        os.close(fd_a)
        os.close(fd_b)
