# pyright: reportPrivateUsage=false
"""Fault-injection tests pinning the exception-safety contract of runs.py.

Each test forces a specific failure mode and asserts:

- caller sees the expected exception (or ``None`` for lazy-boot) within
  bounded time — a hang fails the ``asyncio.wait_for`` around the call;
- no observable state from the failed attempt survives: SQLite row,
  filesystem artifact, podman container, port allocation.

The reply-invariant test (``test_reply_invariant_*``) drives a handler
via an unprotected path — one the original per-handler ``try/except``
did not cover. Pre-refactor, the exception would be swallowed by the
actor loop's catch-all and the caller would hang forever on ``await reply``.
"""

import asyncio

import pytest
from daemon.models import NodeTarget, RunStatus
from daemon.models import TestMetadata as _TestMetadata
from daemon.runs import RunStartFailed
from daemon.services import prometheus_container_name
from daemon.testing import Rig

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]

_BOUNDED = 5.0  # virtual seconds — test fails if any "should complete" call exceeds this


def _md() -> _TestMetadata:
    return _TestMetadata(nodes=[NodeTarget(name="n0", address="127.0.0.1:5000")])


def _assert_no_residue(rig: Rig, run_id: str) -> None:
    """Nothing from a failed ``register`` should survive."""
    assert not rig.compose.is_running(prometheus_container_name(run_id))
    assert rig.manager._ports.allocated_ports() == set()
    config_dir = rig.instance_dir / "runs" / run_id / "config"
    assert not (config_dir / "prometheus.yml").exists()
    assert not (config_dir / "targets.json").exists()
    assert rig.faulty_provisioning is not None
    assert not rig.faulty_provisioning.datasource_file(run_id).exists()


# --------------------------------------------------------------------- shutdown


async def test_shutdown_completes_when_stop_container_raises(fault_rig: Rig):
    """_on_shutdown must swallow podman failures — the daemon is tearing down
    and the systemd cgroup / podman --rm are the cleanup backstop."""
    for run_id in ("a", "b", "c"):
        _ = await fault_rig.manager.register(run_id, _md())
    fault_rig.compose.down_should_fail = True  # sticky: every down raises

    _ = await asyncio.wait_for(fault_rig.manager.shutdown(), timeout=_BOUNDED)


async def test_shutdown_completes_when_set_run_status_raises(fault_rig: Rig):
    """_on_shutdown must swallow SQLite failures too."""
    _ = await fault_rig.manager.register("r1", _md())
    assert fault_rig.hooks is not None
    fault_rig.hooks.fail_next_set_status[RunStatus.DORMANT] = RuntimeError("sqlite broke")

    _ = await asyncio.wait_for(fault_rig.manager.shutdown(), timeout=_BOUNDED)


# --------------------------------------------------------------- register rollback


async def test_register_rollback_on_datasource_write_failure(fault_rig: Rig):
    """Filesystem step (after prom config, before SQLite) fails — everything
    earlier in the sequence must be rolled back."""
    assert fault_rig.faulty_provisioning is not None
    fault_rig.faulty_provisioning.fail_next_write = RuntimeError("datasource write broke")

    with pytest.raises(RuntimeError, match="datasource write broke"):
        _ = await asyncio.wait_for(fault_rig.manager.register("r1", _md()), timeout=_BOUNDED)

    assert await fault_rig.storage.get_run_metadata("r1") is None
    _assert_no_residue(fault_rig, "r1")


async def test_register_rollback_on_db_register_failure(fault_rig: Rig):
    """SQLite step fails — filesystem writes before it must be undone."""
    assert fault_rig.hooks is not None
    fault_rig.hooks.fail_next_register_run = RuntimeError("sqlite broke")

    with pytest.raises(RuntimeError, match="sqlite broke"):
        _ = await asyncio.wait_for(fault_rig.manager.register("r1", _md()), timeout=_BOUNDED)

    assert await fault_rig.storage.get_run_metadata("r1") is None
    _assert_no_residue(fault_rig, "r1")


async def test_register_rollback_on_container_start_cleans_datasource(fault_rig: Rig):
    """The canonical bug: pre-refactor, ``_start_container`` failure left
    the Grafana datasource YAML behind because it wasn't part of the
    per-handler rollback."""
    fault_rig.compose.up_should_fail = True

    with pytest.raises(RunStartFailed):
        _ = await asyncio.wait_for(fault_rig.manager.register("r1", _md()), timeout=_BOUNDED)

    # DB row flipped to DORMANT by rollback, FS + podman fully cleaned up.
    row = await fault_rig.storage.get_run_metadata("r1")
    assert row is not None and row.status == RunStatus.DORMANT
    assert not fault_rig.compose.is_running(prometheus_container_name("r1"))
    assert fault_rig.manager._ports.allocated_ports() == set()
    assert fault_rig.faulty_provisioning is not None
    assert not fault_rig.faulty_provisioning.datasource_file("r1").exists()


# ------------------------------------------------------------- lazy-boot rollback


