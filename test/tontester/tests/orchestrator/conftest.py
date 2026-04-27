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
    try:
        yield m
    finally:
        await m.shutdown()


@pytest_asyncio.fixture
async def fake_runtime() -> AsyncGenerator[FakeRuntime]:
    r = FakeRuntime()
    try:
        yield r
    finally:
        await r.close()


@pytest_asyncio.fixture
async def agent_pair(
    tmp_path: Path,
    manager: Manager,
    fake_runtime: FakeRuntime,
) -> AsyncGenerator[tuple[Manager, Agent]]:
    """Started Manager + in-process Agent backed by FakeRuntime."""
    a = Agent(
        fake_runtime,
        store=manager.store,
        state_dir=tmp_path / "agent",
        cgroup_root=None,
    )
    await manager.start()
    await a.start()
    try:
        yield (manager, a)
    finally:
        await a.stop()
