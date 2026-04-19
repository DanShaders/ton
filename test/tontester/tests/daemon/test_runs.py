"""Tests for :class:`daemon.runs.RunsManager`.

Every test uses the ``rig`` fixture from ``conftest.py``, which wires a
real :class:`SQLiteStorage`/ :class:`GrafanaProvisioning` to a
:class:`FakeCompose` that tracks containers in-memory.

Two time-sensitive areas — the idle reaper and the shutdown-vs-boot race —
are driven without real sleeps by:

* calling ``manager.run_reaper_pass()`` directly after rewriting
  ``handle.last_access`` to simulate idle time,
* using ``FakeCompose.pending_up_gate`` to hold ``up`` mid-call so a test
  can interleave shutdown / release before the "container" comes up.
"""

# pyright: reportPrivateUsage=false
# ``_containers`` on RunsManager is the exact internal state these tests
# exercise; poking it is intentional.

import asyncio

import pytest
from daemon.runs import RunAlreadyActive, RunStartFailed
from daemon.services import prometheus_container_name
from daemon.storage import NodeTarget, RunStatus
from daemon.storage import TestMetadata as _TestMetadata
from daemon.testing import Rig

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


def _md(*nodes: str) -> _TestMetadata:
    return _TestMetadata(
        description="test",
        nodes=[NodeTarget(name=n, address=f"127.0.0.1:{5000 + i}") for i, n in enumerate(nodes)],
    )


# --------------------------------------------------------------------- register


async def test_register_fresh_run(rig: Rig):
    _ = await rig.manager.register("r1", _md("node-0", "node-1"))

    # DB row is LIVE with allocated port.
    row = await rig.storage.get_run_metadata("r1")
    assert row is not None
    assert row.status == RunStatus.LIVE
    assert 9100 <= row.host_port <= 9199

    # In-memory handle matches DB and is ws-owned.
    handle = rig.manager._containers["r1"]
    assert handle.owner == "ws"
    assert handle.host_port == row.host_port

    # Container was "started" via compose.up.
    assert rig.compose.is_running(prometheus_container_name("r1"))

    # Datasource YAML was written.
    assert rig.provisioning.datasource_file("r1").exists()

    # Scrape targets file reflects the registered nodes.
    targets = (rig.instance_dir / "runs" / "r1" / "config" / "targets.json").read_text()
    assert "node-0" in targets and "node-1" in targets


