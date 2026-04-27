"""Startup-cleanup hygiene for ``orchestrator.agent.cleanup``.

The agent calls ``clean_state_dir`` at startup to mop up debris from
a previous unclean exit. The contract: every entry under
``state_dir/workloads/`` is removed, regardless of its kind (file,
directory, symlink, dangling symlink), without following symlinks
into untrusted targets.
"""

from pathlib import Path

import pytest
from orchestrator.agent.cleanup import clean_state_dir

pytestmark = [pytest.mark.asyncio]


async def test_clean_removes_dangling_symlink(tmp_path: Path):
    """A symlink whose target was removed (e.g. by a previous
    cleanup pass) must still be unlinked. ``is_symlink()`` returns
    True for dangling links; the cleanup short-circuits there
    before reaching the directory branch.
    """
    workloads = tmp_path / "workloads"
    workloads.mkdir()
    target = tmp_path / "missing-target"
    link = workloads / "stale-link"
    link.symlink_to(target)
    assert link.is_symlink()

    clean_state_dir(tmp_path)

    assert not link.exists() and not link.is_symlink(), (
        "dangling symlink survived clean_state_dir; cleanup must short-"
        "circuit on is_symlink() before any is_file()/is_dir() check"
    )


async def test_clean_does_not_follow_symlink_to_directory(tmp_path: Path):
    """A symlink to a real, populated directory must be unlinked
    *without* removing the target's contents. Otherwise a stale
    symlink in the agent's state dir could nuke arbitrary host
    paths on startup.
    """
    workloads = tmp_path / "workloads"
    workloads.mkdir()
    real_dir = tmp_path / "outside"
    real_dir.mkdir()
    sentinel = real_dir / "important.txt"
    _ = sentinel.write_text("don't delete me")
    link = workloads / "link-to-outside"
    link.symlink_to(real_dir)

    clean_state_dir(tmp_path)

    assert not link.exists() and not link.is_symlink(), "symlink not removed"
    assert sentinel.exists() and sentinel.read_text() == "don't delete me", (
        "clean_state_dir followed a symlink and deleted contents outside its scope"
    )


async def test_clean_removes_normal_directory(tmp_path: Path):
    """Sanity: a real subdirectory under workloads/ is removed."""
    workloads = tmp_path / "workloads"
    workloads.mkdir()
    sub = workloads / "ns-name"
    sub.mkdir()
    _ = (sub / "junk.txt").write_text("debris")

    clean_state_dir(tmp_path)

    assert not sub.exists()
