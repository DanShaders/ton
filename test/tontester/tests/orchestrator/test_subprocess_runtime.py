# pyright: reportPrivateUsage=false
"""SubprocessRuntime concurrency + cleanup invariants.

Uses short-lived ``/bin/sleep`` workloads; tests that need to inject
faults monkey-patch ``asyncio.create_subprocess_exec``.
"""

import asyncio
import contextlib
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
from orchestrator.testing import wait_for_asyncio_idle

pytestmark = [
    pytest.mark.asyncio,
    pytest.mark.usefixtures("virtual_clock", "fake_processes"),
]


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


async def test_safe_rmtree_tolerates_malformed_cgroup_procs(tmp_path: Path):
    """``_safe_rmtree`` parses ``cgroup.procs`` as ints. The narrow
    ``except OSError`` catch lets ``ValueError`` from a malformed
    line escape — which would crash the supervisor's sync stack
    callback during unwind. Real cgroup.procs can race with the
    kernel writing partial entries.
    """
    from orchestrator.runtime.subprocess_runtime import _safe_rmtree

    cgroup_dir = tmp_path / "fake_cgroup"
    cgroup_dir.mkdir()
    procs = cgroup_dir / "cgroup.procs"
    _ = procs.write_text("123\nnot-a-pid\n456\n")

    # Must not raise — sync stack callbacks are required to be
    # exception-clean during unwind.
    _safe_rmtree(cgroup_dir)


async def test_apply_after_running_exit_raises(tmp_path: Path, reaper: Reaper):
    """``apply()`` after the runtime's ``running()`` context has
    exited must raise rather than silently spawning a leak. The
    supervisor pattern makes this the natural shape: the
    AsyncExitStack of ``running()`` cancels every supervisor on
    exit, sets ``_closed = True``, and any further ``apply`` checks
    ``_closed`` synchronously at the top.
    """
    rt = SubprocessRuntime(state_dir=tmp_path / "state", reaper=reaper)
    async with rt.running():
        pass

    with pytest.raises(RuntimeError, match="closed"):
        _ = await rt.apply(_wl("alpha"))
    assert ("default", "alpha") not in rt._supervisors


async def test_supervisor_cancellation_cleans_cgroup_dir(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
):
    """Round-5 cluster (#1, #4, #7): the cgroup_dir cleanup hazards
    around ``_restart_locked`` / ``_wait_exit`` are eliminated by
    the supervisor's CheckedExitStack: cgroup rmdir is a sync stack
    callback pushed during ``__aenter__``. Even when the supervisor's
    process.wait is patched to hang, ``__aexit__``'s sync rmdir
    callback still removes the (test-fixture, non-cgroup-fs) dir.

    Uses a *local* :class:`Reaper` instance and never awaits it —
    the patched-to-hang ``process.wait`` would also hang any
    ``_async_reap_and_publish`` task offloaded by
    ``_sync_kill_process``. The shared fixture reaper would block
    teardown forever; this is a "simulate stuck state" test, not
    an "operator awaits clean state" test.
    """
    from orchestrator.lifecycle import Reaper as _Reaper

    local_reaper = _Reaper(name="cgroup-cancel-test")
    rt = SubprocessRuntime(
        state_dir=tmp_path / "state",
        cgroup_root=tmp_path / "cgroup",
        reaper=local_reaper,
    )
    wl = _wl_with_cgroup("alpha")
    cgroup_dir = tmp_path / "cgroup" / "wl-default-alpha"

    async with rt.running():
        _ = await rt.apply(wl)
        assert cgroup_dir.exists(), "apply should have created the cgroup_dir"

        sup = rt._supervisors[("default", "alpha")]
        view = sup.view
        assert view is not None, "view must be set after apply()"
        process = view.process
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
            "cgroup_dir leaked through supervisor tear-down; "
            "the sync rm_cgroup stack callback should have cleaned it"
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
    reaper: Reaper,
    monkeypatch: pytest.MonkeyPatch,
):
    """Round-2 finding 1.1: if ``create_subprocess_exec`` raises
    ``CancelledError`` (or any other BaseException), the cgroup_dir
    cleanup callback that was already pushed onto the supervisor's
    CheckedExitStack still fires. The stack unwinds during
    ``__aenter__``'s exception path, running the rm_cgroup callback
    — no narrow ``except OSError`` to miss the cancellation case.
    """
    rt = SubprocessRuntime(
        state_dir=tmp_path / "state",
        cgroup_root=tmp_path / "cgroup",
        reaper=reaper,
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
            "sync rm_cgroup stack callback should have cleaned it"
        )


