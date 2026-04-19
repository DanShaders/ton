"""Tests for :class:`daemon.compose.Compose`.

The real Compose delegates subprocess creation to a :class:`ProcessRunner`
and HTTP readiness probing to a :class:`ReadyProbeFn`. Tests inject
:class:`FakeProcessRunner` + :class:`FakeReadyProbe` so every code path —
including timeouts, post-ready exits, and stop-hang fallbacks — is
deterministic without touching podman.

Notable: ``test_up_post_ready_exit_stops_container`` pins down a bug that
was shipped earlier in this branch (the code path that raises
``ContainerStartFailed`` did not call ``down()``, leaking the container).
"""

# pyright: reportPrivateUsage=false
# The ``_processes`` dict on Compose is the state the tests exercise.

import asyncio

import pytest
from daemon.compose import (
    Compose,
    ContainerReadinessTimeout,
    ContainerStartFailed,
    PodmanTimeout,
)
from daemon.models import HttpReadyProbe, Service
from daemon.testing import ComposeRig, FakeProcessRunner, FakeReadyProbe

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


def _compose_with_fakes() -> tuple[Compose, ComposeRig]:
    runner = FakeProcessRunner()
    ready = FakeReadyProbe()
    compose = Compose(runner=runner, ready_probe=ready)
    return compose, ComposeRig(runner=runner, ready=ready)


def _service(name: str = "svc", *, probe: HttpReadyProbe | None = None) -> Service:
    return Service(
        name=name,
        image="example.com/example:latest",
        ports={9999: 80},
        ready_probe=probe,
    )


# ------------------------------------------------------------------ _podman basics


async def test_podman_command_returns_stripped_stdout():
    compose, rig = _compose_with_fakes()
    rig.runner.queue_capture(match="ps", returncode=0, stdout=b" hello \n")

    out = await compose._podman(["ps"])

    assert out == "hello"


async def test_podman_timeout_raises_pod_man_timeout():
    compose, rig = _compose_with_fakes()
    rig.runner.queue_capture(match="stop", hang=True)

    with pytest.raises(PodmanTimeout) as exc_info:
        _ = await compose._podman(["stop", "name"], timeout=0.05)

    assert exc_info.value.command[:2] == ["stop", "name"]
    assert exc_info.value.timeout == 0.05


async def test_podman_non_zero_raises_when_check_true():
    compose, rig = _compose_with_fakes()
    rig.runner.queue_capture(match="ps", returncode=1, stderr=b"boom")

    with pytest.raises(Exception):  # CalledProcessError
        _ = await compose._podman(["ps"])


async def test_podman_non_zero_ignored_when_check_false():
    compose, rig = _compose_with_fakes()
    rig.runner.queue_capture(match="ps", returncode=1, stdout=b"")

    out = await compose._podman(["ps"], check=False)

    assert out == ""


# --------------------------------------------------------------------- list/is_running


async def test_list_running_parses_lines():
    compose, rig = _compose_with_fakes()
    rig.runner.queue_capture(match="ps", stdout=b"a-1\na-2\n")

    names = await compose.list_running("a-")

    assert names == ["a-1", "a-2"]


async def test_list_running_empty():
    compose, _ = _compose_with_fakes()

    assert await compose.list_running("x-") == []


async def test_is_running_true_and_false():
    compose, rig = _compose_with_fakes()
    rig.runner.queue_capture(match="ps", stdout=b"123abc")
    assert await compose.is_running("foo") is True

    # Default capture returns empty.
    assert await compose.is_running("bar") is False


# ---------------------------------------------------------------------- up/down


async def test_up_happy_path_returns_when_ready():
    compose, rig = _compose_with_fakes()
    rig.ready.ok = True
    svc = _service(probe=HttpReadyProbe(path="/-/ready", container_port=80))

    await compose.up(svc)

    assert svc.name in compose._processes
    # _remove_if_exists (before run) + run_attached.
    assert any("rm -f svc" in " ".join(a) for a in rig.runner.capture_log)
    assert any(a[0] == "podman" and "run" in a for a in rig.runner.attached_log)


async def test_up_without_ready_probe_returns_after_spawn():
    compose, rig = _compose_with_fakes()
    svc = _service()

    await compose.up(svc)

    assert svc.name in compose._processes
    assert rig.ready.calls == []


