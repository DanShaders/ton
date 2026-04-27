"""Hand-rolled k8s-shape orchestrator.

Public surface is small by design:

- ``resources/*`` — pydantic resource shapes (Namespace, Workload, ...)
- ``store/*`` — in-memory typed store + watch bus
- ``control/*`` — Manager, Controller, ControllerRunner, WorkQueue
- ``runtime/*`` — Runtime Protocol + SubprocessRuntime
- ``agent/*`` — Agent: in-process kubelet equivalent
- ``errors`` — typed exceptions

For local-dev orchestration, the daemon constructs a Manager + Agent
+ SubprocessRuntime in-process. For cluster deployments, the Manager
runs on one host and Agents run on each fleet host (transport TBD —
not in this prototype).

Crash semantics: orchestrator crash = payload crash. Agents heartbeat
the Manager and SIGKILL their workers if the connection drops; the
Manager keeps no durable state. Re-applying from the daemon's own
SQLite recovers desired state.
"""

from .agent import Agent
from .control import (
    Controller,
    ControllerRunner,
    ItemRef,
    Manager,
    Result,
    WatchSpec,
    WorkQueue,
    identity_mapper,
    owner_mapper,
)
from .control.reconcilers import NamespaceReconciler, WorkloadSetReconciler
from .errors import (
    AlreadyExists,
    Conflict,
    ManagerStopped,
    NotFound,
    OrchestratorError,
    ValidationError,
    WatchOverflow,
)
from .resources import (
    Condition,
    Container,
    HostBinaryImage,
    LabelSelector,
    Metadata,
    Namespace,
    NamespaceSpec,
    NamespaceStatus,
    NetworkPolicy,
    NetworkPolicySpec,
    NetworkPolicyStatus,
    OwnerRef,
    PortDecl,
    PortForward,
    PortForwardSpec,
    PortForwardStatus,
    Resource,
    ResourceLike,
    Workload,
    WorkloadSet,
    WorkloadSetSpec,
    WorkloadSetStatus,
    WorkloadSpec,
    WorkloadStatus,
    WorkloadTemplate,
)
from .runtime import FakeRuntime, Runtime, RuntimeEvent, RuntimeEventType, SubprocessRuntime
from .store import InMemoryStore, WatchEvent, WatchEventType

__all__ = [
    "Agent",
    "AlreadyExists",
    "Condition",
    "Conflict",
    "Container",
    "Controller",
    "ControllerRunner",
    "FakeRuntime",
    "HostBinaryImage",
    "InMemoryStore",
    "ItemRef",
    "LabelSelector",
    "Manager",
    "ManagerStopped",
    "Metadata",
    "Namespace",
    "NamespaceReconciler",
    "NamespaceSpec",
    "NamespaceStatus",
    "NetworkPolicy",
    "NetworkPolicySpec",
    "NetworkPolicyStatus",
    "NotFound",
    "OrchestratorError",
    "OwnerRef",
    "PortDecl",
    "PortForward",
    "PortForwardSpec",
    "PortForwardStatus",
    "Resource",
    "ResourceLike",
    "Result",
    "Runtime",
    "RuntimeEvent",
    "RuntimeEventType",
    "SubprocessRuntime",
    "ValidationError",
    "WatchEvent",
    "WatchEventType",
    "WatchOverflow",
    "WatchSpec",
    "WorkQueue",
    "Workload",
    "WorkloadSet",
    "WorkloadSetReconciler",
    "WorkloadSetSpec",
    "WorkloadSetStatus",
    "WorkloadSpec",
    "WorkloadStatus",
    "WorkloadTemplate",
    "identity_mapper",
    "owner_mapper",
]
