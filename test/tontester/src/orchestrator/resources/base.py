"""Base shapes for every orchestrator resource.

Every resource has the k8s triple — ``metadata`` (identity, ownership,
finalizers), ``spec`` (desired state, written by clients) and ``status``
(observed state, written only by controllers). The split is enforced
upstream in :mod:`orchestrator.store`: ``apply`` writes spec/metadata,
``set_status`` writes status; neither path touches the other.

``generation`` and ``resource_version`` work the way they do in k8s:

- ``generation`` bumps when spec changes — controllers compare against
  ``status.observed_generation`` to know whether a status condition is
  fresh or stale.
- ``resource_version`` bumps on *any* write (spec, status, metadata).
  It's the watch ordering key.

Both counters are monotonic per-kind, served by the in-memory store.
``uid`` is allocated by the store on first apply and never changes — owner
references key off it so they survive name reuse.

``api_version`` / ``kind`` are deliberately NOT on the generic
:class:`Resource` base. Pydantic discriminator unions require ``kind`` to
be a ``Literal`` on each subclass; basedpyright then refuses to widen
that to the base's ``str`` (LSP says the variance is wrong). Moving the
fields onto the subclasses sidesteps the conflict and keeps the wire
shape identical. :func:`make_ref` reads them from any
:class:`ResourceLike` so callers don't have to repeat the pattern.
"""

from collections.abc import Iterator, Mapping
from datetime import datetime
from typing import ClassVar, Generic, Literal, Protocol, Self, TypeVar

from pydantic import BaseModel, ConfigDict

API_VERSION = "orchestrator/v1"


class StrictModel(BaseModel):
    model_config: ClassVar[ConfigDict] = ConfigDict(extra="forbid")


class LabelExpression(StrictModel):
    key: str
    operator: Literal["In", "NotIn", "Exists", "DoesNotExist"]
    values: list[str] = []


class LabelSelector(StrictModel):
    """k8s-shape label selector. Empty selector matches everything."""

    match_labels: dict[str, str] = {}
    match_expressions: list[LabelExpression] = []

    def is_empty(self) -> bool:
        return not self.match_labels and not self.match_expressions


class ObjectRef(StrictModel):
    """Stable handle for a resource, used in owner refs and watch events."""

    api_version: str
    kind: str
    namespace: str | None
    name: str
    uid: str

    def key(self) -> tuple[str, str, str | None, str]:
        return (self.api_version, self.kind, self.namespace, self.name)


class OwnerRef(StrictModel):
    """Identifies the parent object that controls this child.

    At most one ``controller=True`` ref per object — otherwise two
    controllers would be racing on the same child. ``block_owner_deletion``
    holds the parent in ``Terminating`` until this child is gone.
    """

    api_version: str
    kind: str
    name: str
    uid: str
    controller: bool = False
    block_owner_deletion: bool = True


class Condition(StrictModel):
    """One typed status bit with a transition timestamp.

    ``observed_generation`` is the spec generation that produced this
    status — readers compare against ``metadata.generation`` to know if
    the controller has caught up with the latest spec.
    """

    type: str
    status: Literal["True", "False", "Unknown"]
    reason: str
    message: str
    last_transition: datetime
    observed_generation: int


class Metadata(StrictModel):
    """Identity, labels, and lifecycle bookkeeping.

    ``uid`` and the two counters (``generation`` / ``resource_version``)
    are owned by the store: clients submit specs without them, the store
    fills them in on apply. ``deletion_timestamp`` being non-None means
    the object is in graceful shutdown — finalizers must drain before the
    store can hard-delete the row.
    """

    name: str
    namespace: str | None = None
    labels: dict[str, str] = {}
    annotations: dict[str, str] = {}
    owner_refs: list[OwnerRef] = []
    finalizers: list[str] = []

    uid: str = ""
    generation: int = 0
    resource_version: int = 0
    creation_timestamp: datetime | None = None
    deletion_timestamp: datetime | None = None


class Resource(StrictModel):
    """Common base for every resource kind.

    Holds only :attr:`metadata`. Concrete subclasses (Workload,
    Namespace, ...) add their own ``spec`` / ``status`` fields with
    concrete types — and pin ``api_version`` / ``kind`` as Literals.
    Keeping the base non-generic sidesteps a pile of variance pain at
    the store boundary: ``Workload`` IS-A ``Resource`` cleanly, so the
    store can hold ``dict[..., Resource]`` and narrow back via
    isinstance against the registered class on the way out.

    For typing in places that need access to ``spec`` / ``status``
    without committing to the concrete subclass — primarily the store —
    use the covariant :class:`ResourceLike` Protocol below.
    """

    metadata: Metadata


_TSpec_co = TypeVar("_TSpec_co", bound=BaseModel, covariant=True)
_TStatus_co = TypeVar("_TStatus_co", bound=BaseModel, covariant=True)


class ResourceLike(Protocol, Generic[_TSpec_co, _TStatus_co]):
    """Read-only view of any resource — base + concrete subclasses both fit.

    Read-only by design — covariant in spec/status. Concrete pydantic
    classes (``Workload`` etc.) satisfy structurally because their
    field accesses behave like read-only properties for Protocol
    matching purposes. The store uses ``ResourceLike[BaseModel,
    BaseModel]`` as its TypeVar bound; concrete subclasses with
    ``spec: WorkloadSpec`` substitute via covariance.

    The ``model_copy`` / ``model_dump`` declarations are thin pydantic
    pass-throughs — they exist only so the store can clone and dump
    without sprinkling ``isinstance(BaseModel)`` checks. The ``Any``
    return on ``model_dump`` is the one place pydantic's stubs leak
    into our typing — confined to this Protocol, encapsulated.
    """

    @property
    def api_version(self) -> str: ...

    @property
    def kind(self) -> str: ...

    @property
    def metadata(self) -> Metadata: ...

    @property
    def spec(self) -> _TSpec_co: ...

    @property
    def status(self) -> _TStatus_co: ...

    def model_copy(
        self, *, deep: bool = False, update: Mapping[str, object] | None = None
    ) -> Self: ...

    def model_dump(self) -> dict[str, object]: ...


def make_ref(resource: "ResourceLike[BaseModel, BaseModel]") -> ObjectRef:
    return ObjectRef(
        api_version=resource.api_version,
        kind=resource.kind,
        namespace=resource.metadata.namespace,
        name=resource.metadata.name,
        uid=resource.metadata.uid,
    )


def upsert_condition(conditions: list[Condition], new: Condition) -> list[Condition]:
    """Replace the condition with the same ``type``, or append.

    Order is preserved so callers can read ``conditions[-1]`` for "what
    changed last" without re-sorting. Returns a new list — callers should
    treat conditions as immutable for race-resistance reasons.
    """
    out: list[Condition] = []
    found = False
    for c in conditions:
        if c.type == new.type:
            out.append(new)
            found = True
        else:
            out.append(c)
    if not found:
        out.append(new)
    return out


def iter_owner_refs(meta: Metadata) -> Iterator[OwnerRef]:
    yield from meta.owner_refs


def controller_owner(meta: Metadata) -> OwnerRef | None:
    for ref in meta.owner_refs:
        if ref.controller:
            return ref
    return None
