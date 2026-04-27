"""Expose a workload's named port on a host:port.

Simpler than k8s Ingress (no L7) and simpler than k8s Service (no
load-balancing across replicas) — the selector must resolve to exactly
one workload. The agent's reconciler binds host_port:0 if ``host_port``
is None and writes the chosen port back to ``status.bound_host_port``.

Used by the daemon for: per-run Prometheus reachable from the harness,
Grafana on a fixed loopback port, validator lite-server reachable from
``ton lite-client``.
"""

from typing import Literal

from pydantic import Field

from .base import API_VERSION, Condition, LabelSelector, Resource, StrictModel

KIND = "PortForward"


class PortForwardSpec(StrictModel):
    workload_selector: LabelSelector
    target_port: str
    host: str = "127.0.0.1"
    host_port: int | None = None


class PortForwardStatus(StrictModel):
    bound_host: str | None = None
    bound_host_port: int | None = None
    conditions: list[Condition] = []


class PortForward(Resource):
    api_version: Literal["orchestrator/v1"] = API_VERSION
    kind: Literal["PortForward"] = KIND
    spec: PortForwardSpec
    status: PortForwardStatus = Field(default_factory=PortForwardStatus)
