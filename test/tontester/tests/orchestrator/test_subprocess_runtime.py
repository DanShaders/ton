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


async def test_apply_after_running_exit_raises(tmp_path: Path):
    """``apply()`` after the runtime's ``running()`` context has
    exited must raise rather than silently spawning a leak. The
    supervisor pattern makes this the natural shape: the
    AsyncExitStack of ``running()`` cancels every supervisor on
    exit, sets ``_closed = True``, and any further ``apply`` checks
    ``_closed`` synchronously at the top.
    """
    rt = SubprocessRuntime(state_dir=tmp_path / "state")
    async with rt.running():
        pass

    with pytest.raises(RuntimeError, match="closed"):
        _ = await rt.apply(_wl("alpha"))
    assert ("default", "alpha") not in rt._procs


async def test_supervisor_cancellation_cleans_cgroup_dir(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
):
    """Round-5 cluster (#1, #4, #7): the cgroup_dir cleanup hazards
    around ``_restart_locked`` / ``_wait_exit`` are eliminated by
    the supervisor pattern — each workload's lifetime is one async
    context chain (``owned_path`` → ``owned_process``). Cancelling
    the supervisor task triggers RAII unwind that cleans the
    cgroup, regardless of where in the chain the cancel landed.

    Verify by spawning a workload with a hung process.wait, then
    deleting (which cancels the supervisor). The cgroup must be gone.
    """
    rt = SubprocessRuntime(
        state_dir=tmp_path / "state",
        cgroup_root=tmp_path / "cgroup",
    )
    wl = _wl_with_cgroup("alpha")
    cgroup_dir = tmp_path / "cgroup" / "wl-default-alpha"

    async with rt.running():
        _ = await rt.apply(wl)
        assert cgroup_dir.exists(), "apply should have created the cgroup_dir"

        proc = rt._procs[("default", "alpha")]
        process = proc.process
        real_wait = process.wait
        real_kill = process.kill

        # Patch the process so the supervisor's await process.wait()
        # never returns naturally; cancellation is the only exit path.
        async def _hang() -> int:
            _ = await asyncio.Event().wait()
            return 0

        monkeypatch.setattr(process, "wait", _hang)
        monkeypatch.setattr(process, "terminate", lambda: None)
        monkeypatch.setattr(process, "kill", lambda: None)

        await rt.delete(namespace="default", name="alpha")

        assert not cgroup_dir.exists(), (
            "cgroup_dir leaked through supervisor cancellation; "
            "owned_path's RAII should have cleaned it"
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
    """Round-2 finding 1.1: if ``create_subprocess_exec`` raises
    ``CancelledError`` (or any other BaseException), the outer
    ``owned_path`` for the cgroup_dir cleans up. The supervisor's
    chained ``async with`` makes this automatic — no narrow
    ``except OSError`` to miss the cancellation case.
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

    async with rt.running():
        with pytest.raises((asyncio.CancelledError, Exception)):
            _ = await rt.apply(wl)

        assert not cgroup_dir.exists(), (
            "cgroup_dir leaked through cancellation; the supervisor's "
            "owned_path RAII should have cleaned it"
        )
