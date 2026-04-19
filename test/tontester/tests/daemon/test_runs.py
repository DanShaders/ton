"""Tests for :class:`daemon.runs.RunsSupervisor` + :class:`RunActor`.

State access is via :meth:`RunsSupervisor.inspect`, which returns an
immutable :class:`RunStateSnapshot`; the actor's private fields are
never touched from outside. Time advances via ``VirtualClock``, so
idling a handle is just ``await asyncio.sleep(...)``.
"""

import asyncio

import pytest
from daemon.models import NodeTarget, RunStatus
from daemon.models import TestMetadata as _TestMetadata
from daemon.runs import RunAlreadyActive, RunStartFailed
from daemon.services import prometheus_container_name
from daemon.testing import Rig

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


def _md(*nodes: str) -> _TestMetadata:
    return _TestMetadata(
        description="test",
        nodes=[NodeTarget(name=n, address=f"127.0.0.1:{5000 + i}") for i, n in enumerate(nodes)],
    )


async def _owner(rig: Rig, run_id: str) -> str | None:
    snap = await rig.manager.inspect(run_id)
    return None if snap is None else snap.owner


# --------------------------------------------------------------------- register


async def test_register_fresh_run(rig: Rig):
    _ = await rig.manager.register("r1", _md("node-0", "node-1"))

    row = await rig.storage.get_run_metadata("r1")
    assert row is not None
    assert row.status == RunStatus.LIVE
    assert 9100 <= row.host_port <= 9199

    snap = await rig.manager.inspect("r1")
    assert snap is not None
    assert snap.owner == "ws"
    assert snap.status == "live"
    assert snap.host_port == row.host_port

    assert rig.compose.is_running(prometheus_container_name("r1"))
    assert rig.provisioning.datasource_file("r1").exists()

    targets = (rig.instance_dir / "runs" / "r1" / "config" / "targets.json").read_text()
    assert "node-0" in targets and "node-1" in targets


