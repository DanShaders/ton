"""L3/L4 allow rules for isolated workloads.

Same shape as k8s NetworkPolicy: select target workloads by label, then
list ingress/egress rules. Default-deny once a workload is selected by
*any* policy of the matching ``policy_types``: silent on workloads no
policy touches, restrictive on workloads it does. This matches k8s
exactly so we don't need to invent new mental models.

Reconciled per netns by the agent's nft reconciler. Workloads with
``network=host`` are silently skipped (host policies live elsewhere
and we don't want a workload-scoped resource to clobber them).
"""

from typing import Literal

from pydantic import Field

from .base import API_VERSION, Condition, LabelSelector, Resource, StrictModel

KIND = "NetworkPolicy"


class NetworkPeer(StrictModel):
    workload_selector: LabelSelector | None = None
    cidr: str | None = None


class NetworkPort(StrictModel):
    port: int | str
    protocol: Literal["tcp", "udp"] = "tcp"


class NetworkRule(StrictModel):
    """One allow rule.

    Empty ``peers`` = any peer; empty ``ports`` = any port. Both empty
    = "allow everything in this direction" which is rarely what you
    want but k8s permits, so we do too.
    """

    peers: list[NetworkPeer] = []
    ports: list[NetworkPort] = []


class NetworkPolicySpec(StrictModel):
    workload_selector: LabelSelector
    policy_types: list[Literal["Ingress", "Egress"]] = ["Ingress"]
    ingress: list[NetworkRule] = []
    egress: list[NetworkRule] = []


class NetworkPolicyStatus(StrictModel):
    conditions: list[Condition] = []


class NetworkPolicy(Resource):
    api_version: Literal["orchestrator/v1"] = API_VERSION
    kind: Literal["NetworkPolicy"] = KIND
    spec: NetworkPolicySpec
    status: NetworkPolicyStatus = Field(default_factory=NetworkPolicyStatus)
