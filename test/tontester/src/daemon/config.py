from pydantic import BaseModel


class DaemonConfig(BaseModel):
    # Address shown to users / baked into ``RegisterOk`` + ``/api/info``.
    # The daemon binds on this AND on the podman-network gateway (the latter
    # discovered at runtime from ``podman network inspect``) — the latter is
    # how the Grafana container reaches the Prometheus proxy. Nothing else.
    host: str = "127.0.0.1"
    dashboard_port: int = 8080
    grafana_port: int = 3000
    # Per-run Prometheus instances bind one port each from this inclusive range.
    prometheus_port_range: tuple[int, int] = (9100, 9499)
