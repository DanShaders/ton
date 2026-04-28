# pyright: reportPrivateUsage=false
"""Fault injection: verify cleanup invariants when low-level calls
fail with unexpected exceptions.

What we're hunting: code paths that catch ``OSError`` (or some other
narrow class) in cleanup and leak when the failure is something else
(``PermissionError``, ``MemoryError``, ``RuntimeError``, or
``CancelledError``). Round-2 audit found one such spot in
``SubprocessRuntime._spawn``; this suite ratchets up coverage so
similar bugs surface as test failures, not silent leaks.
"""

import asyncio
from pathlib import Path

import pytest
from orchestrator import (
    Container,
    HostBinaryImage,
    Metadata,
    SubprocessRuntime,
    Workload,
    WorkloadSpec,
)
from orchestrator.lifecycle import Reaper
from orchestrator.resources import ResourceLimits

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


def _wl_with_cgroup(name: str = "x", *, memory_bytes: int = 100 * 1024 * 1024) -> Workload:
    return Workload(
        metadata=Metadata(name=name, namespace="default"),
        spec=WorkloadSpec(
            containers=[
                Container(
                    name="c",
                    image=HostBinaryImage(path="/bin/sleep"),
                    command=["60"],
                    resources=ResourceLimits(memory_bytes=memory_bytes),
                )
            ],
        ),
    )


@pytest.mark.parametrize(
    "exc",
    [
        OSError("simulated mkdir failure"),
        PermissionError("simulated permissions"),
        RuntimeError("unexpected runtime fault"),
    ],
)
async def test_spawn_cleanup_on_various_subprocess_exceptions(
    tmp_path: Path,
    reaper: Reaper,
    monkeypatch: pytest.MonkeyPatch,
    exc: BaseException,
):
    """Inject various exceptions inside ``create_subprocess_exec``.
    The cgroup directory must NOT survive — the only correct cleanup
    pattern is the BaseException-safe try/finally, not an
    ``except OSError`` that misses RuntimeError, PermissionError, etc.
    """
    rt = SubprocessRuntime(
        state_dir=tmp_path / "state",
        cgroup_root=tmp_path / "cgroup",
        reaper=reaper,
    )

    async def _raise(*_args: object, **_kwargs: object) -> None:
        raise exc

    monkeypatch.setattr(asyncio, "create_subprocess_exec", _raise)

    cgroup_dir = tmp_path / "cgroup" / "wl-default-faulty"
    with pytest.raises(type(exc)):
        _ = await rt.apply(_wl_with_cgroup("faulty"))

    assert not cgroup_dir.exists(), (
        f"cgroup_dir leaked after {type(exc).__name__}; cleanup must use try/finally, not except OSError"
    )


async def test_spawn_cleanup_on_cancellation(
    tmp_path: Path,
    reaper: Reaper,
    monkeypatch: pytest.MonkeyPatch,
):
    """CancelledError is a BaseException, not Exception. Past code that
    used ``except Exception:`` for cleanup would let cancellation skip
    the cleanup. This test pins the BaseException-safe behavior."""
    rt = SubprocessRuntime(
        state_dir=tmp_path / "state",
        cgroup_root=tmp_path / "cgroup",
        reaper=reaper,
    )

    async def _cancel(*_args: object, **_kwargs: object) -> None:
        raise asyncio.CancelledError()

    monkeypatch.setattr(asyncio, "create_subprocess_exec", _cancel)

    cgroup_dir = tmp_path / "cgroup" / "wl-default-cancel"
    with pytest.raises(asyncio.CancelledError):
        _ = await rt.apply(_wl_with_cgroup("cancel"))

    assert not cgroup_dir.exists()


async def test_spawn_cleanup_when_cgroup_files_unwritable(
    tmp_path: Path,
    reaper: Reaper,
):
    """If a cgroup limit-file write fails (permission, ENOSPC, etc.),
    ``_maybe_create_cgroup`` already swallows the OSError, removes the
    dir, and returns None. Verify the rest of the apply still succeeds
    (resource limits silently skipped) without leaving partial state.
    """
    rt = SubprocessRuntime(
        state_dir=tmp_path / "state",
        cgroup_root=tmp_path / "cgroup",
        reaper=reaper,
    )
    # Make cgroup_root read-only so mkdir under it fails with PermissionError.
    cgroup_root = tmp_path / "cgroup"
    cgroup_root.mkdir()
    cgroup_root.chmod(0o500)
    try:
        async with rt.running():
            # Should still succeed — cgroup placement is best-effort.
            wl = _wl_with_cgroup("readonly")
            try:
                status = await rt.apply(wl)
            except PermissionError:
                # Some kernels surface mkdir failure differently — that's
                # acceptable too as long as no debris is left.
                pass
            else:
                # Apply succeeded: the workload is running without a cgroup.
                assert status.phase in {"Running", "Pending"}
                await rt.delete(namespace="default", name="readonly")
            await rt.shutdown()
    finally:
        cgroup_root.chmod(0o700)
    # No partial dir left under cgroup_root.
    survivors = list(cgroup_root.iterdir())
    assert survivors == [], f"unexpected cgroup debris: {survivors}"