async def test_register_rejects_when_ws_owner_holds_run(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    with pytest.raises(RunAlreadyActive):
        _ = await rig.manager.register("r1", _md("n0"))


async def test_register_takes_over_archive_owned_handle(rig: Rig):
    # First: put the run into a DORMANT DB state, then lazy-boot it (archive).
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    port = await rig.manager.ensure_queryable("r1")
    assert port is not None
    assert rig.manager._containers["r1"].owner == "archive"
    old_container_name = prometheus_container_name("r1")
    # Sanity: the archive container exists.
    assert rig.compose.is_running(old_container_name)

    # Now register again — archive should be torn down and re-installed as ws.
    _ = await rig.manager.register("r1", _md("n0"))
    handle = rig.manager._containers["r1"]
    assert handle.owner == "ws"
    # The archive ``down`` and the new ``up`` are both in the call log.
    actions = [a for a, n in rig.compose.call_log if n == old_container_name]
    assert actions.count("down") >= 1 and actions.count("up") >= 2  # initial + takeover


async def test_register_rolls_back_on_start_failure(rig: Rig):
    rig.compose.up_should_fail = True

    with pytest.raises(RunStartFailed):
        _ = await rig.manager.register("r1", _md("n0"))

    # No container, no in-memory handle, DB says DORMANT.
    assert not rig.compose.is_running(prometheus_container_name("r1"))
    assert "r1" not in rig.manager._containers
    row = await rig.storage.get_run_metadata("r1")
    assert row is not None
    assert row.status == RunStatus.DORMANT


async def test_register_preserves_start_time_on_resume(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    first = await rig.storage.get_run_metadata("r1")
    assert first is not None
    start_before = first.start_time

    await rig.manager.release("r1")

    # A brief wait so any wall-clock delta would be visible.
    await asyncio.sleep(0.01)

    _ = await rig.manager.register("r1", _md("n0"))
    second = await rig.storage.get_run_metadata("r1")
    assert second is not None
    assert second.start_time == start_before
    assert second.end_time is None  # cleared on resume
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
    assert "r1" not in rig.manager._containers


async def test_release_is_noop_for_archive_owned(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    _ = await rig.manager.ensure_queryable("r1")  # lazy-boot archive
    assert rig.manager._containers["r1"].owner == "archive"

    await rig.manager.release("r1")  # should NOT stop the archive container

    assert rig.manager._containers["r1"].owner == "archive"
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
    assert "r1" not in rig.manager._containers

    port = await rig.manager.ensure_queryable("r1")

    assert port is not None
    handle = rig.manager._containers["r1"]
    assert handle.owner == "archive"
    assert rig.compose.is_running(prometheus_container_name("r1"))


async def test_ensure_queryable_retries_after_boot_failure(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")

    # First lazy-boot fails; handle should be removed so a second call retries.
    rig.compose.up_should_fail = True
    first = await rig.manager.ensure_queryable("r1")
    assert first is None
    assert "r1" not in rig.manager._containers  # no stuck failed handle

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
    # Register via the real code so we also persist a DB row, then simulate a
    # daemon crash by directly forcing the DB row back to LIVE and keeping the
    # container up in the fake.
    _ = await rig.manager.register("r1", _md("n0"))
    # Now drop the in-memory tracking (as if the daemon had died) but leave
    # the container "running" in the fake.
    _ = rig.manager._containers.pop("r1")
    # And set DB status back to LIVE manually so recover has work to do.
    await rig.storage.set_run_status("r1", RunStatus.LIVE)

    assert rig.compose.is_running(prometheus_container_name("r1"))

    await rig.manager.recover()

    assert not rig.compose.is_running(prometheus_container_name("r1"))
    row = await rig.storage.get_run_metadata("r1")
    assert row is not None
    assert row.status == RunStatus.DORMANT


async def test_recover_removes_orphan_grafana_datasources(rig: Rig):
    # Datasource YAML without any DB row.
    rig.provisioning.write_run_datasource("ghost")
    assert rig.provisioning.datasource_file("ghost").exists()

    await rig.manager.recover()

    assert not rig.provisioning.datasource_file("ghost").exists()


async def test_recover_restores_missing_datasources(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    # Simulate someone hand-deleting the YAML.
    rig.provisioning.datasource_file("r1").unlink()
    assert not rig.provisioning.datasource_file("r1").exists()

    await rig.manager.recover()

    assert rig.provisioning.datasource_file("r1").exists()


# ----------------------------------------------------------------------- reaper


async def test_reaper_stops_idle_archive_container(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    _ = await rig.manager.ensure_queryable("r1")  # archive boot
    handle = rig.manager._containers["r1"]

    # Mark the handle idle beyond the TTL.
    handle.last_access -= rig.manager.archive_idle_ttl_seconds + 1

    await rig.manager.run_reaper_pass()

    assert "r1" not in rig.manager._containers
    assert not rig.compose.is_running(prometheus_container_name("r1"))


async def test_reaper_does_not_stop_fresh_archive(rig: Rig):
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    _ = await rig.manager.ensure_queryable("r1")  # fresh archive, not idle yet

    await rig.manager.run_reaper_pass()

    assert "r1" in rig.manager._containers
    assert rig.compose.is_running(prometheus_container_name("r1"))


async def test_reaper_skips_pinned_handle(rig: Rig):
    """Reaper must not stop a container with in-flight queries.

    Pins are incremented by ``acquire_query_pin`` (PromProxy uses this)
    and decremented by ``release_query_pin``. Between those calls, the
    reaper's last_access check may flag the handle as idle — but pins
    protect it.
    """
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    port = await rig.manager.acquire_query_pin("r1")
    assert port is not None
    handle = rig.manager._containers["r1"]
    assert handle.pins == 1

    # Simulate an extended in-flight query: handle goes idle, reaper would
    # normally reap, but pins > 0 saves it.
    handle.last_access -= rig.manager.archive_idle_ttl_seconds + 5
    await rig.manager.run_reaper_pass()
    assert "r1" in rig.manager._containers
    assert rig.compose.is_running(prometheus_container_name("r1"))

    # Release the pin; now reaper can do its job.
    await rig.manager.release_query_pin("r1")
    assert rig.manager._containers["r1"].pins == 0
    await rig.manager.run_reaper_pass()
    assert "r1" not in rig.manager._containers


async def test_reaper_does_not_stop_ws_owned_after_takeover(rig: Rig):
    """Reaper must re-check ownership under the lock.

    Simulates: archive handle idle enough to be a candidate, then before the
    reaper stops it a harness calls ``register()`` which promotes the handle
    to ``ws``. The re-check under the lock must refuse to stop it.
    """
    _ = await rig.manager.register("r1", _md("n0"))
    await rig.manager.release("r1")
    _ = await rig.manager.ensure_queryable("r1")
    handle = rig.manager._containers["r1"]
    handle.last_access -= rig.manager.archive_idle_ttl_seconds + 5

    # Promote to ws before the reaper runs — simulates the race window where
    # a harness acquires the run between the reaper's scan and stop.
    _ = await rig.manager.register("r1", _md("n0"))
    assert rig.manager._containers["r1"].owner == "ws"

    await rig.manager.run_reaper_pass()

    # ws-owned container must survive.
    assert rig.manager._containers["r1"].owner == "ws"
    assert rig.compose.is_running(prometheus_container_name("r1"))


# ---------------------------------------------------------------------- shutdown


async def test_shutdown_waits_for_in_flight_boot(rig: Rig):
    """``shutdown()`` must not stop a container before ``up`` has finished.

    Otherwise the still-running ``compose.up`` leaks a container past
    daemon exit, which the next boot's recovery then has to clean up.
    """
    gate = asyncio.Event()
    rig.compose.pending_up_gate = gate

    boot_task = asyncio.create_task(rig.manager.register("r1", _md("n0")))
    # Let the boot task reach ``compose.up`` and park on the gate.
    await asyncio.sleep(0)
    await asyncio.sleep(0)
    assert ("up", prometheus_container_name("r1")) in rig.compose.call_log

    # Kick off shutdown while boot is still pending.
    shutdown_task = asyncio.create_task(rig.manager.shutdown())
    await asyncio.sleep(0.05)
    # shutdown should be blocked waiting for ``starting`` event.
    assert not shutdown_task.done()

    # Release the gate — boot finishes, then shutdown proceeds.
    gate.set()
    _ = await boot_task
    await shutdown_task

    # Container was created AND stopped — no leak.
    actions = [a for a, n in rig.compose.call_log if n == prometheus_container_name("r1")]
    assert actions == ["up", "down"]
    assert not rig.compose.is_running(prometheus_container_name("r1"))


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


# -------------------------------------------------------------------- port picker


async def test_port_picker_prefers_existing_port_for_known_run(rig: Rig):
    r = await rig.manager.register("r1", _md("n0"))
    old_port = r.host_port
    await rig.manager.release("r1")

    # Re-register: should get the same port back (nothing else is using it).
    r2 = await rig.manager.register("r1", _md("n0"))
    assert r2.host_port == old_port


async def test_port_picker_gives_fresh_port_when_preferred_is_taken(rig: Rig):
    # Lock r1 onto a port, then register r2 and verify it gets a different one.
    r1 = await rig.manager.register("r1", _md("n0"))
    r2 = await rig.manager.register("r2", _md("n0"))
    assert r2.host_port != r1.host_port


# ------------------------------------------------------------ regression bugs below
# These tests are expected to FAIL today; they pin down races the bug-review
# surfaced and the fix will flip them to passing.


async def test_register_after_shutdown_is_rejected(rig: Rig):
    """Bug: ``register`` ignores ``self._stopped``.

    A harness connecting after ``shutdown()`` has returned can install a new
    handle + start a container that outlives the daemon process.
    """
    await rig.manager.shutdown()

    with pytest.raises(Exception):
        _ = await rig.manager.register("r1", _md("n0"))

    assert not rig.compose.is_running(prometheus_container_name("r1"))


async def test_register_racing_with_shutdown_does_not_leak(rig: Rig):
    """Bug: ``register`` acquiring the lock between shutdown's two
    passes (set-stopped / wait-for-starting / stop-all) can insert a
    handle after shutdown's stop-loop has already snapshotted the keys.
    """
    gate = asyncio.Event()
    rig.compose.pending_up_gate = gate

    shutdown_task = asyncio.create_task(rig.manager.shutdown())
    # Let shutdown set ``_stopped`` and pass its (empty) starting-event wait.
    for _ in range(5):
        await asyncio.sleep(0)

    # New register sneaks in — currently succeeds silently.
    register_task: asyncio.Task[object] = asyncio.create_task(rig.manager.register("r1", _md("n0")))
    for _ in range(5):
        await asyncio.sleep(0)

    # Let the gated compose.up proceed.
    gate.set()

    try:
        _ = await register_task
    except Exception:
        pass  # the fix should reject; today it succeeds
    await shutdown_task

    # Container must not be alive after shutdown returns.
    assert not rig.compose.is_running(prometheus_container_name("r1")), (
        "shutdown + concurrent register leaked a container"
    )


async def test_shutdown_starting_timeout_does_not_stop_bogus_container(rig: Rig):
    """Bug: shutdown()'s ``asyncio.wait_for(starting.wait(), timeout=10)``
    was logged-and-swallowed. After timeout, the stop loop issues a
    ``compose.down(name)`` for a container ``compose.up(name)`` hasn't
    created yet — exactly the orphan-until-recovery leak the wait was
    supposed to prevent. Fix: on timeout, pop the handle so the stop loop
    skips it.
    """
    gate = asyncio.Event()
    rig.compose.pending_up_gate = gate

    # Kick off a register that will park in compose.up.
    register_task = asyncio.create_task(rig.manager.register("r1", _md("n0")))
    # Let it install the handle and enter compose.up.
    for _ in range(5):
        await asyncio.sleep(0)
    assert "r1" in rig.manager._containers

    # Shutdown: waits on handle.starting for 10s (virtual), times out.
    await rig.manager.shutdown()

    # The bug: shutdown calls compose.down("r1") before compose.up completes.
    down_calls = [n for action, n in rig.compose.call_log if action == "down"]
    assert prometheus_container_name("r1") not in down_calls, (
        "shutdown stopped a container whose compose.up hadn't finished"
    )

    # Release the parked up so we can clean up the task.
    gate.set()
    try:
        _ = await register_task
    except Exception:
        pass


async def test_failed_register_rollback_does_not_clobber_concurrent_register(hooked_rig: Rig):
    """Bug: after ``_start_container`` raises, ``register`` writes DORMANT to
    the DB without re-acquiring the lock. A concurrent ``register`` for the
    same run_id that succeeds in between sees its fresh ``LIVE`` row
    overwritten.
    """
    assert hooked_rig.hooks is not None
    pause = hooked_rig.hooks.pause_next_set_status(RunStatus.DORMANT)

    # First register fails at compose.up — enters rollback, pops handle,
    # then calls set_run_status(DORMANT). Our hook pauses it there.
    hooked_rig.compose.up_should_fail = True
    first_task = asyncio.create_task(hooked_rig.manager.register("r1", _md("n0")))

    # Let it reach (and block on) the DORMANT write.
    for _ in range(10):
        await asyncio.sleep(0)
    assert not first_task.done()

    # Second register for the same run_id now succeeds cleanly: acquires
    # the lock, sees no handle, writes LIVE via register_run, boots the
    # container, returns.
    row = await hooked_rig.manager.register("r1", _md("n0"))
    assert row.status == RunStatus.LIVE
    live_row = await hooked_rig.storage.get_run_metadata("r1")
    assert live_row is not None
    assert live_row.status == RunStatus.LIVE

    # Now release the paused rollback — today it stomps on the LIVE row.
    pause.set()
    with pytest.raises(Exception):
        _ = await first_task  # RunStartFailed
    # The container from the successful register must still be live, and
    # so must the DB row.
    final_row = await hooked_rig.storage.get_run_metadata("r1")
    assert final_row is not None
    assert final_row.status == RunStatus.LIVE, (
        "failed register's rollback clobbered the concurrent register's LIVE row"
    )
    assert hooked_rig.compose.is_running(prometheus_container_name("r1"))
