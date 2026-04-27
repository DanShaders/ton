"""WorkloadSet reconciler: replicas, ordinals, scale up/down, deletion cascade."""

from pathlib import Path

import pytest
from orchestrator import (
    Agent,
    Container,
    FakeRuntime,
    HostBinaryImage,
    LabelSelector,
    Manager,
    Metadata,
    Workload,
    WorkloadSet,
    WorkloadSetReconciler,
    WorkloadSetSpec,
    WorkloadSpec,
    WorkloadTemplate,
)
from orchestrator.testing import wait_for_state

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


def _set(name: str, replicas: int) -> WorkloadSet:
    return WorkloadSet(
        metadata=Metadata(name=name, namespace="default", labels={"app": name}),
        spec=WorkloadSetSpec(
            replicas=replicas,
            selector=LabelSelector(match_labels={"app": name}),
            template=WorkloadTemplate(
                labels={"app": name},
                spec=WorkloadSpec(
                    containers=[Container(name="primary", image=HostBinaryImage(path="/bin/true"))],
                ),
            ),
        ),
    )


async def _wait_for_replicas(manager: Manager, set_name: str, n: int) -> None:
    """Subscribe to Workload, then wait until the set has exactly ``n``
    replicas in the default namespace.

    Subscribe first so any apply/delete the WorkloadSet reconciler
    issues after this is observed; the ``state_check`` re-runs after
    every event until the count matches.
    """
    await wait_for_state(
        manager.store,
        Workload,
        lambda: (
            len(
                [
                    w
                    for w in manager.store.list(Workload, namespace="default")
                    if w.metadata.name.startswith(set_name)
                ]
            )
            == n
        ),
        timeout=3.0,
    )


async def test_workloadset_creates_and_scales(tmp_path: Path, fake_runtime: FakeRuntime):
    manager = Manager()
    manager.register_kind(WorkloadSet)
    manager.register_kind(Workload)
    manager.add_controller(WorkloadSetReconciler())
    agent = Agent(fake_runtime, store=manager.store, state_dir=tmp_path / "agent")
    async with manager.running(), agent.running():
        _ = await manager.store.apply(_set("validators", 3))
        await _wait_for_replicas(manager, "validators", 3)
        names = sorted(w.metadata.name for w in manager.store.list(Workload, namespace="default"))
        assert names == ["validators-0", "validators-1", "validators-2"]

        # Scale up.
        _ = await manager.store.apply(_set("validators", 5))
        await _wait_for_replicas(manager, "validators", 5)

        # Scale down.
        _ = await manager.store.apply(_set("validators", 2))
        await _wait_for_replicas(manager, "validators", 2)


async def test_workloadset_direct_delete_does_not_orphan_children(
    tmp_path: Path,
    fake_runtime: FakeRuntime,
):
    """Bug (audit round 4): when a WorkloadSet has a deletion_timestamp,
    the reconciler iterates ``range(wset.spec.replicas)`` and issues
    deletes for those names. But:

    1. WorkloadSet has no finalizer, so ``store.delete`` hard-deletes
       the row immediately — before the children have actually drained.
    2. Once the set is hard-deleted, ``store.get_or_none(WorkloadSet)``
       returns None and the reconciler returns ``ok``. Any child whose
       ordinal is ``>= replicas`` (because of an in-flight scale-down,
       or simply the agent's finalizer delaying hard-delete) is now
       owned by a no-longer-existent set — permanent orphan.

    Trigger by scaling down (so children > replicas exist) then deleting
    the set immediately. Verify children are eventually all gone.
    """
    manager = Manager()
    manager.register_kind(WorkloadSet)
    manager.register_kind(Workload)
    manager.add_controller(WorkloadSetReconciler())
    agent = Agent(fake_runtime, store=manager.store, state_dir=tmp_path / "agent")
    async with manager.running(), agent.running():
        # Create with 3 replicas, scale down to 1 (so v-1, v-2 are about
        # to become "stranded" in the reconciler's view), then delete the
        # set before scale-down has a chance to converge.
        _ = await manager.store.apply(_set("v", 3))
        await _wait_for_replicas(manager, "v", 3)

        _ = await manager.store.apply(_set("v", 1))
        await manager.store.delete(WorkloadSet, namespace="default", name="v")

        # All children must drain (regardless of ordinal vs replicas).
        # With the bug, v-1 and v-2 stay alive forever because the set
        # was hard-deleted before the reconciler enumerated them, and
        # subsequent reconciles bail with "set gone".
        await _wait_for_replicas(manager, "v", 0)


async def test_workloadset_owner_refs_cascade(tmp_path: Path, fake_runtime: FakeRuntime):
    manager = Manager()
    manager.register_kind(WorkloadSet)
    manager.register_kind(Workload)
    manager.add_controller(WorkloadSetReconciler())
    agent = Agent(fake_runtime, store=manager.store, state_dir=tmp_path / "agent")
    async with manager.running(), agent.running():
        _ = await manager.store.apply(_set("v", 2))
        await _wait_for_replicas(manager, "v", 2)

        # Children carry an owner_ref pointing back to the set, controller=True.
        children = manager.store.list(Workload, namespace="default")
        for c in children:
            assert any(
                ref.kind == "WorkloadSet" and ref.controller for ref in c.metadata.owner_refs
            )