async def test_supersede_drops_predecessor_refs_after_handoff(tmp_path: Path, reaper: Reaper):
    """Predecessor pattern: when v2 supersedes v1, v2 owns v1's
    teardown via the predecessor handoff. After v2's __aenter__
    returns, the runtime tracks only v2 (v1 was drained + aexit'd
    inside v2's _drain_predecessor); v2's predecessor refs are
    dropped to make ``shutdown after aexit`` structurally
    impossible.

    Pin: after a supersede, the runtime's _supervisors maps key →
    v2 only, v1 is no longer running, and v2 holds no live ref
    to v1.
    """
    rt = SubprocessRuntime(state_dir=tmp_path / "state", reaper=reaper)
    async with rt.running():
        v1_status = await rt.apply(_wl("rev"))
        sup_v1 = rt._supervisors[("default", "rev")]
        cm_v1 = rt._supervisor_cms[("default", "rev")]

        # v2 has a different spec → triggers supersede.
        v2_wl = _wl("rev")
        v2_wl.spec.containers[0].command = ["1"]
        _ = await rt.apply(v2_wl)

        sup_v2 = rt._supervisors[("default", "rev")]
        cm_v2 = rt._supervisor_cms[("default", "rev")]
        assert sup_v2 is not sup_v1, "runtime should have replaced v1 with v2"
        assert cm_v2 is not cm_v1, "runtime should track v2's cm, not v1's"

        # Predecessor refs cleared on v2 — the structural guard
        # against ``shutdown after aexit``.
        assert sup_v2._predecessor is None, (
            "v2 must drop predecessor ref after draining; otherwise a "
            "future call to shutdown() on it would race aexit"
        )
        assert sup_v2._predecessor_cm is None

        # v1's __aexit__ ran: _is_running flipped, sync release fired.
        # (We don't assert process reaped — under virtual_clock the
        # wait_for in shutdown returns instantly without giving the
        # OS a chance to deliver SIGCHLD; SIGKILL was sent, the OS
        # reaps eventually.)
        assert not sup_v1._is_running

        assert v1_status.phase == "Running"
        await rt.shutdown()


async def test_runtime_shutdown_cancellation_releases_supervisors_via_aexit(
    tmp_path: Path,
    reaper: Reaper,
):
    """Cancel ``rt.shutdown()`` mid-flight (its gather is awaiting
    every supervisor's drain). The runtime's sync ``__aexit__``
    must still release every supervisor via ``release_sync``.

    Verifies the half-cancellation contract at the runtime level:
    even with cancel pending, every supervisor's sync stack-callbacks
    fire, processes get SIGKILL'd, _is_running flips.
    """
    rt = SubprocessRuntime(state_dir=tmp_path / "state", reaper=reaper)
    async with rt.running():
        _ = await rt.apply(_wl("a"))
        _ = await rt.apply(_wl("b"))
        sup_a = rt._supervisors[("default", "a")]
        sup_b = rt._supervisors[("default", "b")]
        assert sup_a._is_running and sup_b._is_running

        # Spawn shutdown as its own task; cancel after one yield so
        # its gather has started awaiting the per-supervisor drains.
        shutdown_task = asyncio.create_task(rt.shutdown(), name="t.rt.shutdown")
        await asyncio.sleep(0)
        _ = shutdown_task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await shutdown_task

    # __aexit__ ran release_sync on every still-tracked supervisor.
    assert not sup_a._is_running, (
        "supervisor a's __aexit__ should have flipped _is_running even "
        "after rt.shutdown was cancelled"
    )
    assert not sup_b._is_running


async def test_supervisor_shutdown_cancellation_still_releases(
    tmp_path: Path,
    reaper: Reaper,
):
    """Cancel ``sup.shutdown()`` while it's awaiting process.wait
    after SIGTERM. The cm.__aexit__ must still run the sync stack
    unwind: SIGKILL fires, cgroup rmdir.
    """
    rt = SubprocessRuntime(
        state_dir=tmp_path / "state",
        cgroup_root=tmp_path / "cgroup",
        reaper=reaper,
    )
    cgroup_dir = tmp_path / "cgroup" / "wl-default-x"

    async with rt.running():
        _ = await rt.apply(_wl_with_cgroup("x"))
        assert cgroup_dir.exists()
        sup = rt._supervisors[("default", "x")]

        # Spawn shutdown as its own task; cancel mid-way.
        shutdown_task = asyncio.create_task(sup.shutdown(), name="t.sup.shutdown")
        await asyncio.sleep(0)
        _ = shutdown_task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await shutdown_task

        # delete() runs cm.__aexit__ on the supervisor — that's the
        # reconciler that runs the sync release callbacks.
        await rt.delete(namespace="default", name="x")

    assert not sup._is_running
    assert not cgroup_dir.exists(), (
        "cgroup_dir should be cleaned by the supervisor's sync rm callback "
        "even after shutdown was cancelled"
    )


