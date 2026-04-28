"""Test fixtures for the orchestrator package.

``virtual_clock`` is opt-in (not autouse) — tests that touch real
process I/O (the SubprocessRuntime smoke test) need real timing, while
tests that exercise reconciler timing want the virtual clock so they
finish in milliseconds.

Subscription-based wait helpers (``wait_for_event`` /
``wait_for_state``) live in :mod:`orchestrator.testing` so they're
importable across the test files without fighting pytest's
conftest-discovery semantics.
"""

import asyncio
from collections.abc import AsyncGenerator
from pathlib import Path

import aiotools
import pytest
import pytest_asyncio
from orchestrator import (
    Agent,
    FakeRuntime,
    InMemoryStore,
    Manager,
    Namespace,
    NetworkPolicy,
    PortForward,
    Workload,
    WorkloadSet,
)
from orchestrator.lifecycle import Reaper
from orchestrator.testing import MockProcess


@pytest_asyncio.fixture
async def virtual_clock():
    with aiotools.VirtualClock().patch_loop():
        yield


@pytest_asyncio.fixture
async def fake_processes(monkeypatch: pytest.MonkeyPatch) -> list[MockProcess]:
    """Patch ``asyncio.create_subprocess_exec`` to hand out a fresh
    :class:`MockProcess` per call.

    Why mock instead of forking real children: real subprocess +
    ``virtual_clock`` is racy. Real SIGCHLD delivery happens on the
    real wall-clock; virtual time advances independently. The reaper's
    ``await process.wait()`` can park indefinitely while virtual time
    races past the moment SIGCHLD would arrive in real time.
    Mocks make signal delivery deterministic.

    Mocks auto-exit on ``terminate``/``kill`` (well-behaved process
    semantics). Tests simulating stuck processes can flip
    ``auto_exit`` off via the returned handles.
    """
    spawned: list[MockProcess] = []

    async def _create(*_args: object, **_kwargs: object) -> MockProcess:
        p = MockProcess()
        p.auto_exit_on_signal()
        spawned.append(p)
        return p

    monkeypatch.setattr(asyncio, "create_subprocess_exec", _create)
    return spawned


@pytest_asyncio.fixture
async def reaper() -> AsyncGenerator[Reaper]:
    """Top-level reaper for kernel-state cleanup.

    Mirrors the production pattern: top-level call site owns it,
    drains in finally. Tests that construct ``SubprocessRuntime``
    must pass this fixture. The drain is unbounded — same as
    production. Tests that would otherwise hang (real subprocess
    under virtual_clock) should use the ``fake_processes`` fixture
    so reap is deterministic.
    """
    r = Reaper()
    try:
        yield r
    finally:
        await r.wait()


@pytest_asyncio.fixture
async def store() -> InMemoryStore:
    s = InMemoryStore()
    s.register_kind(Namespace)
    s.register_kind(Workload)
    s.register_kind(WorkloadSet)
    s.register_kind(NetworkPolicy)
    s.register_kind(PortForward)
    return s


@pytest_asyncio.fixture
async def manager() -> AsyncGenerator[Manager]:
    m = Manager()
    m.register_kind(Namespace)
    m.register_kind(Workload)
    m.register_kind(WorkloadSet)
    m.register_kind(NetworkPolicy)
    m.register_kind(PortForward)
    async with m.running():
        yield m


@pytest_asyncio.fixture
async def fake_runtime() -> FakeRuntime:
    """Constructed but not entered. The caller (e.g. ``agent_pair``,
    or a test that wants to drive it directly) is responsible for
    ``async with rt.running():``. We don't enter here because the
    common consumer is ``agent_pair`` which hands ownership to the
    agent — that would otherwise produce a double-enter of the
    runtime context."""
    return FakeRuntime()


@pytest_asyncio.fixture
async def agent_pair(
    tmp_path: Path,
    fake_runtime: FakeRuntime,
) -> AsyncGenerator[tuple[Manager, Agent]]:
    """Manager + in-process Agent backed by FakeRuntime, both running.

    The agent owns the runtime — running() enters it internally; we
    don't enter it here.
    """
    m = Manager()
    m.register_kind(Namespace)
    m.register_kind(Workload)
    m.register_kind(WorkloadSet)
    m.register_kind(NetworkPolicy)
    m.register_kind(PortForward)
    a = Agent(
        fake_runtime,
        store=m.store,
        state_dir=tmp_path / "agent",
        cgroup_root=None,
    )
    async with m.running(), a.running():
        yield (m, a)
