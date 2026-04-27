"""In-memory typed store for orchestrator resources.

One :class:`InMemoryStore` per orchestrator process. State lives in
``dict``s and an ``asyncio.Queue``-backed pub/sub bus; nothing touches
disk. Crash semantics: process restart wipes everything (per the
"orchestrator crash = payload crash" contract).

**Race-resistance design.**

The store is the single writer. Every mutation goes through one
entrypoint (``apply``, ``patch_status``, ``delete``, ``patch_metadata``)
which acquires the per-kind lock, performs read-modify-write, bumps the
counters, and publishes the watch event — all without yielding. Callers
never assemble a "diff" themselves: they pass either a full new spec
(``apply``) or a typed mutator (``patch_status``). This kills two
classic race shapes in one stroke:

- "I read v=5, computed an update, wrote it back. Meanwhile someone
  wrote v=6 — my write silently overwrote theirs." → impossible because
  the mutator runs under the lock against the latest state.
- "I observed status A then status B from the watch — but status A's
  conditions actually came after B." → impossible because publish is
  inside the lock and the watch bus preserves enqueue order.

Three counters per kind:

- ``generation`` — bumped only when ``spec`` changes; carried into
  ``status.observed_generation`` so controllers can detect catch-up.
- ``resource_version`` — bumped on any write; the watch ordering key.
  Optimistic concurrency on ``apply`` checks the caller-supplied version
  against this.
- ``uid`` — assigned on first apply, never reused. Owner refs key off it.

Spec/status separation is enforced at the store API: ``apply`` writes
spec/metadata-but-finalizers, ``patch_status`` writes status, and
``patch_metadata`` writes only metadata bookkeeping (labels,
annotations, finalizers).

**Why no api_version/kind introspection.** The store keys ``_KindState``
by the resource *class* itself. The ``api_version`` / ``kind`` strings
live on resource *instances* (each concrete subclass declares them as
``Literal`` defaults) and are read off instances when needed for owner
refs or wire serialization. Internally the store never needs to derive
those strings from a class — it dispatches on ``type(resource)`` or
``resource_type`` parameters directly.
"""

import asyncio
import copy
from collections.abc import AsyncGenerator, AsyncIterator, Callable, Iterator
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Generic, TypeVar, final

from pydantic import BaseModel

from ..errors import AlreadyExists, Conflict, NotFound, ValidationError
from ..ids import new_uid
from ..resources import (
    LabelSelector,
    Metadata,
    ResourceLike,
    matches,
)
from .watch import Subscription as BusSubscription
from .watch import WatchBus, WatchEvent, WatchEventType

# Internal storage erases to the kind-erased Protocol; public API
# methods are parameterized in ``_TRes`` and narrow back via
# ``isinstance(row, resource_type)``.
_AnyResource = ResourceLike[BaseModel, BaseModel]

# Bound on the Protocol so we can access .spec / .status (read-only,
# covariant) inside helpers. Concrete subclasses (Workload, Namespace,
# ...) substitute via covariance — they all both inherit Resource for
# pydantic discriminator behavior AND structurally satisfy ResourceLike.
_TRes = TypeVar("_TRes", bound=_AnyResource)


