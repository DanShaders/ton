"""WorkloadSet → child Workloads reconciler.

For each WorkloadSet, ensure exactly ``replicas`` child Workloads exist,
named ``f"{set.name}-{ordinal}"`` for ordinals ``[0, replicas)``, with
the set's template baked into their spec and an owner_ref pointing at
the set.

Update strategy in this prototype: **Recreate**. When the template's
spec changes (set.metadata.generation bumps), every child is deleted
and recreated. Rolling updates are a TODO.

The reconciler also reports ``status.replicas`` and
``status.ready_replicas`` (from each Workload's ``Available`` condition,
written by the agent).
"""

import logging
from datetime import datetime, timezone
from typing import final, override

from ...errors import AlreadyExists, NotFound
from ...ids import child_name
from ...resources import (
    Condition,
    Metadata,
    OwnerRef,
    Workload,
    WorkloadSet,
    WorkloadSpec,
    WorkloadStatus,
    upsert_condition,
)
from ...store import InMemoryStore
from ..controller import Controller, ItemRef, Result, WatchSpec, owner_mapper

logger = logging.getLogger(__name__)


def _now() -> datetime:
    return datetime.now(timezone.utc)


@final
class WorkloadSetReconciler(Controller[WorkloadSet]):
    """Ensures the set's child Workloads match its template + replica count."""

    @property
    @override
    def owned_kind(self) -> type[WorkloadSet]:
        return WorkloadSet

    @property
    @override
    def watches(self) -> list[WatchSpec]:
        return [
            WatchSpec(resource_type=Workload, map_event=owner_mapper("WorkloadSet")),
        ]

    @override
    async def reconcile(self, store: InMemoryStore, ref: ItemRef) -> Result:
        wset = store.get_or_none(WorkloadSet, namespace=ref.namespace, name=ref.name)
        if wset is None:
            return Result(ok=True, reason="set gone")

        if wset.metadata.deletion_timestamp is not None:
            # Cascade: delete every child; the namespace controller
            # also does this for namespace-scoped tear-downs, but a
            # WorkloadSet might be deleted independently of its
            # namespace.
            for ordinal in range(wset.spec.replicas):
                await store.delete(
                    Workload,
                    namespace=ref.namespace,
                    name=child_name(ref.name, ordinal),
                )
            return Result(ok=True, reason="set terminating")

        existing = {
            w.metadata.name: w
            for w in store.list(Workload, namespace=ref.namespace)
            if _is_child_of(w, wset)
        }

        # Bring children into sync with desired count + template spec.
        ready = 0
        replicas_observed = 0
        for ordinal in range(wset.spec.replicas):
            cname = child_name(ref.name, ordinal)
            child = existing.pop(cname, None)
            if child is None:
                await _create_child(store, wset, ordinal)
                replicas_observed += 1
                continue
            replicas_observed += 1
            if _spec_drift(child.spec, wset.spec.template.spec):
                await _replace_child(store, wset, ordinal, child)
            elif _is_ready(child):
                ready += 1

        # Anything left in `existing` is past the current replica count
        # — delete it (scale-down).
        for stranded in existing.values():
            await store.delete(
                Workload,
                namespace=ref.namespace,
                name=stranded.metadata.name,
            )

        await _write_set_status(
            store,
            wset,
            replicas=replicas_observed,
            ready_replicas=ready,
        )
        return Result(ok=True, reason="reconciled")


# --- helpers --------------------------------------------------------------


def _is_child_of(workload: Workload, wset: WorkloadSet) -> bool:
    for ref in workload.metadata.owner_refs:
        if ref.kind == "WorkloadSet" and ref.uid == wset.metadata.uid:
            return True
    return False


def _spec_drift(child_spec: WorkloadSpec, template_spec: WorkloadSpec) -> bool:
    return child_spec.model_dump() != template_spec.model_dump()


def _is_ready(workload: Workload) -> bool:
    for c in workload.status.conditions:
        if c.type == "Available" and c.status == "True":
            return True
    return False


async def _create_child(store: InMemoryStore, wset: WorkloadSet, ordinal: int) -> None:
    name = child_name(wset.metadata.name, ordinal)
    template = wset.spec.template
    labels = dict(template.labels) | {"ordinal": str(ordinal), "set": wset.metadata.name}
    annotations = dict(template.annotations)
    workload = Workload(
        metadata=_build_child_metadata(wset, name, labels, annotations),
        spec=template.spec.model_copy(deep=True),
        status=WorkloadStatus(),
    )
    try:
        _ = await store.create(workload)
    except AlreadyExists:
        # Race with another reconcile cycle; the next pass will diff.
        return


async def _replace_child(
    store: InMemoryStore,
    wset: WorkloadSet,
    ordinal: int,
    existing: Workload,
) -> None:
    name = child_name(wset.metadata.name, ordinal)
    # Recreate strategy: delete first, then create. Concurrent applies
    # against the store are serialized per-key by the kind lock, so the
    # delete event is processed before the create.
    await store.delete(Workload, namespace=wset.metadata.namespace, name=name)
    template = wset.spec.template
    labels = dict(template.labels) | {"ordinal": str(ordinal), "set": wset.metadata.name}
    annotations = dict(template.annotations)
    fresh = Workload(
        metadata=_build_child_metadata(wset, name, labels, annotations),
        spec=template.spec.model_copy(deep=True),
        status=WorkloadStatus(),
    )
    try:
        _ = await store.create(fresh)
    except AlreadyExists:
        # The agent's finalizer may delay hard-delete past the time we
        # try to create. Fall back to apply (which updates) — the
        # delete will eventually happen, but for now we want the new
        # spec live. Real fix: wait for the DELETED event before create.
        try:
            _ = await store.apply(fresh)
        except Exception:
            logger.exception(f"replace child {name} fell back to apply and still failed")
            _ = existing  # silence unused


def _build_child_metadata(
    wset: WorkloadSet,
    name: str,
    labels: dict[str, str],
    annotations: dict[str, str],
) -> Metadata:
    return Metadata(
        name=name,
        namespace=wset.metadata.namespace,
        labels=labels,
        annotations=annotations,
        owner_refs=[
            OwnerRef(
                api_version=wset.api_version,
                kind=wset.kind,
                name=wset.metadata.name,
                uid=wset.metadata.uid,
                controller=True,
                block_owner_deletion=True,
            )
        ],
    )


async def _write_set_status(
    store: InMemoryStore,
    wset: WorkloadSet,
    *,
    replicas: int,
    ready_replicas: int,
) -> None:
    gen = wset.metadata.generation

    def _apply(s: WorkloadSet) -> None:
        s.status.replicas = replicas
        s.status.ready_replicas = ready_replicas
        s.status.observed_generation = gen
        ready_status = "True" if ready_replicas == s.spec.replicas else "False"
        s.status.conditions = upsert_condition(
            s.status.conditions,
            Condition(
                type="Ready",
                status=ready_status,
                reason="Reconciled",
                message=f"{ready_replicas}/{s.spec.replicas} ready",
                last_transition=_now(),
                observed_generation=gen,
            ),
        )

    try:
        _ = await store.patch_status(
            WorkloadSet,
            namespace=wset.metadata.namespace,
            name=wset.metadata.name,
            mutator=_apply,
        )
    except NotFound:
        pass
