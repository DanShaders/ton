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


async def test_apply_into_namespace_marked_terminating_during_lock_wait_rejected():
    """Round-7 H2: ``_reject_if_namespace_terminating`` runs *outside*
    the workload kind's lock. The subsequent ``async with state.lock``
    yields if the lock is contended; during that yield, another task
    can mark the namespace terminating. When the apply resumes inside
    the lock, the check has already passed — the orphan child is
    written into a now-terminating namespace.

    Test setup: hold the workload lock from a sibling task; while
    held, mark the namespace terminating; release the lock. The
    pending apply must fail-the-check INSIDE the lock and raise
    ``NamespaceTerminating`` rather than silently accepting the write.

    Pre-fix: the check was outside the lock; the test apply succeeds
    and creates an orphan workload.
    """
    from orchestrator import InMemoryStore
    from orchestrator.errors import NamespaceTerminating

    store = InMemoryStore()
    store.register_kind(Namespace)
    store.register_kind(Workload)

    _ = await store.apply(_ns("dying"))
    _ = await store.patch_metadata(
        Namespace,
        namespace=None,
        name="dying",
        mutator=lambda m: m.finalizers.append("test/keep"),
    )
    # Namespace is registered but not yet terminating.

    # Hold the Workload kind's lock from a sibling task so the
    # apply below has to wait for it.
    workload_state = store._kinds[Workload]  # pyright: ignore[reportPrivateUsage]
    _ = await workload_state.lock.acquire()

    apply_task = asyncio.create_task(
        store.apply(_wl("late-arrival", "dying")),
        name="t.apply",
    )
    # Let apply progress through the (now-passing) namespace check
    # and park on the workload lock.
    await asyncio.sleep(0)

    # Race: mark namespace terminating *while* apply is parked
    # waiting for the workload lock. The store's namespace lock is
    # independent; this delete proceeds.
    await store.delete(Namespace, namespace=None, name="dying")

    # Confirm the namespace is now terminating.
    ns_now = store.get(Namespace, namespace=None, name="dying")
    assert ns_now.metadata.deletion_timestamp is not None

    # Release the workload lock. The apply re-acquires and must
    # re-check (inside the lock) that the namespace is still writable.
    workload_state.lock.release()

    # Apply must fail with NamespaceTerminating now.
    with pytest.raises(NamespaceTerminating, match="dying"):
        _ = await apply_task


async def test_apply_into_terminating_namespace_rejected():
    """Store invariant: once a namespace has a deletion_timestamp,
    new ``apply`` calls for resources inside it must be rejected.

    This eliminates the cascade orphan window: the cascade reconciler
    enumerates children, drops the namespace finalizer, and the row
    is hard-deleted. Without this guard, a late ``apply`` between
    enumerate and finalizer-drop would create an orphan child whose
    namespace was already gone. With the guard, that race is
    structurally impossible — the late apply just raises.
    """
    from orchestrator import InMemoryStore
    from orchestrator.errors import NamespaceTerminating

    store = InMemoryStore()
    store.register_kind(Namespace)
    store.register_kind(Workload)

    # Bring up a namespace, install a finalizer so delete() leaves
    # it in terminating state instead of hard-deleting.
    _ = await store.apply(_ns("dying"))
    _ = await store.patch_metadata(
        Namespace,
        namespace=None,
        name="dying",
        mutator=lambda m: m.finalizers.append("test/keep"),
    )
    await store.delete(Namespace, namespace=None, name="dying")

    ns_now = store.get(Namespace, namespace=None, name="dying")
    assert ns_now.metadata.deletion_timestamp is not None, (
        "namespace must be terminating, not hard-deleted, for this test"
    )

    # Now: apply a Workload into the dying namespace must fail.
    with pytest.raises(NamespaceTerminating, match="dying"):
        _ = await store.apply(_wl("late-arrival", "dying"))


async def test_namespace_finalizer_added(tmp_path: Path, fake_runtime: FakeRuntime):
    manager = Manager()
    manager.register_kind(Namespace)
    manager.register_kind(Workload)
    manager.register_kind(WorkloadSet)
    manager.register_kind(NetworkPolicy)
    manager.register_kind(PortForward)
    manager.add_controller(NamespaceReconciler())
    agent = Agent(fake_runtime, store=manager.store, state_dir=tmp_path / "agent")
    async with manager.running(), agent.running():
        _ = await manager.store.apply(_ns("run-1"))

        ns = await wait_for_event(
            manager.store,
            Namespace,
            lambda n: n.metadata.name == "run-1" and bool(n.metadata.finalizers),
            timeout=3.0,
        )
        assert "orchestrator.io/namespace-cascade" in ns.metadata.finalizers


async def test_namespace_cascade_drains_workloads(tmp_path: Path, fake_runtime: FakeRuntime):
    manager = Manager()
    manager.register_kind(Namespace)
    manager.register_kind(Workload)
    manager.register_kind(WorkloadSet)
    manager.register_kind(NetworkPolicy)
    manager.register_kind(PortForward)
    manager.add_controller(NamespaceReconciler())
    agent = Agent(fake_runtime, store=manager.store, state_dir=tmp_path / "agent")
    async with manager.running(), agent.running():
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