@final
class Subscription(Generic[_TRes]):
    """Async-context-manager-and-iterable handle to a watch subscription.

    Synchronously registers with the bus on construction (so events
    published immediately afterward are observed). The two halves —
    iteration and cleanup — are bound together by ``async with`` so a
    caller can't forget to close::

        async with store.subscription(Workload) as sub:
            async for event in sub:
                ...

    Direct construction is internal — use :meth:`InMemoryStore.subscription`.
    The bus operates on the kind-erased base; the ``resource_type`` we
    carry alongside is what narrows events on the way out. _TRes is a
    phantom parameter — concrete iteration yields ``WatchEvent[_TRes]``
    even though the underlying bus token is parameterized on the base.
    """

    def __init__(
        self,
        bus: "WatchBus[_AnyResource]",
        resource_type: type[_TRes],
    ):
        self._bus: WatchBus[_AnyResource] = bus
        self._token: BusSubscription[_AnyResource] = bus.open()
        self._resource_type: type[_TRes] = resource_type
        self._closed: bool = False
        # Subscriptions are single-use. The bus token is consumed by
        # the first ``async for``; calling ``__aiter__`` a second time
        # would silently yield zero events because the underlying
        # token's queue has been drained or closed. Raise instead.
        self._iterated: bool = False

    @property
    def resource_type(self) -> type[_TRes]:
        return self._resource_type

    async def __aenter__(self) -> "Subscription[_TRes]":
        return self

    async def __aexit__(
        self,
        _exc_type: object,
        _exc: object,
        _tb: object,
    ) -> None:
        self.close()

    async def __aiter__(self) -> AsyncGenerator[WatchEvent[_TRes]]:
        if self._iterated:
            raise RuntimeError(
                (
                    "Subscription is single-use; open a fresh one via "
                    "store.subscription(...) instead of iterating twice"
                )
            )
        self._iterated = True
        async for event in self._bus.stream(self._token):
            yield WatchEvent(
                type=event.type,
                resource=_narrow(event.resource, self._resource_type),
                resource_version=event.resource_version,
            )

    def close(self) -> None:
        """Unregister from the bus + close the token. Idempotent.

        Calls :meth:`WatchBus.unregister` which both closes the per-sub
        queue and removes the token from the bus's ``_subscriptions``
        set. Without the discard half, "open via context manager but
        never iterate" would leak the token until the bus saw a publish
        whose ``offer`` returned False — which on a quiet bus is never.
        """
        if self._closed:
            return
        self._closed = True
        self._bus.unregister(self._token)


def _now() -> datetime:
    return datetime.now(timezone.utc)


@dataclass
class _KindState:
    """Per-kind storage. The class identity is the dict key in
    :class:`InMemoryStore`, so we don't need to store strings here."""

    resource_type: type[_AnyResource]
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    next_resource_version: int = 1
    rows: dict[tuple[str | None, str], _AnyResource] = field(default_factory=dict)
    bus: WatchBus[_AnyResource] = field(init=False)

    def __post_init__(self) -> None:
        self.bus = WatchBus(self.resource_type.__name__)


