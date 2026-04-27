# pyright: reportPrivateUsage=false
"""SubprocessRuntime concurrency + cleanup invariants.

Uses short-lived ``/bin/sleep`` workloads; tests that need to inject
faults monkey-patch ``asyncio.create_subprocess_exec``.
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
from orchestrator.resources import ResourceLimits
from orchestrator.testing import wait_for_asyncio_idle

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


def _wl(name: str = "x") -> Workload:
    return Workload(
        metadata=Metadata(name=name, namespace="default"),
        spec=WorkloadSpec(
            containers=[
                Container(name="c", image=HostBinaryImage(path="/bin/sleep"), command=["60"])
            ],
        ),
    )


def _wl_with_cgroup(name: str = "x") -> Workload:
    """Workload with a resource limit so SubprocessRuntime creates a cgroup_dir."""
    return Workload(
        metadata=Metadata(name=name, namespace="default"),
        spec=WorkloadSpec(
            containers=[
                Container(
                    name="c",
                    image=HostBinaryImage(path="/bin/sleep"),
                    command=["60"],
                    resources=ResourceLimits(memory_bytes=100 * 1024 * 1024),
                )
            ],
        ),
    )


async def test_apply_blocked_on_lock_during_close_does_not_spawn(tmp_path: Path):
    """apply() that's already past the ``self._closed`` check but still
    awaiting the per-key lock when close() runs would, without the
    re-check inside the lock, spawn a new process that's never tracked
    by close()'s drain loop. The process leaks past the runtime's
    lifetime.
    """
    rt = SubprocessRuntime(state_dir=tmp_path / "state")
    wl = _wl("alpha")
    key = (wl.metadata.namespace, wl.metadata.name)

    # Prime the per-key lock and hold it externally so the apply blocks.
    lock = rt._lock_for(key)
    _ = await lock.acquire()

    apply_task = asyncio.create_task(rt.apply(wl), name="t.apply")
    # Drain the loop until apply is parked on the locked lock.
    await wait_for_asyncio_idle()

    # Close while apply is still blocked.
    close_task = asyncio.create_task(rt.close(), name="t.close")
    await wait_for_asyncio_idle()

    # Release; apply resumes inside the closed runtime.
    lock.release()

    with pytest.raises(RuntimeError, match="closed"):
        _ = await apply_task

    await close_task
    # No process registered — the apply must have bailed before spawn.
    assert key not in rt._procs


async def test_stop_locked_cleans_cgroup_dir_on_cancellation(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
):
    """Bug (audit round 4): ``_stop_locked`` does ``terminate()``, then
    ``await asyncio.wait_for(proc.process.wait(), grace)``, then
    ``_safe_rmtree(proc.cgroup_dir)``. If the awaiting task is cancelled
    at any of those awaits, ``CancelledError`` propagates out and the
    cgroup_dir cleanup is skipped. ``_spawn`` was hardened with
    ``try/finally + spawned`` flag specifically to fix this shape;
    ``_stop_locked`` was missed.

    Trigger: monkey-patch ``proc.process.wait`` to hang forever, then
    cancel the in-flight ``rt.delete()`` task. Verify the cgroup_dir is
    gone after cancellation propagates.
    """
    rt = SubprocessRuntime(
        state_dir=tmp_path / "state",
        cgroup_root=tmp_path / "cgroup",
    )
    wl = _wl_with_cgroup("alpha")
    cgroup_dir = tmp_path / "cgroup" / "wl-default-alpha"

    _ = await rt.apply(wl)
    assert cgroup_dir.exists(), "apply should have created the cgroup_dir"

    proc = rt._procs[("default", "alpha")]
    process = proc.process
    real_wait = process.wait
    real_kill = process.kill

    # Patch the process's wait to hang so wait_for never completes
    # naturally; cancellation is the only way out, exercising the
    # not-yet-hardened cleanup path.
    async def _hang() -> int:
        _ = await asyncio.Event().wait()
        return 0

    monkeypatch.setattr(process, "wait", _hang)
    # Suppress real signals so cancellation is the only termination path.
    monkeypatch.setattr(process, "terminate", lambda: None)
    monkeypatch.setattr(process, "kill", lambda: None)

    delete_task = asyncio.create_task(rt.delete(namespace="default", name="alpha"), name="t.delete")
    await wait_for_asyncio_idle()

    _ = delete_task.cancel()
    with pytest.raises(asyncio.CancelledError):
        _ = await delete_task

    assert not cgroup_dir.exists(), (
        "cgroup_dir leaked through _stop_locked cancellation; needs "
        "try/finally around _safe_rmtree (mirroring the _spawn pattern)"
    )

    # Cleanup the real /bin/sleep so it doesn't outlive the test —
    # use the originals captured before monkeypatching.
    try:
        real_kill()
    except ProcessLookupError, OSError:
        pass
    _ = await real_wait()


async def test_spawn_cancellation_cleans_cgroup_dir(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
):
    """Round-2 finding 1.1: ``_spawn`` only catches OSError around
    ``create_subprocess_exec``. A cancellation (or any other
    BaseException) leaves the cgroup_dir on disk for the rest of the
    daemon's lifetime.
    """
    rt = SubprocessRuntime(
        state_dir=tmp_path / "state",
        cgroup_root=tmp_path / "cgroup",
    )
    wl = _wl_with_cgroup("alpha")
    cgroup_dir = tmp_path / "cgroup" / "wl-default-alpha"

    async def _raise_cancelled(*_args: object, **_kwargs: object) -> None:
        raise asyncio.CancelledError()

    monkeypatch.setattr(asyncio, "create_subprocess_exec", _raise_cancelled)

    with pytest.raises(asyncio.CancelledError):
        _ = await rt.apply(wl)

    # Without the fix the cgroup_dir survives the cancellation.
    assert not cgroup_dir.exists(), (
        "cgroup_dir leaked through cancellation; _spawn must clean up on BaseException"
    )