async def test_forceful_teardown_offloads_reap_to_reaper(
    tmp_path: Path,
    reaper: Reaper,
):
    """New contract (post-Reaper): forceful runtime tear-down does
    NOT publish a synthetic EXITED event. Instead,
    ``_sync_kill_process`` SIGKILLs sync and offloads
    ``_async_reap_and_publish`` to the reaper. The reaper task
    awaits the real reap and publishes the real exit code.

    By the time the reaper publishes, the bus is closed (subscribers
    are also being torn down), so no orchestrator-level subscriber
    sees the event — that's intentional. What we verify here:

    - Sync ``__aexit__`` returns immediately (no await for reap).
    - The reaper has the offloaded reap+publish task.
    - After ``reaper.wait()``, the process is reaped (returncode set
      to a SIGKILL value).
    """
    rt = SubprocessRuntime(state_dir=tmp_path / "state", reaper=reaper)

    async with rt.running():
        _ = await rt.apply(_wl("zap"))
        sup = rt._supervisors[("default", "zap")]
        view = sup.view
        assert view is not None
        process = view.process
        # Forceful exit: no shutdown(). __aexit__ runs sync release.

    # __aexit__ already returned → reaper has the reap task.
    assert len(reaper) >= 1, "reap+publish should have been offloaded"

    # The fixture awaits reaper.wait() in finally; verify by polling
    # the asyncio loop until the reap completes. Under real-time
    # subprocess this happens quickly after SIGKILL is delivered.
    await reaper.wait()
    assert process.returncode is not None, "reaper task should have awaited process reap"


async def test_graceful_shutdown_publishes_real_exit_status(tmp_path: Path, reaper: Reaper):
    """Sanity: when we DO call shutdown(), the natural-exit path
    (process actually reaped) produces an EXITED with the real exit
    code. The synthetic-Failed path is only for the no-shutdown case.
    """
    rt = SubprocessRuntime(state_dir=tmp_path / "state", reaper=reaper)
    captured: list[tuple[str, int | None]] = []

    async def _drain() -> None:
        async with rt.watch() as events:
            async for event in events:
                if event.type.value == "exited":
                    cs = event.status.container_statuses
                    captured.append((event.status.phase, cs[0].exit_code if cs else None))

    drain_task = asyncio.create_task(_drain(), name="t.drain")
    await wait_for_asyncio_idle()

    async with rt.running():
        # /bin/true exits with 0 immediately. Use a workload that
        # actually exits naturally; then call shutdown() so the
        # graceful path runs.
        _ = await rt.apply(
            Workload(
                metadata=Metadata(name="ok", namespace="default"),
                spec=WorkloadSpec(
                    containers=[
                        Container(name="c", image=HostBinaryImage(path="/bin/true"), command=[])
                    ],
                ),
            )
        )
        await rt.shutdown()

    await wait_for_asyncio_idle()
    _ = drain_task.cancel()
    with contextlib.suppress(asyncio.CancelledError):
        await drain_task

    assert captured, "expected an EXITED event"
    phase, _exit = captured[-1]
    # /bin/true exits 0 → Succeeded, OR shutdown raced (terminate
    # before natural exit) → Failed with negative exit code. Either
    # way, NOT a synthetic Failed-with-SIGKILL during forceful path.
    assert phase in {"Succeeded", "Failed"}


async def test_supervisor_shutdown_idempotent_inside_running(tmp_path: Path, reaper: Reaper):
    """Supervisor.shutdown() called multiple times inside running()
    is idempotent: second call sees stop_token already set, process
    already dead, monitor task already cancelled — no-op."""
    rt = SubprocessRuntime(state_dir=tmp_path / "state", reaper=reaper)
    async with rt.running():
        _ = await rt.apply(_wl("idem"))
        sup = rt._supervisors[("default", "idem")]
        await sup.shutdown()
        # Second call: still _is_running (we haven't aexit'd yet).
        # No raise. Process already dying.
        await sup.shutdown()
        await sup.shutdown()
        await rt.delete(namespace="default", name="idem")


