"""Cross-module exceptions.

Tight, explicit set: one per error mode the caller has to disambiguate.
``Conflict`` carries the offending versions so retries can re-fetch
without an extra round-trip; ``NotFound`` carries the (kind, namespace,
name) triple so logs name the missing object. None of these errors hold
a resource instance — they're raised exactly where no instance is
available — so they take typed strings + the class name from the type
the caller already had in hand.
"""


class OrchestratorError(RuntimeError):
    """Base for everything raised by the orchestrator package."""


class NotFound(OrchestratorError):
    def __init__(self, *, kind: str, namespace: str | None, name: str):
        super().__init__(f"resource not found: {kind}/{namespace}/{name}")
        self.kind: str = kind
        self.namespace: str | None = namespace
        self.name: str = name


class Conflict(OrchestratorError):
    """Optimistic-concurrency failure on update.

    Raised when ``expected_version`` doesn't match the current row's
    ``resource_version``. The caller should re-fetch and retry.
    """

    def __init__(
        self,
        *,
        kind: str,
        namespace: str | None,
        name: str,
        expected: int,
        actual: int,
    ):
        super().__init__(
            (f"version conflict on {kind}/{namespace}/{name}: expected={expected} actual={actual}")
        )
        self.kind: str = kind
        self.namespace: str | None = namespace
        self.name: str = name
        self.expected: int = expected
        self.actual: int = actual


class AlreadyExists(OrchestratorError):
    def __init__(self, *, kind: str, namespace: str | None, name: str):
        super().__init__(f"resource already exists: {kind}/{namespace}/{name}")
        self.kind: str = kind
        self.namespace: str | None = namespace
        self.name: str = name


class WatchOverflow(OrchestratorError):
    """Watch buffer filled before the consumer drained.

    The consumer must close, re-list to get a fresh snapshot, and
    re-subscribe with the snapshot's resource_version. Mirrors k8s'
    "410 Gone" on stale watches.
    """

    def __init__(self, kind: str):
        super().__init__(f"watch overflow on kind {kind}; re-list required")
        self.kind: str = kind


class ValidationError(OrchestratorError):
    """Spec-level validation a controller would reject (selector mismatch, etc.).

    Distinct from pydantic ValidationError (which is structural). This
    one means "the structure is fine but the relationship between fields
    is invalid".
    """


class ManagerStopped(OrchestratorError):
    def __init__(self) -> None:
        super().__init__("orchestrator manager is stopping; new operations are refused")


class NamespaceTerminating(OrchestratorError):
    """Write rejected because the target namespace is terminating.

    Once a namespace has a ``deletion_timestamp``, the store rejects
    new resource creations / updates inside it. This eliminates the
    cascade-orphan race: a child can't slip in between the cascade
    reconciler's enumerate and the finalizer drop. Cleanup-style
    operations (``delete``, ``patch_status``, ``patch_metadata`` for
    finalizer maintenance) remain allowed.
    """

    def __init__(self, namespace: str):
        super().__init__(f"namespace {namespace!r} is terminating; new writes are refused")
        self.namespace: str = namespace
