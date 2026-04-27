"""Stable-identity replica set.

Like k8s' StatefulSet rather than ReplicaSet — child workloads have
deterministic names ``f"{set.name}-{ordinal}"`` and are not
interchangeable. Validators care: their keys are tied to the ordinal,
and Catchain wants stable peer IDs across restarts.

We don't ship a separate ReplicaSet kind because nothing we run wants
"any pod is fine" semantics. If something does later, add it then.
"""

from typing import Literal

from pydantic import Field

from .base import API_VERSION, Condition, LabelSelector, Resource, StrictModel
from .workload import WorkloadSpec

KIND = "WorkloadSet"


class UpdateStrategy(StrictModel):
    """How to roll spec changes into the existing replicas.

    ``Recreate`` deletes all then recreates — fastest, accepts downtime,
    matches today's tontester behavior.

    ``Rolling`` walks ordinals one at a time (or up to ``max_unavailable``
    in parallel), waits for ``Ready`` between batches. Slower, no full
    outage. Not implemented in the prototype reconciler — kept in the
    spec so we don't need a schema bump to add it later.
    """

    kind: Literal["Recreate", "Rolling"] = "Recreate"
    max_unavailable: int = 1


class WorkloadTemplate(StrictModel):
    """Spec + labels stamped onto every child workload.

    The set's ``selector`` must select these labels — we validate at
    apply time. This rules out the foot-gun where the selector and
    template labels diverge and the set can no longer find its kids.
    """

    labels: dict[str, str] = {}
    annotations: dict[str, str] = {}
    spec: WorkloadSpec


class WorkloadSetSpec(StrictModel):
    replicas: int = Field(ge=0)
    selector: LabelSelector
    template: WorkloadTemplate
    update_strategy: UpdateStrategy = Field(default_factory=UpdateStrategy)


class WorkloadSetStatus(StrictModel):
    replicas: int = 0
    ready_replicas: int = 0
    observed_generation: int = 0
    conditions: list[Condition] = []


class WorkloadSet(Resource):
    api_version: Literal["orchestrator/v1"] = API_VERSION
    kind: Literal["WorkloadSet"] = KIND
    spec: WorkloadSetSpec
    status: WorkloadSetStatus = Field(default_factory=WorkloadSetStatus)
