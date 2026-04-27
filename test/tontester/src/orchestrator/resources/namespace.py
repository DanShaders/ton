"""Cluster-scoped grouping primitive.

Namespaces here are *just* names — no quota, no defaults, no policies.
The daemon uses them as the unit of "a Run": create one Namespace per
Run, place every workload of that Run inside it, delete the Namespace
to cascade-delete the Run.

Children with an owner_ref to the Namespace block deletion until they're
gone (standard k8s cascade), so callers don't have to write a delete
loop themselves.
"""

from typing import Literal

from pydantic import Field

from .base import API_VERSION, Condition, Resource, StrictModel

KIND = "Namespace"


class NamespaceSpec(StrictModel):
    pass


class NamespaceStatus(StrictModel):
    phase: Literal["Active", "Terminating"] = "Active"
    conditions: list[Condition] = []


class Namespace(Resource):
    api_version: Literal["orchestrator/v1"] = API_VERSION
    kind: Literal["Namespace"] = KIND
    spec: NamespaceSpec = Field(default_factory=NamespaceSpec)
    status: NamespaceStatus = Field(default_factory=NamespaceStatus)