async def test_chained_supersedes_release_each_predecessor(tmp_path: Path, reaper: Reaper):
    """v1 → v2 → v3: each supersede correctly drains the immediate
    predecessor and drops refs. The whole chain converges to a single
    live supervisor with no orphan refs.
    """
    rt = SubprocessRuntime(state_dir=tmp_path / "state", reaper=reaper)
    async with rt.running():
        _ = await rt.apply(_wl("chain"))
        v1 = rt._supervisors[("default", "chain")]

        v2_wl = _wl("chain")
        v2_wl.spec.containers[0].command = ["1"]
        _ = await rt.apply(v2_wl)
        v2 = rt._supervisors[("default", "chain")]
        assert v2 is not v1
        assert not v1._is_running, "v1 must be released by v2's __aenter__"
        assert v2._predecessor is None and v2._predecessor_cm is None

        v3_wl = _wl("chain")
        v3_wl.spec.containers[0].command = ["2"]
        _ = await rt.apply(v3_wl)
        v3 = rt._supervisors[("default", "chain")]
        assert v3 is not v2
        assert not v2._is_running, "v2 must be released by v3's __aenter__"
        assert v3._predecessor is None and v3._predecessor_cm is None

        # v3 alone is live; v1 was already released long ago.
        assert v3._is_running
        assert not v1._is_running

        await rt.shutdown()


async def test_supersede_with_no_predecessor_is_noop(tmp_path: Path, reaper: Reaper):
    """When no existing supervisor for the key, ``apply`` constructs
    a fresh supervisor with predecessor=None. _drain_predecessor
    short-circuits; spawn proceeds normally.
    """
    rt = SubprocessRuntime(state_dir=tmp_path / "state", reaper=reaper)
    async with rt.running():
        _ = await rt.apply(_wl("first"))
        sup = rt._supervisors[("default", "first")]
        # No predecessor was ever supplied.
        assert sup._predecessor is None
        assert sup._predecessor_cm is None
        assert sup._is_running
        await rt.shutdown()


async def test_supersede_cancel_after_predecessor_drained_releases_predecessor(
    tmp_path: Path,
    reaper: Reaper,
    monkeypatch: pytest.MonkeyPatch,
):
    """Cancel v2's apply task AFTER v2's _drain_predecessor returned
    but BEFORE v2's spawn completes. v1 was already drained + aexit'd
    by then (refs dropped). v2's CheckedExitStack unwinds whatever it
    had pushed; runtime never sees v2.

    This pins: even with cancel landing in v2's "own setup" phase
    (not predecessor phase), v1 stays released and the runtime
    state is consistent.
    """
    rt = SubprocessRuntime(state_dir=tmp_path / "state", reaper=reaper)
    async with rt.running():
        _ = await rt.apply(_wl("setup"))
        v1 = rt._supervisors[("default", "setup")]

        # Patch create_subprocess_exec to block forever AFTER v1's
        # cleanup finishes — i.e., when v2 is spawning its own
        # process. The block never returns; the never-fires Event
        # call ensures cancellation is the only exit, so we never
        # try to call the original (which would need typed args).
        spawn_started = asyncio.Event()

        async def _hang_spawn(*_args: object, **_kwargs: object) -> asyncio.subprocess.Process:
            spawn_started.set()
            _ = await asyncio.Event().wait()
            raise AssertionError("unreachable: caller should cancel before this")

        monkeypatch.setattr(asyncio, "create_subprocess_exec", _hang_spawn)

        v2_wl = _wl("setup")
        v2_wl.spec.containers[0].command = ["1"]
        apply_task = asyncio.create_task(rt.apply(v2_wl), name="t.apply.v2")
        # Bigger virtual timeout because v1's shutdown burns up to
        # termination_grace_s (default 30s) of virtual time before
        # spawn proceeds.
        _ = await asyncio.wait_for(spawn_started.wait(), timeout=120.0)
        # By here v1 has been drained + aexit'd inside v2's
        # _drain_predecessor; v2 is now parked in our hung spawn.
        assert not v1._is_running, "v1 should have been released before spawn started"

        _ = apply_task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await apply_task

        # Runtime state: v1 was popped, v2 never registered. Key absent.
        assert ("default", "setup") not in rt._supervisors
        assert ("default", "setup") not in rt._supervisor_cms

        # Restore the fake_processes factory (test patched over it) so
        # the next spawn returns a fresh MockProcess instead of forking
        # a real child. We can't ``monkeypatch.undo()`` because that
        # would also undo the fixture's setup.
        from orchestrator.testing import MockProcess as _MP

        def _make_fresh_mock() -> _MP:
            p = _MP()
            p.auto_exit_on_signal()
            return p

        async def _create_fresh(*_args: object, **_kwargs: object) -> _MP:
            return _make_fresh_mock()

        monkeypatch.setattr(asyncio, "create_subprocess_exec", _create_fresh)
        _ = await rt.apply(_wl("setup"))
        assert ("default", "setup") in rt._supervisors

        await rt.shutdown()


