"""Namespace cascade-delete reconciler.

Apply a namespace + a workload inside it, then delete the namespace —
the workload should drain (via the agent finalizer) and the namespace
row should disappear once everything inside is gone.

Waits use the store's watch primitive (see ``conftest.wait_for_event``
/ ``conftest.wait_for_state``) — never poll-and-sleep, which would
race the reconciler's async patch path.
"""

import asyncio
from pathlib import Path

import pytest
from orchestrator import (
    Agent,
    Container,
    FakeRuntime,
    HostBinaryImage,
    Manager,
    Metadata,
    Namespace,
    NamespaceReconciler,
    NetworkPolicy,
    PortForward,
    WatchEventType,
    Workload,
    WorkloadSet,
    WorkloadSpec,
)
from orchestrator.testing import wait_for_event, wait_for_state

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


def _ns(name: str) -> Namespace:
    return Namespace(metadata=Metadata(name=name))


def _wl(name: str, namespace: str) -> Workload:
    return Workload(
        metadata=Metadata(name=name, namespace=namespace),
        spec=WorkloadSpec(
            containers=[Container(name="primary", image=HostBinaryImage(path="/bin/true"))],
        ),
    )


async def test_namespace_finalizer_added(tmp_path: Path, fake_runtime: FakeRuntime):
    manager = Manager()
    manager.register_kind(Namespace)
    manager.register_kind(Workload)
    manager.register_kind(WorkloadSet)
    manager.register_kind(NetworkPolicy)
    manager.register_kind(PortForward)
    manager.add_controller(NamespaceReconciler())
    agent = Agent(fake_runtime, store=manager.store, state_dir=tmp_path / "agent")
    await manager.start()
    await agent.start()
    try:
        _ = await manager.store.apply(_ns("run-1"))

        ns = await wait_for_event(
            manager.store,
            Namespace,
            lambda n: n.metadata.name == "run-1" and bool(n.metadata.finalizers),
            timeout=3.0,
        )
        assert "orchestrator.io/namespace-cascade" in ns.metadata.finalizers
    finally:
        await agent.stop()
        await manager.shutdown()


async def test_namespace_cascade_drains_workloads(tmp_path: Path, fake_runtime: FakeRuntime):
    manager = Manager()
    manager.register_kind(Namespace)
    manager.register_kind(Workload)
    manager.register_kind(WorkloadSet)
    manager.register_kind(NetworkPolicy)
    manager.register_kind(PortForward)
    manager.add_controller(NamespaceReconciler())
    agent = Agent(fake_runtime, store=manager.store, state_dir=tmp_path / "agent")
    await manager.start()
    await agent.start()
    try:
        _ = await manager.store.apply(_ns("run-2"))
        _ = await manager.store.apply(_wl("alpha", "run-2"))
        _ = await manager.store.apply(_wl("beta", "run-2"))

        # Wait for the agent to claim both workloads — i.e., both have
        # finalizers and both rows are present.
        await wait_for_state(
            manager.store,
            Workload,
            lambda: (
                len(manager.store.list(Workload, namespace="run-2")) == 2
                and all(
                    w.metadata.finalizers for w in manager.store.list(Workload, namespace="run-2")
                )
            ),
            timeout=3.0,
        )

        # Open a Namespace subscription BEFORE delete so the eventual
        # DELETED event (fired only after the cascade has drained every
        # workload) is observed without a race window.
        async with manager.store.subscription(Namespace) as sub:
            await manager.store.delete(Namespace, namespace=None, name="run-2")

            async def _await_namespace_gone() -> None:
                async for event in sub:
                    if (
                        event.type == WatchEventType.DELETED
                        and event.resource.metadata.name == "run-2"
                    ):
                        return
                raise RuntimeError("Namespace subscription closed before run-2 was deleted")

            await asyncio.wait_for(_await_namespace_gone(), timeout=5.0)

        # By the time the namespace is hard-deleted the cascade
        # reconciler must have first drained every child workload,
        # so this is a post-condition check, not a wait.
        assert manager.store.list(Workload, namespace="run-2") == []
    finally:
        await agent.stop()
        await manager.shutdown()
