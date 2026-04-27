"""Pod-equivalent — a placement of one or more containers.

Multi-container pods exist (sidecars: log shipper, telemetry exporter)
but the common case is one container. ``containers[0]`` is the primary;
the rest are sidecars. Restart policy applies to the workload as a whole.

``image`` is one of:

- ``host_binary`` — a pre-existing executable on the host. Bind-mounted
  into the bundle as-is. No image pull, no rootfs synthesis.
- ``tarball`` — a directory containing a pre-extracted root filesystem,
  bind-mounted as the container's ``/``. We never pull OCI images at run
  time; if we ever need to, ``skopeo + umoci`` runs at install time and
  drops the result here.

``network=host`` shares the host's namespace (no isolation, no per-port
shaping). ``network=isolated`` gets its own user+net+mount+ipc+pid+uts
namespaces — required for ``network_shape`` (tc+netem) and per-workload
NetworkPolicy enforcement. ``host`` is the right default for most TON
validators because they bind 0.0.0.0:<adnl-port> and Catchain doesn't
play nicely with NAT.
"""

from datetime import datetime
from typing import Annotated, Literal

from pydantic import Field, TypeAdapter

from .base import API_VERSION, Condition, LabelSelector, Resource, StrictModel

KIND = "Workload"


class HostBinaryImage(StrictModel):
    kind: Literal["host_binary"] = "host_binary"
    path: str


class TarballImage(StrictModel):
    kind: Literal["tarball"] = "tarball"
    path: str


type ImageSpec = Annotated[HostBinaryImage | TarballImage, Field(discriminator="kind")]
IMAGE_ADAPTER: TypeAdapter[ImageSpec] = TypeAdapter(ImageSpec)


class Mount(StrictModel):
    host_path: str
    container_path: str
    readonly: bool = False
    kind: Literal["bind", "tmpfs"] = "bind"


class PortDecl(StrictModel):
    name: str
    port: int
    protocol: Literal["tcp", "udp"] = "tcp"


class ResourceLimits(StrictModel):
    """cgroup v2 limits. ``None`` means inherit from the parent cgroup."""

    cpu_millis: int | None = None
    memory_bytes: int | None = None
    io_weight: int | None = None


class NetworkShape(StrictModel):
    """tc+netem shaping applied inside the worker's netns.

    Requires ``WorkloadSpec.network = "isolated"`` — host-network mode
    cannot be shaped per-workload because every workload would clobber
    the host's qdisc.
    """

    delay_ms: int | None = None
    jitter_ms: int | None = None
    loss_pct: float | None = None
    bandwidth_bps: int | None = None


class HttpProbeAction(StrictModel):
    kind: Literal["http"] = "http"
    port_name: str
    path: str = "/"


class TcpProbeAction(StrictModel):
    kind: Literal["tcp"] = "tcp"
    port_name: str


class ExecProbeAction(StrictModel):
    kind: Literal["exec"] = "exec"
    command: list[str]


type ProbeAction = Annotated[
    HttpProbeAction | TcpProbeAction | ExecProbeAction,
    Field(discriminator="kind"),
]
PROBE_ACTION_ADAPTER: TypeAdapter[ProbeAction] = TypeAdapter(ProbeAction)


class Probe(StrictModel):
    """Periodic health check.

    ``failure_threshold`` × ``period_s`` is the wall time before we give
    up on a probe. Default ~30s — long enough for cold-starting a TON
    validator, short enough to fail a wedged container fast.
    """

    action: ProbeAction
    initial_delay_s: float = 0.0
    period_s: float = 1.0
    timeout_s: float = 5.0
    failure_threshold: int = 30


class Container(StrictModel):
    name: str
    image: ImageSpec
    command: list[str] = []
    env: dict[str, str] = {}
    mounts: list[Mount] = []
    resources: ResourceLimits = Field(default_factory=ResourceLimits)
    workdir: str | None = None


class WorkloadSpec(StrictModel):
    containers: list[Container] = Field(min_length=1)
    network: Literal["host", "isolated"] = "host"
    network_shape: NetworkShape | None = None
    ports: list[PortDecl] = []
    restart_policy: Literal["Never", "OnFailure", "Always"] = "OnFailure"
    host_selector: LabelSelector = Field(default_factory=LabelSelector)
    readiness_probe: Probe | None = None
    termination_grace_s: float = 30.0


class ContainerStatus(StrictModel):
    name: str
    state: Literal["Waiting", "Running", "Terminated"]
    pid: int | None = None
    started_at: datetime | None = None
    finished_at: datetime | None = None
    exit_code: int | None = None
    restart_count: int = 0


class WorkloadStatus(StrictModel):
    phase: Literal["Pending", "Running", "Succeeded", "Failed"] = "Pending"
    host: str | None = None
    network_address: str | None = None
    container_statuses: list[ContainerStatus] = []
    conditions: list[Condition] = []


class Workload(Resource):
    api_version: Literal["orchestrator/v1"] = API_VERSION
    kind: Literal["Workload"] = KIND
    spec: WorkloadSpec
    status: WorkloadStatus = Field(default_factory=WorkloadStatus)