@final
class InMemoryStore:
    """Typed kind-indexed store with watch.

    Register every kind up-front via :meth:`register_kind` before the
    manager starts reconcilers. After that, the kind's
    :class:`_KindState` is stable and locks live for the store's
    lifetime.
    """

    def __init__(self) -> None:
        self._kinds: dict[type[_AnyResource], _KindState] = {}

    # ---- registration ---------------------------------------------------

    def register_kind(self, resource_type: type[_AnyResource]) -> None:
        if resource_type in self._kinds:
            return
        self._kinds[resource_type] = _KindState(resource_type=resource_type)

    def kinds(self) -> Iterator[type[_AnyResource]]:
        yield from self._kinds.keys()

    # ---- read paths -----------------------------------------------------

    def get(self, resource_type: type[_TRes], *, namespace: str | None, name: str) -> _TRes:
        state = self._state_for(resource_type)
        row = state.rows.get((namespace, name))
        if row is None:
            raise NotFound(kind=resource_type.__name__, namespace=namespace, name=name)
        return _narrow(row.model_copy(deep=True), resource_type)

    def get_or_none(
        self, resource_type: type[_TRes], *, namespace: str | None, name: str
    ) -> _TRes | None:
        state = self._state_for(resource_type)
        row = state.rows.get((namespace, name))
        if row is None:
            return None
        return _narrow(row.model_copy(deep=True), resource_type)

    def list(
        self,
        resource_type: type[_TRes],
        *,
        namespace: str | None = None,
        selector: LabelSelector | None = None,
    ) -> list[_TRes]:
        state = self._state_for(resource_type)
        out: list[_TRes] = []
        sel = selector or LabelSelector()
        for (ns, _), row in state.rows.items():
            if namespace is not None and ns != namespace:
                continue
            if not sel.is_empty() and not matches(sel, row.metadata.labels):
                continue
            out.append(_narrow(row.model_copy(deep=True), resource_type))
        return out

    # ---- write paths ----------------------------------------------------

    async def apply(self, desired: _TRes, *, expected_version: int | None = None) -> _TRes:
        """Create or update spec + writable metadata fields.

        - On create, allocates uid, sets ``creation_timestamp``,
          ``generation=1``, ``resource_version=N``.
        - On update, requires ``expected_version`` to match the current
          ``resource_version`` if supplied; raises :class:`Conflict`
          otherwise. Bumps ``generation`` only if the spec actually
          changed; bumps ``resource_version`` unconditionally.

        Status is preserved across applies — it's owned by the
        controller side and the only path to write it is
        :meth:`patch_status`.
        """
        kind_type = type(desired)
        state = self._state_for(kind_type)
        async with state.lock:
            key = (desired.metadata.namespace, desired.metadata.name)
            existing = state.rows.get(key)
            if existing is None:
                if expected_version is not None and expected_version != 0:
                    raise Conflict(
                        kind=kind_type.__name__,
                        namespace=desired.metadata.namespace,
                        name=desired.metadata.name,
                        expected=expected_version,
                        actual=0,
                    )
                stored = self._create(state, desired)
                ev_type = WatchEventType.ADDED
            else:
                if (
                    expected_version is not None
                    and expected_version != existing.metadata.resource_version
                ):
                    raise Conflict(
                        kind=kind_type.__name__,
                        namespace=desired.metadata.namespace,
                        name=desired.metadata.name,
                        expected=expected_version,
                        actual=existing.metadata.resource_version,
                    )
                stored = self._update_spec(state, existing, desired)
                ev_type = WatchEventType.MODIFIED
            state.bus.publish(
                WatchEvent(
                    type=ev_type,
                    resource=stored.model_copy(deep=True),
                    resource_version=stored.metadata.resource_version,
                )
            )
        return _narrow(stored.model_copy(deep=True), kind_type)

    async def create(self, desired: _TRes) -> _TRes:
        """Like :meth:`apply` but errors if the object already exists."""
        kind_type = type(desired)
        state = self._state_for(kind_type)
        async with state.lock:
            key = (desired.metadata.namespace, desired.metadata.name)
            if key in state.rows:
                raise AlreadyExists(
                    kind=kind_type.__name__,
                    namespace=desired.metadata.namespace,
                    name=desired.metadata.name,
                )
            stored = self._create(state, desired)
            state.bus.publish(
                WatchEvent(
                    type=WatchEventType.ADDED,
                    resource=stored.model_copy(deep=True),
                    resource_version=stored.metadata.resource_version,
                )
            )
        return _narrow(stored.model_copy(deep=True), kind_type)

    async def patch_status(
        self,
        resource_type: type[_TRes],
        *,
        namespace: str | None,
        name: str,
        mutator: Callable[[_TRes], None],
    ) -> _TRes:
        """Run ``mutator`` against the live status under the kind lock.

        ``mutator`` receives a *deep copy* of the row and may mutate
        ``status.*`` and ``metadata.{labels, annotations, finalizers}``
        in place. Spec writes are rejected (raises
        :class:`ValidationError`) — only the spec-write paths can
        change spec.
        """
        state = self._state_for(resource_type)
        async with state.lock:
            existing = state.rows.get((namespace, name))
            if existing is None:
                raise NotFound(kind=resource_type.__name__, namespace=namespace, name=name)
            draft = existing.model_copy(deep=True)
            spec_before = draft.spec.model_dump()
            mutator(_narrow(draft, resource_type))
            if draft.spec.model_dump() != spec_before:
                raise ValidationError(
                    f"patch_status mutator changed spec on {resource_type.__name__}/{name}"
                )
            stored = self._commit_status(state, existing, draft)
            state.bus.publish(
                WatchEvent(
                    type=WatchEventType.MODIFIED,
                    resource=stored.model_copy(deep=True),
                    resource_version=stored.metadata.resource_version,
                )
            )
        return _narrow(stored.model_copy(deep=True), resource_type)

    async def delete(
        self,
        resource_type: type[_TRes],
        *,
        namespace: str | None,
        name: str,
    ) -> None:
        """Mark for deletion. Hard-deletes only when finalizers are empty."""
        state = self._state_for(resource_type)
        async with state.lock:
            existing = state.rows.get((namespace, name))
            if existing is None:
                return
            if existing.metadata.finalizers:
                if existing.metadata.deletion_timestamp is not None:
                    return
                draft = existing.model_copy(deep=True)
                draft.metadata.deletion_timestamp = _now()
                stored = self._bump_version(state, draft)
                state.rows[(namespace, name)] = stored
                state.bus.publish(
                    WatchEvent(
                        type=WatchEventType.MODIFIED,
                        resource=stored.model_copy(deep=True),
                        resource_version=stored.metadata.resource_version,
                    )
                )
                return
            del state.rows[(namespace, name)]
            rv = state.next_resource_version
            state.next_resource_version += 1
            state.bus.publish(
                WatchEvent(
                    type=WatchEventType.DELETED,
                    resource=existing.model_copy(deep=True),
                    resource_version=rv,
                )
            )

    async def patch_metadata(
        self,
        resource_type: type[_TRes],
        *,
        namespace: str | None,
        name: str,
        mutator: Callable[[Metadata], None],
    ) -> _TRes:
        """Edit labels/annotations/finalizers in place under the kind lock.

        If the mutator removes the last finalizer on a row that's
        already marked for deletion, the row is hard-deleted and a
        DELETED event fires.
        """
        state = self._state_for(resource_type)
        async with state.lock:
            existing = state.rows.get((namespace, name))
            if existing is None:
                raise NotFound(kind=resource_type.__name__, namespace=namespace, name=name)
            draft = existing.model_copy(deep=True)
            uid = draft.metadata.uid
            generation = draft.metadata.generation
            rv = draft.metadata.resource_version
            ctime = draft.metadata.creation_timestamp
            dtime = draft.metadata.deletion_timestamp
            mutator(draft.metadata)
            draft.metadata.uid = uid
            draft.metadata.generation = generation
            draft.metadata.resource_version = rv
            draft.metadata.creation_timestamp = ctime
            draft.metadata.deletion_timestamp = dtime

            if dtime is not None and not draft.metadata.finalizers:
                del state.rows[(namespace, name)]
                event_rv = state.next_resource_version
                state.next_resource_version += 1
                state.bus.publish(
                    WatchEvent(
                        type=WatchEventType.DELETED,
                        resource=draft.model_copy(deep=True),
                        resource_version=event_rv,
                    )
                )
                return _narrow(draft, resource_type)

            stored = self._bump_version(state, draft)
            state.rows[(namespace, name)] = stored
            state.bus.publish(
                WatchEvent(
                    type=WatchEventType.MODIFIED,
                    resource=stored.model_copy(deep=True),
                    resource_version=stored.metadata.resource_version,
                )
            )
        return _narrow(stored.model_copy(deep=True), resource_type)

    # ---- watch ----------------------------------------------------------

    def subscription(self, resource_type: type[_TRes]) -> Subscription[_TRes]:
        """Open a synchronously-registered subscription bound to a kind.

        Returns a :class:`Subscription` that's an async context manager
        and async iterable::

            async with store.subscription(Workload) as sub:
                async for event in sub:
                    ...

        Synchronous registration ensures any ``apply`` issued after this
        call returns is observed (no async-generator race window).
        ``async with`` enforces cleanup even if the consumer never gets
        to the iteration — without it, a partial-init exception would
        leak the subscription in the bus's set.
        """
        state = self._state_for(resource_type)
        return Subscription(bus=state.bus, resource_type=resource_type)

    async def subscribe(
        self,
        resource_type: type[_TRes],
    ) -> AsyncIterator[WatchEvent[_TRes]]:
        """Convenience: register + iterate in one call.

        Equivalent to ``async with store.subscription(...) as sub:
        async for event in sub: yield event`` but as an async generator.
        Use only when the caller doesn't need to share the subscription
        across an outer context.
        """
        async with self.subscription(resource_type) as sub:
            async for event in sub:
                yield event

    def close(self) -> None:
        for state in self._kinds.values():
            state.bus.close()

    # ---- internals ------------------------------------------------------

    def _state_for(self, resource_type: type[_AnyResource]) -> _KindState:
        state = self._kinds.get(resource_type)
        if state is None:
            raise ValidationError(f"kind {resource_type.__name__} not registered")
        return state

    def _create(self, state: _KindState, desired: _AnyResource) -> _AnyResource:
        new_meta = desired.metadata.model_copy(deep=True)
        new_meta.uid = new_uid()
        new_meta.creation_timestamp = _now()
        new_meta.deletion_timestamp = None
        new_meta.generation = 1
        new_meta.resource_version = state.next_resource_version
        state.next_resource_version += 1
        stored = desired.model_copy(deep=True, update={"metadata": new_meta})
        state.rows[(stored.metadata.namespace, stored.metadata.name)] = stored
        return stored

    def _update_spec(
        self,
        state: _KindState,
        existing: _AnyResource,
        desired: _AnyResource,
    ) -> _AnyResource:
        spec_changed = existing.spec.model_dump() != desired.spec.model_dump()
        new_meta = desired.metadata.model_copy(deep=True)
        # System-managed metadata: preserve from existing.
        new_meta.uid = existing.metadata.uid
        new_meta.creation_timestamp = existing.metadata.creation_timestamp
        new_meta.deletion_timestamp = existing.metadata.deletion_timestamp
        new_meta.generation = existing.metadata.generation + (1 if spec_changed else 0)
        new_meta.resource_version = state.next_resource_version
        # Controller-managed metadata: preserve from existing. Apply is
        # the *user's* intent; finalizers and owner_refs are owned by
        # controllers (e.g. the agent's cleanup finalizer, a parent
        # set's owner_ref) and clobbering them on every spec change
        # would silently disable cascading delete and let the agent
        # think it has nothing to drain. Use patch_metadata to mutate
        # finalizers/owner_refs intentionally.
        new_meta.finalizers = list(existing.metadata.finalizers)
        new_meta.owner_refs = list(existing.metadata.owner_refs)
        state.next_resource_version += 1
        # Take desired's spec, preserve existing's status.
        merged = existing.model_copy(
            deep=True,
            update={
                "spec": copy.deepcopy(desired.spec),
                "metadata": new_meta,
            },
        )
        state.rows[(merged.metadata.namespace, merged.metadata.name)] = merged
        return merged

    def _commit_status(
        self,
        state: _KindState,
        existing: _AnyResource,
        draft: _AnyResource,
    ) -> _AnyResource:
        new_meta = draft.metadata.model_copy(deep=True)
        new_meta.uid = existing.metadata.uid
        new_meta.creation_timestamp = existing.metadata.creation_timestamp
        new_meta.deletion_timestamp = existing.metadata.deletion_timestamp
        new_meta.generation = existing.metadata.generation
        new_meta.resource_version = state.next_resource_version
        new_meta.owner_refs = list(
            existing.metadata.owner_refs
        )  # status writers don't own owner_refs
        state.next_resource_version += 1
        merged = existing.model_copy(
            deep=True,
            update={
                "status": copy.deepcopy(draft.status),
                "metadata": new_meta,
            },
        )
        state.rows[(merged.metadata.namespace, merged.metadata.name)] = merged
        return merged

    def _bump_version(
        self,
        state: _KindState,
        row: _AnyResource,
    ) -> _AnyResource:
        """Stamp a fresh ``resource_version`` on a metadata-modified row.

        The row's ``metadata`` has been mutated in place by the caller
        (delete/patch_metadata). We deep-copy it (so the dict-stored
        instance and the published-event copy don't share references),
        bump rv, and store. The mutation is metadata-only so spec /
        status remain untouched.
        """
        new_meta = row.metadata.model_copy(deep=True)
        new_meta.resource_version = state.next_resource_version
        state.next_resource_version += 1
        return row.model_copy(deep=True, update={"metadata": new_meta})


def _narrow(row: _AnyResource, resource_type: type[_TRes]) -> _TRes:
    """Recover the precise subclass type from the erased base.

    Rows are stored under their declared subclass at ``register_kind``
    time; the runtime instance *is* a ``_TRes``. ``isinstance`` narrows
    the type without ``cast()``.
    """
    if not isinstance(row, resource_type):
        raise ValidationError(
            (
                f"store row type mismatch: expected {resource_type.__name__}, "
                f"got {type(row).__name__}"
            )
        )
    return row
