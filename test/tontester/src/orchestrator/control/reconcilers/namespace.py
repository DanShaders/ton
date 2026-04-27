"""Namespace cascade-delete reconciler.

When a Namespace gets a ``deletion_timestamp``, the
:class:`CascadingReconciler` base handles the bookkeeping —
finalizer install, children enumeration + delete, finalizer drop —
and this module supplies the namespace-specific bits:

- which kinds count as namespace children (every namespaced kind),
- enumeration via ``store.list(kind, namespace=ns.name)``,
- a status-write hook that flips ``status.phase = "Terminating"``.
"""

import logging
from collections.abc import Iterable
from typing import final, override

from pydantic import BaseModel

from ...resources import (
    Namespace,
    NetworkPolicy,
    PortForward,
    ResourceLike,
    Workload,
    WorkloadSet,
)
from ...store import InMemoryStore, WatchEvent
from ..cascading import CascadingReconciler
from ..controller import ItemRef, WatchSpec, identity_mapper

logger = logging.getLogger(__name__)

NAMESPACE_FINALIZER = "orchestrator.io/namespace-cascade"

# Order matters: tear down sets before their workloads (so the set
# reconciler doesn't keep recreating kids), then policies, then the
# leaf workloads themselves.
_CHILD_KINDS: tuple[type[ResourceLike[BaseModel, BaseModel]], ...] = (
    WorkloadSet,
    NetworkPolicy,
    PortForward,
    Workload,
)


@final
class NamespaceReconciler(CascadingReconciler[Namespace]):
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

    @property
    @override
    def finalizer_name(self) -> str:
        return NAMESPACE_FINALIZER

    @override
    def enumerate_children(
        self,
        store: InMemoryStore,
        parent: Namespace,
    ) -> Iterable[
        tuple[type[ResourceLike[BaseModel, BaseModel]], ResourceLike[BaseModel, BaseModel]]
    ]:
        for kind in _CHILD_KINDS:
            for child in store.list(kind, namespace=parent.metadata.name):
                yield (kind, child)

    @override
    async def on_terminating_entered(self, store: InMemoryStore, parent: Namespace) -> None:
        if parent.status.phase == "Terminating":
            return
        try:
            _ = await store.patch_status(
                Namespace,
                namespace=None,
                name=parent.metadata.name,
                mutator=_mark_terminating,
            )
        except Exception:
            logger.exception(f"namespace {parent.metadata.name} status mark failed")


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


def _mark_terminating(ns: Namespace) -> None:
    ns.status.phase = "Terminating"
