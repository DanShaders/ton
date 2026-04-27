"""Store invariants — uid, generation, resource_version, optimistic
concurrency, finalizers, status / spec separation.

These are the hard guarantees the rest of the orchestrator depends on.
The tests are deliberately mechanical.
"""

import asyncio

import pytest
from orchestrator import (
    AlreadyExists,
    Conflict,
    Container,
    HostBinaryImage,
    InMemoryStore,
    Metadata,
    Namespace,
    NotFound,
    ValidationError,
    Workload,
    WorkloadSpec,
)

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


def _wl(name: str, *, namespace: str | None = "default", env: str = "x") -> Workload:
    return Workload(
        metadata=Metadata(name=name, namespace=namespace),
        spec=WorkloadSpec(
            containers=[
                Container(
                    name="primary",
                    image=HostBinaryImage(path="/bin/true"),
                    env={"E": env},
                )
            ],
        ),
    )


async def test_apply_assigns_uid_generation_and_rv(store: InMemoryStore):
    wl = await store.apply(_wl("one"))
    assert wl.metadata.uid != ""
    assert wl.metadata.generation == 1
    assert wl.metadata.resource_version >= 1
    assert wl.metadata.creation_timestamp is not None


async def test_apply_update_bumps_generation_only_on_spec_change(store: InMemoryStore):
    initial = await store.apply(_wl("a", env="v1"))
    again_same = await store.apply(_wl("a", env="v1"))
    assert again_same.metadata.generation == initial.metadata.generation
    assert again_same.metadata.resource_version > initial.metadata.resource_version

    changed = await store.apply(_wl("a", env="v2"))
    assert changed.metadata.generation == initial.metadata.generation + 1


async def test_apply_optimistic_concurrency(store: InMemoryStore):
    initial = await store.apply(_wl("a"))
    rv = initial.metadata.resource_version

    # Wrong version raises Conflict.
    with pytest.raises(Conflict) as exc:
        _ = await store.apply(_wl("a", env="v2"), expected_version=rv - 1)
    assert exc.value.actual == rv

    # Right version succeeds.
    after = await store.apply(_wl("a", env="v2"), expected_version=rv)
    assert after.metadata.generation == initial.metadata.generation + 1


async def test_apply_with_expected_zero_on_missing_creates(store: InMemoryStore):
    created = await store.apply(_wl("new"), expected_version=0)
    assert created.metadata.generation == 1


async def test_apply_with_expected_nonzero_on_missing_raises(store: InMemoryStore):
    with pytest.raises(Conflict):
        _ = await store.apply(_wl("new"), expected_version=5)


async def test_create_errors_on_existing(store: InMemoryStore):
    _ = await store.create(_wl("a"))
    with pytest.raises(AlreadyExists):
        _ = await store.create(_wl("a"))


async def test_get_not_found_raises_typed(store: InMemoryStore):
    with pytest.raises(NotFound) as exc:
        _ = store.get(Workload, namespace="default", name="missing")
    assert exc.value.name == "missing"


async def test_status_writer_cannot_change_spec(store: InMemoryStore):
    _ = await store.apply(_wl("a", env="v1"))

    def _bad_mutator(w: Workload) -> None:
        # Status patch tries to also write spec — must be rejected.
        w.status.phase = "Running"
        w.spec.containers[0].env = {"E": "v9"}

    with pytest.raises(ValidationError):
        _ = await store.patch_status(Workload, namespace="default", name="a", mutator=_bad_mutator)


async def test_status_writer_succeeds_on_status_only(store: InMemoryStore):
    initial = await store.apply(_wl("a"))

    def _good(w: Workload) -> None:
        w.status.phase = "Running"
        w.status.host = "test"

    after = await store.patch_status(Workload, namespace="default", name="a", mutator=_good)
    assert after.status.phase == "Running"
    assert after.status.host == "test"
    # Generation unchanged — only spec changes bump it.
    assert after.metadata.generation == initial.metadata.generation
    # resource_version bumped — the row itself was rewritten.
    assert after.metadata.resource_version > initial.metadata.resource_version


async def test_apply_preserves_status(store: InMemoryStore):
    """Status writes survive subsequent apply() (which only touches spec)."""
    _ = await store.apply(_wl("a"))

    def _set_running(w: Workload) -> None:
        w.status.phase = "Running"

    _ = await store.patch_status(Workload, namespace="default", name="a", mutator=_set_running)

    after = await store.apply(_wl("a", env="changed"))
    assert after.status.phase == "Running"


async def test_delete_with_no_finalizers_hard_deletes(store: InMemoryStore):
    _ = await store.apply(_wl("a"))
    await store.delete(Workload, namespace="default", name="a")
    assert store.get_or_none(Workload, namespace="default", name="a") is None