async def test_lazy_boot_cleans_prom_config_on_failure(fault_rig: Rig):
    """Archive lazy-boot writes a fresh empty prom config before calling
    compose.up; on failure that file must be unlinked (pre-refactor gap)."""
    _ = await fault_rig.manager.register("r1", _md())
    await fault_rig.manager.release("r1")
    config_path = fault_rig.instance_dir / "runs" / "r1" / "config" / "prometheus.yml"
    config_path.unlink()  # simulate clean slate on disk
    targets_path = fault_rig.instance_dir / "runs" / "r1" / "config" / "targets.json"
    targets_path.unlink()

    fault_rig.compose.up_should_fail = True
    port = await asyncio.wait_for(fault_rig.manager.ensure_queryable("r1"), timeout=_BOUNDED)

    assert port is None
    assert not config_path.exists()
    assert not targets_path.exists()
    assert fault_rig.manager._ports.allocated_ports() == set()


# -------------------------------------------------------------- reply invariant


async def test_reply_invariant_unprotected_exception_does_not_hang(fault_rig: Rig):
    """The final ``get_run_metadata`` read in ``_on_register`` was not wrapped
    by any per-handler ``try/except`` pre-refactor — a raise there was
    swallowed by the actor loop's catch-all and the caller hung forever
    on ``await reply``. The centralized dispatcher must route any
    handler exception to ``reply.set_exception`` regardless of where it
    originates."""
    assert fault_rig.hooks is not None
    fault_rig.hooks.fail_next_get_run_metadata = RuntimeError("unexpected db error")

    with pytest.raises(RuntimeError, match="unexpected db error"):
        _ = await asyncio.wait_for(fault_rig.manager.register("r1", _md()), timeout=_BOUNDED)

    # The failure happened after the DB row was inserted, so rollback
    # flipped it to DORMANT and cleaned up everything else.
    row = await fault_rig.storage.get_run_metadata("r1")
    assert row is not None and row.status == RunStatus.DORMANT
    _assert_no_residue(fault_rig, "r1")


# ----------------------------------------------------------- client-cancel compensation


async def _yield_for(n: int) -> None:
    """Give other tasks ``n`` scheduling slots to make progress."""
    for _ in range(n):
        await asyncio.sleep(0)


async def test_cancel_during_register_triggers_compensating_release(fault_rig: Rig):
    """If a caller cancels register(), the actor still completes the work; a
    background compensation task then releases the run so it doesn't sit LIVE
    until daemon shutdown."""
    gate = asyncio.Event()
    fault_rig.compose.pending_up_gate = gate

    register_task = asyncio.create_task(fault_rig.manager.register("r1", _md()))
    await _yield_for(5)  # let register reach compose.up

    _ = register_task.cancel()
    with pytest.raises(asyncio.CancelledError):
        _ = await register_task

    # Let the actor finish the in-flight register, then the compensation.
    gate.set()
    await _yield_for(50)

    # Run converged to DORMANT; container torn down; port released.
    row = await fault_rig.storage.get_run_metadata("r1")
    assert row is not None and row.status == RunStatus.DORMANT
    assert not fault_rig.compose.is_running(prometheus_container_name("r1"))
    assert fault_rig.manager._ports.allocated_ports() == set()


async def test_cancel_during_acquire_pin_triggers_compensating_release(fault_rig: Rig):
    """If a caller cancels acquire_query_pin during lazy-boot, the actor
    still boots and increments pins; compensation then releases the pin so
    the reaper can collect the archive container."""
    # Set up a dormant run so acquire_query_pin triggers a lazy-boot.
    _ = await fault_rig.manager.register("r1", _md())
    await fault_rig.manager.release("r1")
    assert fault_rig.manager._ports.allocated_ports() == set()

    gate = asyncio.Event()
    fault_rig.compose.pending_up_gate = gate

    acquire_task = asyncio.create_task(fault_rig.manager.acquire_query_pin("r1"))
    await _yield_for(5)  # let lazy-boot reach compose.up

    _ = acquire_task.cancel()
    with pytest.raises(asyncio.CancelledError):
        _ = await acquire_task

    gate.set()
    await _yield_for(50)

    # Pin count drained back to 0 — reaper can collect the archive now.
    snap = await fault_rig.manager.inspect("r1")
    assert snap is not None
    assert snap.pins == 0


# --------------------------------------------------------- escalated shutdown


async def test_shutdown_cancels_stuck_actor_when_reply_timeout_elapses(fault_rig: Rig):
    """``runs.shutdown(reply_timeout=...)`` force-cancels actors that don't
    respond in time. Partial state is acceptable — ``recover()`` reconciles
    on next boot, same as any abnormal termination backstop."""
    gate = asyncio.Event()  # never set — compose.up hangs
    fault_rig.compose.pending_up_gate = gate

    register_task = asyncio.create_task(fault_rig.manager.register("r1", _md()))
    await _yield_for(5)  # let register reach compose.up and wedge

    # Tight timeout — actor's in-flight compose.up will still be blocked.
    _ = await asyncio.wait_for(fault_rig.manager.shutdown(reply_timeout=0.1), timeout=_BOUNDED)

    # The register caller is still awaiting a reply that will never come:
    # ``asyncio.shield(reply)`` kept the reply alive across shutdown, but
    # the actor was force-cancelled so nobody will set it. In real IPC use
    # the caller task is also cancelled when the daemon exits (its socket
    # closes); simulate that here.
    _ = register_task.cancel()
    with pytest.raises(asyncio.CancelledError):
        _ = await register_task
