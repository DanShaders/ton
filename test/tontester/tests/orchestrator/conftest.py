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

from collections.abc import AsyncGenerator
from pathlib import Path

import aiotools
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


@pytest_asyncio.fixture
async def virtual_clock():
    with aiotools.VirtualClock().patch_loop():
        yield


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
