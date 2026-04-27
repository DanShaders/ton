"""Pydantic resource shapes for the orchestrator API.

Every kind has the ``Resource[Spec, Status]`` triple defined in
:mod:`.base`. Sub-modules pin a literal ``api_version`` + ``kind`` so
that pydantic discriminator unions can dispatch on ``kind`` without
ambiguity.
"""

from .base import (
    API_VERSION,
    Condition,
    LabelExpression,
    LabelSelector,
    Metadata,
    ObjectRef,
    OwnerRef,
    Resource,
    ResourceLike,
    StrictModel,
    controller_owner,
    iter_owner_refs,
    make_ref,
    upsert_condition,
)
from .matching import matches, select
from .namespace import Namespace, NamespaceSpec, NamespaceStatus
from .networkpolicy import (
    NetworkPeer,
    NetworkPolicy,
    NetworkPolicySpec,
    NetworkPolicyStatus,
    NetworkPort,
    NetworkRule,
)
from .portforward import PortForward, PortForwardSpec, PortForwardStatus
from .workload import (
    Container,
    ContainerStatus,
    ExecProbeAction,
    HostBinaryImage,
    HttpProbeAction,
    ImageSpec,
    Mount,
    NetworkShape,
    PortDecl,
    Probe,
    ProbeAction,
    ResourceLimits,
    TarballImage,
    TcpProbeAction,
    Workload,
    WorkloadSpec,
    WorkloadStatus,
)
from .workloadset import (
    UpdateStrategy,
    WorkloadSet,
    WorkloadSetSpec,
    WorkloadSetStatus,
    WorkloadTemplate,
)

__all__ = [
    "API_VERSION",
    "Condition",
    "Container",
    "ContainerStatus",
    "ExecProbeAction",
    "HostBinaryImage",
    "HttpProbeAction",
    "ImageSpec",
    "LabelExpression",
    "LabelSelector",
    "Metadata",
    "Mount",
    "Namespace",
    "NamespaceSpec",
    "NamespaceStatus",
    "NetworkPeer",
    "NetworkPolicy",
    "NetworkPolicySpec",
    "NetworkPolicyStatus",
    "NetworkPort",
    "NetworkRule",
    "NetworkShape",
    "ObjectRef",
    "OwnerRef",
    "PortDecl",
    "PortForward",
    "PortForwardSpec",
    "PortForwardStatus",
    "Probe",
    "ProbeAction",
    "Resource",
    "ResourceLike",
    "ResourceLimits",
    "StrictModel",
    "TarballImage",
    "TcpProbeAction",
    "UpdateStrategy",
    "Workload",
    "WorkloadSet",
    "WorkloadSetSpec",
    "WorkloadSetStatus",
    "WorkloadSpec",
    "WorkloadStatus",
    "WorkloadTemplate",
    "controller_owner",
    "iter_owner_refs",
    "make_ref",
    "matches",
    "select",
    "upsert_condition",
]