async def test_supersede_with_failing_build_argv_still_drains_predecessor(
    tmp_path: Path,
    reaper: Reaper,
):
    """Round-7 H1: ``_build_argv`` raises (e.g. ``TarballImage``)
    before ``_drain_predecessor`` runs. The runtime had already
    popped the predecessor from its dicts in ``_start_supervisor``.
    If drain doesn't run first, the predecessor is orphaned — its
    process keeps running, its cm stays open, no one tracks it.

    Pin: drain runs unconditionally, so even if the new supervisor's
    own setup fails, v1 is properly released.
    """
    rt = SubprocessRuntime(state_dir=tmp_path / "state", reaper=reaper)
    async with rt.running():
        _ = await rt.apply(_wl("rev"))
        sup_v1 = rt._supervisors[("default", "rev")]
        assert sup_v1._is_running

        # v2 with a TarballImage: ``_build_argv`` will raise
        # RuntimeError. We do NOT want that to leak v1.
        from orchestrator.resources import TarballImage

        v2_wl = Workload(
            metadata=Metadata(name="rev", namespace="default"),
            spec=WorkloadSpec(
                containers=[
                    Container(
                        name="c",
                        image=TarballImage(path="/tmp/some.tar"),
                        command=[],
                    )
                ],
            ),
        )

        with pytest.raises(RuntimeError, match="tarball|RuncRuntime"):
            _ = await rt.apply(v2_wl)

        # v1 must be fully released even though v2's setup failed.
        assert not sup_v1._is_running, (
            "v1 should have been drained by v2's _drain_predecessor "
            "before _build_argv raised; otherwise v1 leaks"
        )
        # Runtime no longer tracks the key — v1 was popped, v2 never
        # registered. A subsequent apply spawns fresh.
        assert ("default", "rev") not in rt._supervisors


async def test_supersede_during_predecessor_drain_is_cancellable(
    tmp_path: Path,
    reaper: Reaper,
    monkeypatch: pytest.MonkeyPatch,
):
    """Cancel the apply task mid-supersede: v2's _drain_predecessor
    is awaiting v1.shutdown when CancelledError hits. The finally
    in _drain_predecessor must still aexit v1's cm (cm body has no
    awaits, runs under cancel-pending). After cancel propagates,
    v1's process is reaped; v2 was never spawned.

    This exercises the half-cancellation contract for the
    predecessor handoff path specifically.
    """
    rt = SubprocessRuntime(state_dir=tmp_path / "state", reaper=reaper)
    async with rt.running():
        _ = await rt.apply(_wl("hung"))
        sup_v1 = rt._supervisors[("default", "hung")]

        # Patch v1's shutdown to hang on a never-firing event so we
        # can deterministically cancel v2's apply task while v2 is
        # in _drain_predecessor's `await predecessor.shutdown()`.
        v1_shutdown_started = asyncio.Event()
        let_v1_shutdown_finish = asyncio.Event()

        original_shutdown = sup_v1.shutdown

        async def _hung_shutdown() -> None:
            v1_shutdown_started.set()
            _ = await let_v1_shutdown_finish.wait()
            await original_shutdown()

        monkeypatch.setattr(sup_v1, "shutdown", _hung_shutdown)

        # Kick off v2's apply; it'll park inside v1.shutdown().
        v2_wl = _wl("hung")
        v2_wl.spec.containers[0].command = ["1"]
        apply_task = asyncio.create_task(rt.apply(v2_wl), name="t.apply.v2")
        _ = await asyncio.wait_for(v1_shutdown_started.wait(), timeout=2.0)

        # Cancel v2's apply task. _drain_predecessor's finally must
        # still aexit v1's cm.
        _ = apply_task.cancel()
        let_v1_shutdown_finish.set()  # let the parked shutdown complete
        with pytest.raises(asyncio.CancelledError):
            await apply_task

        # v1 must be fully released: _is_running False, process reaped,
        # and the runtime entries gone (handoff already happened).
        assert not sup_v1._is_running, (
            "v1 should be aexit'd by _drain_predecessor's finally even under cancel-pending"
        )
        assert ("default", "hung") not in rt._supervisors
        assert ("default", "hung") not in rt._supervisor_cms

        await rt.shutdown()