async def test_delete_with_finalizers_marks_terminating(store: InMemoryStore):
    wl = _wl("a")
    wl.metadata.finalizers.append("test/finalizer")
    _ = await store.apply(wl)

    await store.delete(Workload, namespace="default", name="a")
    after = store.get(Workload, namespace="default", name="a")
    assert after.metadata.deletion_timestamp is not None

    # Drop finalizer; row goes away on next patch_metadata.
    def _drop(meta: Metadata) -> None:
        meta.finalizers.clear()

    _ = await store.patch_metadata(Workload, namespace="default", name="a", mutator=_drop)
    assert store.get_or_none(Workload, namespace="default", name="a") is None


async def test_uid_stable_across_updates(store: InMemoryStore):
    a = await store.apply(_wl("a"))
    b = await store.apply(_wl("a", env="v2"))
    assert a.metadata.uid == b.metadata.uid


async def test_concurrent_applies_serialize_per_kind(store: InMemoryStore):
    """Two concurrent applies on the same row must not interleave —
    the kind lock serializes them so resource_version is monotonic.
    """
    initial = await store.apply(_wl("a", env="seed"))
    rv0 = initial.metadata.resource_version

    async def _writer(env: str) -> int:
        result = await store.apply(_wl("a", env=env))
        return result.metadata.resource_version

    rvs = await asyncio.gather(
        *(_writer(f"e{i}") for i in range(20)),
    )
    # Every rv > rv0; no duplicates.
    assert all(rv > rv0 for rv in rvs)
    assert len(set(rvs)) == len(rvs)


async def test_returned_objects_are_isolated_from_store(store: InMemoryStore):
    """Mutating a returned resource must not affect the stored row."""
    out = await store.apply(_wl("a"))
    out.metadata.labels["evil"] = "yes"
    out.spec.containers[0].env["BAD"] = "1"
    fresh = store.get(Workload, namespace="default", name="a")
    assert "evil" not in fresh.metadata.labels
    assert "BAD" not in fresh.spec.containers[0].env


async def test_list_with_label_selector(store: InMemoryStore):
    wl_v = _wl("v1")
    wl_v.metadata.labels = {"role": "validator"}
    wl_p = _wl("p1")
    wl_p.metadata.labels = {"role": "prom"}
    _ = await store.apply(wl_v)
    _ = await store.apply(wl_p)

    from orchestrator import LabelSelector

    only_v = store.list(Workload, selector=LabelSelector(match_labels={"role": "validator"}))
    assert len(only_v) == 1
    assert only_v[0].metadata.name == "v1"


async def test_namespace_separation(store: InMemoryStore):
    _ = await store.apply(_wl("a", namespace="ns1"))
    _ = await store.apply(_wl("a", namespace="ns2"))
    in_ns1 = store.list(Workload, namespace="ns1")
    in_ns2 = store.list(Workload, namespace="ns2")
    assert len(in_ns1) == 1
    assert len(in_ns2) == 1
    assert in_ns1[0].metadata.namespace == "ns1"
    assert in_ns2[0].metadata.namespace == "ns2"


async def test_unregistered_kind_raises():
    """Calling get on a kind that wasn't registered surfaces clearly."""
    fresh = InMemoryStore()
    with pytest.raises(ValidationError):
        _ = fresh.get(Namespace, namespace=None, name="x")


async def test_apply_preserves_finalizers(store: InMemoryStore):
    """Caught while debugging: apply() was using ``desired.metadata`` for
    every metadata field, which clobbered the finalizers a controller
    had previously installed. That silently disabled the agent's
    cleanup finalizer the moment a user re-applied the workload's
    spec — graceful delete then no-op'd.

    Apply should preserve controller-managed metadata (finalizers,
    owner_refs); only labels/annotations come from desired.
    """
    initial = _wl("a")
    initial.metadata.finalizers = ["controller-A/cleanup", "controller-B/cleanup"]
    _ = await store.apply(initial)

    # User re-applies with their own version, no finalizers in their
    # workload object — they don't manage finalizers.
    user_apply = _wl("a", env="v2")
    assert user_apply.metadata.finalizers == []
    after = await store.apply(user_apply)

    assert after.metadata.finalizers == ["controller-A/cleanup", "controller-B/cleanup"]


async def test_apply_preserves_owner_refs(store: InMemoryStore):
    """Same shape as finalizers — owner_refs are set by parent
    reconcilers (e.g. WorkloadSet creating its child Workloads) and
    must survive a user's spec apply, otherwise cascade delete breaks.
    """
    from orchestrator import OwnerRef

    parent_ref = OwnerRef(
        api_version="orchestrator/v1",
        kind="WorkloadSet",
        name="my-set",
        uid="parent-uid-123",
        controller=True,
    )
    initial = _wl("a")
    initial.metadata.owner_refs = [parent_ref]
    _ = await store.apply(initial)

    user_apply = _wl("a", env="v2")
    assert user_apply.metadata.owner_refs == []
    after = await store.apply(user_apply)

    assert after.metadata.owner_refs == [parent_ref]