async def test_register_rejects_when_ws_owner_holds_run(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    with pytest.raises(RunAlreadyActive):
        _ = await rig.manager.register("r1", _md("n0"))


async def test_register_takes_over_archive_owned_handle(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    port = await rig.manager.ensure_queryable("r1")
    assert port is not None
    assert await _owner(rig, "r1") == "archive"
    container_name = prometheus_container_name("r1")
    assert rig.compose.is_running(container_name)

    _ = await rig.manager.register("r1", _md("n0"))
    assert await _owner(rig, "r1") == "ws"
    actions = [a for a, n in rig.compose.call_log if n == container_name]
    assert "down" in actions and actions.count("up") >= 2


async def test_register_rolls_back_on_start_failure(rig: Rig):
    rig.compose.up_should_fail = True

    with pytest.raises(RunStartFailed):
        _ = await rig.manager.register("r1", _md("n0"))

    assert not rig.compose.is_running(prometheus_container_name("r1"))
    assert await _owner(rig, "r1") is None
    row = await rig.storage.get_run_metadata("r1")
    assert row is not None
    assert row.status == RunStatus.DORMANT


async def test_register_preserves_start_time_on_resume(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    first = await rig.storage.get_run_metadata("r1")
    assert first is not None
    start_before = first.start_time

    await rig.manager.release("r1")
    await asyncio.sleep(0.01)

    _ = await rig.manager.register("r1", _md("n0"))
    second = await rig.storage.get_run_metadata("r1")
    assert second is not None
    assert second.start_time == start_before
    assert second.end_time is None
    assert second.status == RunStatus.LIVE


# ------------------------------------------------------------------------ release


async def test_release_transitions_to_dormant(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    assert rig.compose.is_running(prometheus_container_name("r1"))

    await rig.manager.release("r1")

    row = await rig.storage.get_run_metadata("r1")
    assert row is not None
    assert row.status == RunStatus.DORMANT
    assert not rig.compose.is_running(prometheus_container_name("r1"))
    assert await _owner(rig, "r1") is None


async def test_release_is_noop_for_archive_owned(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    _ = await rig.manager.ensure_queryable("r1")
    assert await _owner(rig, "r1") == "archive"

    await rig.manager.release("r1")

    assert await _owner(rig, "r1") == "archive"
    assert rig.compose.is_running(prometheus_container_name("r1"))


# ----------------------------------------------------------------- ensure_queryable


async def test_ensure_queryable_unknown_run_returns_none(rig: Rig):
    assert await rig.manager.ensure_queryable("nope") is None


async def test_ensure_queryable_for_live_run_returns_live_port(rig: Rig):
    row = await rig.manager.register("r1", _md("n0"))
    port = await rig.manager.ensure_queryable("r1")
    assert port == row.host_port


async def test_ensure_queryable_lazy_boots_dormant_run(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    assert await _owner(rig, "r1") is None

    port = await rig.manager.ensure_queryable("r1")

    assert port is not None
    assert await _owner(rig, "r1") == "archive"
    assert rig.compose.is_running(prometheus_container_name("r1"))


async def test_ensure_queryable_retries_after_boot_failure(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")

    rig.compose.up_should_fail = True
    first = await rig.manager.ensure_queryable("r1")
    assert first is None
    assert await _owner(rig, "r1") is None

    second = await rig.manager.ensure_queryable("r1")
    assert second is not None
    assert rig.compose.is_running(prometheus_container_name("r1"))


# ------------------------------------------------------------------------ recovery


async def test_recover_flips_live_rows_to_dormant(rig: Rig):
    await rig.storage.register_run("r1", _md("n0"), host_port=9150)
    await rig.storage.register_run("r2", _md("n0"), host_port=9151)

    await rig.manager.recover()

    r1 = await rig.storage.get_run_metadata("r1")
    r2 = await rig.storage.get_run_metadata("r2")
    assert r1 is not None and r1.status == RunStatus.DORMANT
    assert r2 is not None and r2.status == RunStatus.DORMANT


async def test_recover_stops_leftover_prom_containers(rig: Rig):
    """Simulate a crashed previous daemon: DB row LIVE + podman container
    running + no in-memory actor. recover() should stop the container and
    flip the row to DORMANT."""
    from daemon.models import Service
    from daemon.testing import FakeContainer

    await rig.storage.register_run("r1", _md("n0"), host_port=9150)
    rig.compose.containers[prometheus_container_name("r1")] = FakeContainer(
        service=Service(name=prometheus_container_name("r1"), image="x")
    )

    await rig.manager.recover()

    assert not rig.compose.is_running(prometheus_container_name("r1"))
    row = await rig.storage.get_run_metadata("r1")
    assert row is not None
    assert row.status == RunStatus.DORMANT


async def test_recover_removes_orphan_grafana_datasources(rig: Rig):
    rig.provisioning.write_run_datasource("ghost")
    assert rig.provisioning.datasource_file("ghost").exists()

    await rig.manager.recover()

    assert not rig.provisioning.datasource_file("ghost").exists()


async def test_recover_restores_missing_datasources(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    rig.provisioning.datasource_file("r1").unlink()

    await rig.manager.recover()

    assert rig.provisioning.datasource_file("r1").exists()


# ----------------------------------------------------------------------- reaper


async def test_reaper_stops_idle_archive_container(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    _ = await rig.manager.ensure_queryable("r1")

    # Advance past TTL via virtual clock; reap pass sees it as idle.
    await asyncio.sleep(rig.manager.archive_idle_ttl_seconds + 1)
    await rig.manager.run_reaper_pass()
    await _drain_inbox(rig, "r1")

    assert await _owner(rig, "r1") is None
    assert not rig.compose.is_running(prometheus_container_name("r1"))


async def test_reaper_does_not_stop_fresh_archive(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    _ = await rig.manager.ensure_queryable("r1")

    await rig.manager.run_reaper_pass()
    await _drain_inbox(rig, "r1")

    assert await _owner(rig, "r1") == "archive"
    assert rig.compose.is_running(prometheus_container_name("r1"))


async def test_reaper_skips_pinned_handle(rig: Rig):
    """Reaper must not stop a container with in-flight queries."""
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    port = await rig.manager.acquire_query_pin("r1")
    assert port is not None

    # Make the handle appear idle; a pin must still protect it.
    await asyncio.sleep(rig.manager.archive_idle_ttl_seconds + 1)
    await rig.manager.run_reaper_pass()
    await _drain_inbox(rig, "r1")

    snap = await rig.manager.inspect("r1")
    assert snap is not None and snap.owner == "archive" and snap.pins == 1
    assert rig.compose.is_running(prometheus_container_name("r1"))

    await rig.manager.release_query_pin("r1")
    await rig.manager.run_reaper_pass()
    await _drain_inbox(rig, "r1")
    assert await _owner(rig, "r1") is None


async def test_reaper_does_not_stop_ws_owned(rig: Rig):
    """A ws-owned run is never reaped regardless of idle time."""
    _ = await rig.manager.register("r1", _md("n0"))

    await asyncio.sleep(rig.manager.archive_idle_ttl_seconds + 5)
    await rig.manager.run_reaper_pass()
    await _drain_inbox(rig, "r1")

    assert await _owner(rig, "r1") == "ws"
    assert rig.compose.is_running(prometheus_container_name("r1"))


# ---------------------------------------------------------------------- shutdown


async def test_shutdown_archives_all_live_runs(rig: Rig):
    for run_id in ("a", "b", "c"):
        _ = await rig.manager.register(run_id, _md("n0"))
    for run_id in ("a", "b", "c"):
        assert rig.compose.is_running(prometheus_container_name(run_id))

    await rig.manager.shutdown()

    for run_id in ("a", "b", "c"):
        row = await rig.storage.get_run_metadata(run_id)
        assert row is not None
        assert row.status == RunStatus.DORMANT
        assert not rig.compose.is_running(prometheus_container_name(run_id))


async def test_register_after_shutdown_is_rejected(rig: Rig):
    await rig.manager.shutdown()
    with pytest.raises(Exception):
        _ = await rig.manager.register("r1", _md("n0"))
    assert not rig.compose.is_running(prometheus_container_name("r1"))


async def test_shutdown_completes_even_with_in_flight_register(rig: Rig):
    """Shutdown + concurrent register should both settle cleanly — no orphan.

    With the actor model, messages on the same run_id are strictly serialized:
    register runs to completion (container up), then _Shutdown runs (container
    down). Final state is clean.
    """
    gate = asyncio.Event()
    rig.compose.pending_up_gate = gate

    register_task = asyncio.create_task(rig.manager.register("r1", _md("n0")))
    # Let register reach compose.up.
    for _ in range(5):
        await asyncio.sleep(0)

    shutdown_task = asyncio.create_task(rig.manager.shutdown())
    for _ in range(5):
        await asyncio.sleep(0)

    gate.set()
    _ = await register_task
    await shutdown_task

    assert not rig.compose.is_running(prometheus_container_name("r1"))


# -------------------------------------------------------------------- port picker


async def test_port_picker_prefers_existing_port_for_known_run(rig: Rig):
    r = await rig.manager.register("r1", _md("n0"))
    old_port = r.host_port
    await rig.manager.release("r1")

    r2 = await rig.manager.register("r1", _md("n0"))
    assert r2.host_port == old_port


async def test_port_picker_gives_fresh_port_when_preferred_is_taken(rig: Rig):
    r1 = await rig.manager.register("r1", _md("n0"))
    r2 = await rig.manager.register("r2", _md("n0"))
    assert r2.host_port != r1.host_port


async def test_failed_register_then_retry_succeeds(rig: Rig):
    """With per-run serialization, a failed register followed by a retry is
    a straight sequence — no CAS needed, no state to interleave. The test
    just pins that behavior.
    """
    rig.compose.up_should_fail = True
    with pytest.raises(RunStartFailed):
        _ = await rig.manager.register("r1", _md("n0"))

    row = await rig.manager.register("r1", _md("n0"))
    assert row.status == RunStatus.LIVE
    assert rig.compose.is_running(prometheus_container_name("r1"))


# ---- helpers


async def _drain_inbox(_rig: Rig, _run_id: str) -> None:
    """Yield enough for an actor to finish processing its queued messages."""
    for _ in range(20):
        await asyncio.sleep(0)
