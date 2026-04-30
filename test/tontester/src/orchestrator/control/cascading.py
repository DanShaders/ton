"""Base reconciler for parents that own children via cascade-delete.

The recurring shape across NamespaceReconciler and WorkloadSetReconciler:

1. **Install a finalizer on the parent** so the store can't hard-delete
   the row before children are drained.
2. **On deletion_timestamp**, enumerate children, issue
   :meth:`InMemoryStore.delete` for each, count remaining. If non-zero,
   return ``ok`` and let the watch fire when a child changes.
3. **When children drained**, drop the finalizer; the store hard-
   deletes the parent row.
4. **Otherwise (live parent)**, call subclass-defined active reconcile
   to bring children into desired shape.

Forgetting the finalizer (Bug 4 — original WorkloadSetReconciler) is
no longer possible: the base class always installs it on first
reconcile. Forgetting cascade-delete enumeration is no longer possible:
the base class always runs it. The subclass implements only the
domain-specific bits — *which* kind to own, *how* to enumerate
children, and what *active* reconcile does.
"""

import logging
from abc import ABC, abstractmethod
from collections.abc import Iterable
from typing import override

from pydantic import BaseModel

from ..resources import Metadata, ResourceLike
from ..store import InMemoryStore
from .controller import Controller, ItemRef, Result

logger = logging.getLogger(__name__)

_AnyResource = ResourceLike[BaseModel, BaseModel]


# Invariant: CascadingReconciler's methods both consume and return
# the parent type, so it can't be covariant. Concrete subclasses fix
# the parameter (NamespaceReconciler is CascadingReconciler[Namespace],
# not assignable to CascadingReconciler[Resource]).
class CascadingReconciler[T: _AnyResource](Controller[T], ABC):
    """Reconciler base that handles finalizer + cascade-delete uniformly.

    Subclasses implement:

    - :attr:`finalizer_name` — string identifying this reconciler's
      finalizer on the parent.
    - :meth:`enumerate_children` — yield every child of a given parent,
      paired with its resource type, so the base can issue typed
      :meth:`InMemoryStore.delete` calls.
    - :meth:`reconcile_active` — domain-specific work for the live
      parent (e.g. WorkloadSet's "ensure N replicas with current spec").
      Optional override; default is a no-op ``Result(ok=True)``.
    - :meth:`on_terminating_entered` — optional hook called once on
      first reconcile after deletion_timestamp is set. Used by
      NamespaceReconciler to mark ``status.phase = "Terminating"``.

    The base class supplies :meth:`reconcile`, which Controller's
    runner calls. Subclasses should *not* override ``reconcile`` —
    overriding it bypasses the cascade machinery and reintroduces
    Bug 4's failure shape.
    """

    @property
    @abstractmethod
    def finalizer_name(self) -> str: ...

    @abstractmethod
    def enumerate_children(
        self, store: InMemoryStore, parent: T
    ) -> Iterable[tuple[type[_AnyResource], _AnyResource]]: ...

    async def reconcile_active(self, store: InMemoryStore, parent: T) -> Result:
        """Live-parent reconcile. Default: nothing to do."""
        _ = (store, parent)
        return Result(ok=True, reason="active")

    async def on_terminating_entered(self, store: InMemoryStore, parent: T) -> None:
        """Called once on first reconcile after deletion_timestamp is set.

        Default: no-op. Override to write status.phase or similar.
        Idempotency is the override's responsibility — this is called
        on every cascade reconcile pass.
        """
        _ = (store, parent)

    @override
    async def reconcile(self, store: InMemoryStore, ref: ItemRef) -> Result:
        parent = store.get_or_none(self.owned_kind, namespace=ref.namespace, name=ref.name)
        if parent is None:
            return Result(ok=True, reason="parent gone")

        # Always ensure the finalizer first. Without this, store.delete
        # would hard-delete the row before we got a chance to drain
        # children — the namespace reconciler discovered this; this base
        # class enforces it for every cascading reconciler.
        if self.finalizer_name not in parent.metadata.finalizers:
            try:
                _ = await store.patch_metadata(
                    self.owned_kind,
                    namespace=ref.namespace,
                    name=ref.name,
                    mutator=_add_finalizer(self.finalizer_name),
                )
            except Exception as e:
                logger.exception(
                    f"failed to add finalizer to {self.owned_kind.__name__}/{ref.name}"
                )
                return Result(ok=False, reason=f"finalizer write: {e}")
            return Result(ok=True, reason="finalizer added; will requeue on next event")

        if parent.metadata.deletion_timestamp is None:
            return await self.reconcile_active(store, parent)

        await self.on_terminating_entered(store, parent)

        # Count children that are still present. ``store.delete`` is
        # idempotent: if a child has its own finalizer we won't see it
        # vanish until that finalizer drops, but we'll keep being woken
        # by watch events on the child kind until that happens.
        remaining = 0
        for kind, child in self.enumerate_children(store, parent):
            if child.metadata.deletion_timestamp is None:
                await store.delete(
                    kind,
                    namespace=child.metadata.namespace,
                    name=child.metadata.name,
                )
            remaining += 1

        if remaining > 0:
            return Result(ok=True, reason=f"awaiting {remaining} children")

        try:
            _ = await store.patch_metadata(
                self.owned_kind,
                namespace=ref.namespace,
                name=ref.name,
                mutator=_drop_finalizer(self.finalizer_name),
            )
        except Exception as e:
            return Result(ok=False, reason=f"drop finalizer: {e}")
        return Result(ok=True, reason="cascade complete")


def _add_finalizer(name: str):
    def _mutate(meta: Metadata) -> None:
        if name not in meta.finalizers:
            meta.finalizers.append(name)

    return _mutate


def _drop_finalizer(name: str):
    def _mutate(meta: Metadata) -> None:
        if name in meta.finalizers:
            meta.finalizers.remove(name)

    return _mutate
