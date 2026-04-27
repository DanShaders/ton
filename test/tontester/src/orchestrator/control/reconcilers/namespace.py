"""Namespace cascade-delete reconciler.

When a Namespace gets a ``deletion_timestamp``, we must wait for every
child resource referencing it (by ``namespace`` field, not owner_ref —
namespaces are cluster-scoped so ownership-by-namespace is implicit)
to drain before hard-deleting the namespace row.

Implementation: a single finalizer ``orchestrator.io/namespace-cascade``
on every namespace. While the finalizer is present and
``deletion_timestamp`` is set, the reconciler:

1. Issues ``store.delete()`` for every resource in this namespace
   (other than the namespace itself).
2. Re-checks: if any child still exists (with or without deletion_ts),
   it requeues.
3. Once all children are gone, drops the finalizer; the store
   hard-deletes the namespace row on the next patch.

This is roughly what k8s' namespace controller does, minus the third-
party CRD discovery.
"""

import logging
from typing import final, override

from pydantic import BaseModel

from ...resources import (
    Metadata,
    Namespace,
    NetworkPolicy,
    PortForward,
    ResourceLike,
    Workload,
    WorkloadSet,
)
from ...store import InMemoryStore, WatchEvent
from ..controller import Controller, ItemRef, Result, WatchSpec, identity_mapper

logger = logging.getLogger(__name__)

NAMESPACE_FINALIZER = "orchestrator.io/namespace-cascade"

# Order matters: tear down sets before their workloads (so the set
# reconciler doesn't keep recreating kids), then policies, then the
# leaf workloads themselves.
_CHILD_KINDS = (WorkloadSet, NetworkPolicy, PortForward, Workload)


@final
class NamespaceReconciler(Controller[Namespace]):
    """Cascade-deletes a namespace's children before hard-deleting itself."""

    @property
    @override
    def owned_kind(self) -> type[Namespace]:
        return Namespace

    @property
    @override
    def watches(self) -> list[WatchSpec]:
        # Reconcile when child kinds change so we notice the last child
        # going away. We re-derive the namespace's name from the
        # resource's namespace via a small mapper.
        return [
            WatchSpec(resource_type=Namespace, map_event=identity_mapper),
            *(WatchSpec(resource_type=k, map_event=_child_to_namespace_ref) for k in _CHILD_KINDS),
        ]

    @override
    async def reconcile(self, store: InMemoryStore, ref: ItemRef) -> Result:
        ns = store.get_or_none(Namespace, namespace=None, name=ref.name)
        if ns is None:
            return Result(ok=True, reason="namespace gone")

        # Always ensure the cascade finalizer exists — this is the
        # invariant that lets graceful delete actually wait.
        if NAMESPACE_FINALIZER not in ns.metadata.finalizers:
            try:
                _ = await store.patch_metadata(
                    Namespace,
                    namespace=None,
                    name=ns.metadata.name,
                    mutator=_add_namespace_finalizer,
                )
            except Exception as e:
                logger.exception(f"failed to add finalizer to ns {ns.metadata.name}")
                return Result(ok=False, reason=f"finalizer write: {e}")
            return Result(ok=True, reason="finalizer added; will requeue on next event")

        if ns.metadata.deletion_timestamp is None:
            # Live namespace; nothing to do until someone deletes it.
            return Result(ok=True, reason="active")

        # Mark Terminating in status (idempotent).
        if ns.status.phase != "Terminating":
            try:
                _ = await store.patch_status(
                    Namespace,
                    namespace=None,
                    name=ns.metadata.name,
                    mutator=_mark_terminating,
                )
            except Exception:
                logger.exception(f"namespace {ns.metadata.name} status mark failed")

        # Issue deletes for every child kind. Each ``store.delete`` is
        # idempotent — already-deleted is a no-op — so we don't bother
        # tracking which ones we've sent.
        remaining = 0
        for kind in _CHILD_KINDS:
            children = store.list(kind, namespace=ns.metadata.name)
            for child in children:
                if child.metadata.deletion_timestamp is None:
                    await store.delete(
                        kind,
                        namespace=child.metadata.namespace,
                        name=child.metadata.name,
                    )
                remaining += 1

        if remaining > 0:
            # Pure event-driven: any child change fires a watch event
            # that wakes this controller via _child_to_namespace_ref.
            # No requeue_after — we don't need a timer to "check again
            # later"; the watch tells us when to look.
            return Result(ok=True, reason=f"awaiting {remaining} children")

        # Children drained — drop the finalizer; store hard-deletes.
        try:
            _ = await store.patch_metadata(
                Namespace,
                namespace=None,
                name=ns.metadata.name,
                mutator=_drop_namespace_finalizer,
            )
        except Exception as e:
            return Result(ok=False, reason=f"drop finalizer: {e}")
        return Result(ok=True, reason="cascade complete")


def _child_to_namespace_ref(
    event: WatchEvent[ResourceLike[BaseModel, BaseModel]],
) -> list[ItemRef]:
    """Map a child-kind watch event to a ``[ItemRef(name=event.namespace)]``.

    The cluster-scoped namespace's name lives in the child's
    ``metadata.namespace`` field — we re-derive the parent ref from
    there so the namespace controller wakes on any child change.
    """
    ns = event.resource.metadata.namespace
    if ns is None:
        return []
    return [ItemRef(namespace=None, name=ns)]


def _add_namespace_finalizer(meta: Metadata) -> None:
    if NAMESPACE_FINALIZER not in meta.finalizers:
        meta.finalizers.append(NAMESPACE_FINALIZER)


def _drop_namespace_finalizer(meta: Metadata) -> None:
    if NAMESPACE_FINALIZER in meta.finalizers:
        meta.finalizers.remove(NAMESPACE_FINALIZER)


def _mark_terminating(ns: Namespace) -> None:
    ns.status.phase = "Terminating"