async def test_up_readiness_timeout_stops_container():
    compose, rig = _compose_with_fakes()
    rig.ready.ok = False
    svc = _service(
        probe=HttpReadyProbe(
            path="/-/ready", container_port=80, timeout_seconds=0.05, interval_seconds=0.01
        )
    )

    with pytest.raises(ContainerReadinessTimeout):
        await compose.up(svc)

    # down() was called — podman stop in capture_log.
    assert any("stop svc" in " ".join(a) for a in rig.runner.capture_log)
    assert svc.name not in compose._processes


async def test_up_process_exits_before_ready():
    compose, rig = _compose_with_fakes()
    rig.ready.ok = False
    svc = _service(
        probe=HttpReadyProbe(
            path="/-/ready", container_port=80, timeout_seconds=1.0, interval_seconds=0.01
        )
    )

    async def kill_after_spawn():
        # Wait for the attached process to appear, then exit it with a non-zero code.
        while not rig.runner.attached:
            await asyncio.sleep(0.01)
        rig.runner.attached[0].simulate_exit(1)

    killer = asyncio.create_task(kill_after_spawn())
    with pytest.raises(ContainerStartFailed):
        await compose.up(svc)
    await killer

    assert svc.name not in compose._processes
    # down() rolled back.
    assert any("stop svc" in " ".join(a) for a in rig.runner.capture_log)


async def test_up_post_ready_exit_stops_container():
    """REGRESSION: container exited *after* readiness succeeded; the old
    code raised ContainerStartFailed without calling down(), leaking the
    container. The fix calls down() in that branch too.

    Uses ``before_returning_true`` to atomically exit the process inside
    the same probe call that reports ready — so the post-ready check
    in ``up()`` deterministically sees ``returncode is not None``.
    """
    compose, rig = _compose_with_fakes()
    rig.ready.ok = True
    svc = _service(probe=HttpReadyProbe(path="/-/ready", container_port=80))

    def kill_before_ready_returns() -> None:
        assert rig.runner.attached
        rig.runner.attached[0].simulate_exit(137)

    rig.ready.before_returning_true = kill_before_ready_returns

    with pytest.raises(ContainerStartFailed):
        await compose.up(svc)

    assert svc.name not in compose._processes
    stop_calls = [" ".join(a) for a in rig.runner.capture_log if "stop" in a]
    assert any("stop svc" in c for c in stop_calls), (
        "Compose.up's post-ready exit path did not call down(); container leaked"
    )


async def test_down_waits_for_attached_exit():
    compose, rig = _compose_with_fakes()
    svc = _service()
    await compose.up(svc)
    handle = rig.runner.attached[0]

    # podman stop succeeds; in the real world the attached child then exits.
    # Simulate that: arrange for podman stop to fire an exit on the handle.
    # Simplest: exit the handle before calling down.
    handle.simulate_exit(0)

    await compose.down(svc.name)

    assert svc.name not in compose._processes


async def test_down_recovers_when_podman_stop_hangs():
    """``podman stop`` hangs; Compose falls through to proc.wait (with a
    hard cap, then terminate/kill). We shortcut the hard cap by exiting the
    handle ourselves once ``down`` is past the stop phase.
    """
    compose, rig = _compose_with_fakes()
    svc = _service()
    await compose.up(svc)
    handle = rig.runner.attached[0]

    # podman stop will raise TimeoutError via the fake, which Compose turns
    # into PodmanTimeout and logs — then it waits on the child.
    rig.runner.queue_capture(match="stop", hang=True)

    down_task = asyncio.create_task(compose.down(svc.name))
    # Let ``down`` reach the child-wait phase, then simulate the child
    # exiting (as if SIGTERM had been received out-of-band). In production
    # the terminate/kill fallback would fire; here we assert the recovery
    # path completes at all and drops the handle from _processes.
    await asyncio.sleep(0)
    handle.simulate_exit(0)
    await down_task

    assert handle.returncode is not None
    assert svc.name not in compose._processes


async def test_down_bounded_even_if_child_ignores_kill():
    """Bug: ``Compose.down``'s final ``proc.wait()`` after kill() had no
    timeout. A wedged child (ignores SIGKILL, or its reaping is stuck)
    hung the daemon. Fix: wrap the final wait in a hard timeout.
    """
    compose, rig = _compose_with_fakes()
    svc = _service()
    await compose.up(svc)
    handle = rig.runner.attached[0]

    # podman stop hangs → Compose falls through to proc.wait → terminate → kill.
    rig.runner.queue_capture(match="stop", hang=True)
    handle.ignore_signals = True

    await asyncio.wait_for(compose.down(svc.name), timeout=30.0)

    assert svc.name not in compose._processes


async def test_down_is_noop_on_unknown_container():
    compose, _ = _compose_with_fakes()
    await compose.down("nothing-here")  # must not raise
